#pragma once

#include "Constants.h"
#include "Fft.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace rsdsp
{
    class HowlDetector
    {
    public:
        struct Event
        {
            double freq = 0.0;
            double levelDb = -200.0;
            double growthDbPerSec = 0.0;   // Pegelanstieg des Tracks (dB/s), 0 wenn unbekannt
        };

        static constexpr int maxEvents = 8;
        static constexpr int maxTracks = 20;
        static constexpr int historyLen = 24;
        static constexpr int maxPeaks = 48;
        static constexpr int maxSpecBins = kMaxFftSize / 2 + 1;
        static constexpr int maxSuspects = 8;
        static constexpr double suspectSeconds = 0.25;

        // -3-dB-Bandbreite eines Spektralpeaks, gemessen in Bins.
        float peakWidthBins(int peakBin, float limitDb = 3.0f) const noexcept
        {
            const int nb = getNumSpectrumBins();
            if (peakBin < 2 || peakBin > nb - 3)
                return 1.0e9f;

            const auto& sp = getSpectrum();
            const float top = sp[(size_t)peakBin].load(std::memory_order_relaxed);

            int lo = peakBin;
            int hi = peakBin;

            while (lo > 1 && top - sp[(size_t)(lo - 1)].load(std::memory_order_relaxed) < limitDb)
                --lo;
            while (hi < nb - 2 && top - sp[(size_t)(hi + 1)].load(std::memory_order_relaxed) < limitDb)
                ++hi;

            return (float)(hi - lo + 1);
        }

        // Einmess-Messung: Detektor ist empfindlicher und bestaetigt schneller,
        // weil dort keine Musik im Signal ist, die faelschlich erkannt werden koennte.
        static constexpr float measureRelaxDb = 3.0f;   // PNPR-/PAPR-Schwellen sinken um diesen Wert

        void prepare(double sampleRate)
        {
            fs = sampleRate;

            int want = kMinFftSize;
            while (want < (int)(fs * kWindowSeconds) && want < kMaxFftSize)
                want <<= 1;

            n = want;

            fft.prepare(n);
            maskN = n - 1;   // n ist immer eine Zweierpotenz
            frameIn.assign((size_t)n, 0.0f);

            ring.assign((size_t)n, 0.0f);

            window.assign((size_t)n, 0.0f);
            for (int i = 0; i < n; ++i)
                window[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * (double)i / (double)n));

            frame.assign((size_t)n * 2, 0.0f);
            powerDb.assign((size_t)n / 2 + 1, -200.0f);
            prevPowerDb.assign((size_t)n / 2 + 1, -200.0f);

            binHz = fs / (double)n;
            kLow = std::max(1, (int)std::ceil(kBandLowHz / binHz));
            kHigh = std::min(n / 2 - 1, (int)std::floor(std::min(kBandHighHz, 0.45 * fs) / binHz));

            // Schnell-Screen: 2048-FFT
            prepareFast();

            setHopDivisor(hopDivisor);
            reset();
        }

        void reset()
        {
            std::fill(ring.begin(), ring.end(), 0.0f);
            writePos = 0;
            sinceHop = 0;

            std::fill(ringFast.begin(), ringFast.end(), 0.0f);
            writePosFast = 0;
            sinceHopFast = 0;
            clearSuspects();

            for (auto& t : tracks)
                t = Track{};

            for (auto& s : spectrum)
                s.store(-120.0f, std::memory_order_relaxed);

            std::fill(prevPowerDb.begin(), prevPowerDb.end(), -200.0f);
        }

        void setSensitivity(double s) noexcept { sensitivity = std::min(1.0, std::max(0.0, s)); }

        // true waehrend der Messung im Einmessen-Modus (siehe measureRelaxDb)
        void setMeasurementMode(bool on) noexcept { measurementMode = on; }

        void setHopDivisor(int div) noexcept
        {
            hopDivisor = std::min(std::max(div, 2), 128);
            hop = std::max(16, n / hopDivisor);

            // Schnell-Screen laeuft doppelt so schnell wie der Haupt-Detektor
            hopFast = std::max(16, hop / 2);
        }

        void setNotchQuery(bool (*fn) (void*, double), void* ctx) noexcept
        {
            nearNotch = fn;
            nearNotchCtx = ctx;
        }

        double getBinHz()   const noexcept { return binHz; }
        int    getFftSize() const noexcept { return n; }
        int    getHop()     const noexcept { return hop; }

        int push(const float* x, int num, Event* out) noexcept
        {
            int count = 0;

            for (int i = 0; i < num; ++i)
            {
                ring[(size_t)writePos] = x[i];
                writePos = (writePos + 1) & maskN;

                // Schnell-Screen fuettern
                ringFast[(size_t)writePosFast] = x[i];
                writePosFast = (writePosFast + 1) & maskFast;

                ++clock;

                if (++sinceHopFast >= hopFast)
                {
                    sinceHopFast = 0;
                    screenFast();   // traegt Verdachtsfrequenzen in die Verdachtsliste ein
                }

                if (++sinceHop >= hop)
                {
                    sinceHop = 0;

                    // Hauptanalyse: regulaer alle hop Samples. Frequenzen aus der
                    // Verdachtsliste des Schnell-Screens werden dort schneller bestaetigt.
                    if (count < maxEvents)
                        count += analyseFrame(out + count, maxEvents - count);
                }
            }

            return count;
        }

        const std::array<std::atomic<float>, maxSpecBins>& getSpectrum() const noexcept { return spectrum; }
        int getNumSpectrumBins() const noexcept { return n / 2 + 1; }

    private:
        struct Track
        {
            bool active = false;
            int frames = 0;
            int missing = 0;
            int count = 0;
            double freq = 0.0;

            std::array<float, historyLen> level{}, freqBins{};
        };

        struct Suspect
        {
            bool active = false;
            double freq = 0.0;
            long long lastSeen = 0;
        };

        struct Peak
        {
            double bin = 0.0;
            float level = -200.0f;
            float pnpr = 0.0f;
        };

        struct Candidate
        {
            double bin = 0.0;
            float level = -200.0f;
            bool harmonic = false;
        };

        static float toDb(float p) noexcept { return 10.0f * std::log10(p + 1.0e-20f); }

        // ================================================================
        // Schnell-Screen: 2048-FFT, nur PNPR-Check, keine Tracks
        // ================================================================
        void prepareFast()
        {
            nFast = kFastFftSize;

            while (nFast < (int)(fs * 0.045) && nFast < 4096)
                nFast <<= 1;

            fftFast.prepare(nFast);
            maskFast = nFast - 1;   // nFast ist immer eine Zweierpotenz
            frameFastIn.assign((size_t)nFast, 0.0f);

            ringFast.assign((size_t)nFast, 0.0f);

            windowFast.assign((size_t)nFast, 0.0f);
            for (int i = 0; i < nFast; ++i)
                windowFast[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * (double)i / (double)nFast));

            frameFast.assign((size_t)nFast * 2, 0.0f);
            powerDbFast.assign((size_t)nFast / 2 + 1, -200.0f);

            binHzFast = fs / (double)nFast;
            kLowFast = std::max(1, (int)std::ceil(kBandLowHz / binHzFast));
            kHighFast = std::min(nFast / 2 - 1, (int)std::floor(std::min(kBandHighHz, 0.45 * fs) / binHzFast));
        }

        void screenFast() noexcept
        {
            for (int i = 0; i < nFast; ++i)
                frameFastIn[(size_t)i] = ringFast[(size_t)((writePosFast + i) & maskFast)] * windowFast[(size_t)i];

            fftFast.forward(frameFastIn.data(), frameFast.data());

            const float scale = 4.0f / (float)nFast;
            const int half = nFast / 2;

            for (int k = 0; k <= half; ++k)
            {
                const float re = frameFast[(size_t)(2 * k)];
                const float im = frameFast[(size_t)(2 * k + 1)];
                powerDbFast[(size_t)k] = toDb((re * re + im * im) * scale * scale);
            }

            // PNPR-Check mit niedriger Schwelle (12 dB)
            for (int k = kLowFast + 2; k <= kHighFast - 2; ++k)
            {
                const float p = powerDbFast[(size_t)k];

                if (p < -72.0f)
                    continue;

                if (!(p > powerDbFast[(size_t)(k - 1)] && p >= powerDbFast[(size_t)(k + 1)]))
                    continue;

                float sum = 0.0f;
                int cnt = 0;

                for (int d = 3; d <= 6; ++d)
                {
                    const int lo = k - d, hi = k + d;

                    if (lo >= 1) { sum += (float)std::pow(10.0, powerDbFast[(size_t)lo] / 10.0); ++cnt; }
                    if (hi <= half - 1) { sum += (float)std::pow(10.0, powerDbFast[(size_t)hi] / 10.0); ++cnt; }
                }

                if (cnt < 2)
                    continue;

                const float neighbour = toDb((float)(sum / cnt));

                if (p - neighbour > 12.0f)
                    recordSuspect((double)k * binHzFast);
            }
        }

        // Verdachtsliste: der Schnell-Screen traegt hier Frequenzen ein, die
        // Hauptanalyse verkuerzt fuer solche Frequenzen die Bestaetigung.
        void recordSuspect(double freq) noexcept
        {
            const double tol = 1.5 * binHzFast;

            int slot = 0;
            long long oldest = clock + 1;

            for (int i = 0; i < maxSuspects; ++i)
            {
                auto& sp = suspects[(size_t)i];

                if (sp.active && std::abs(sp.freq - freq) <= tol)
                {
                    sp.freq = freq;
                    sp.lastSeen = clock;
                    return;
                }

                const long long seen = sp.active ? sp.lastSeen : -1;

                if (seen < oldest)
                {
                    oldest = seen;
                    slot = i;
                }
            }

            suspects[(size_t)slot].active = true;
            suspects[(size_t)slot].freq = freq;
            suspects[(size_t)slot].lastSeen = clock;
        }

        bool isSuspect(double freq) const noexcept
        {
            const double tol = 1.5 * binHzFast + binHz;
            const long long maxAge = (long long)(fs * suspectSeconds);

            for (const auto& sp : suspects)
                if (sp.active && clock - sp.lastSeen <= maxAge && std::abs(sp.freq - freq) <= tol)
                    return true;

            return false;
        }

        void clearSuspects() noexcept
        {
            for (auto& sp : suspects)
                sp = Suspect{};

            clock = 0;
        }

        // ================================================================
        // Hauptanalyse: volle Erkennung mit Tracks und Bestaetigung
        // ================================================================
        int analyseFrame(Event* out, int capacity) noexcept
        {
            for (int i = 0; i < n; ++i)
                frameIn[(size_t)i] = ring[(size_t)((writePos + i) & maskN)] * window[(size_t)i];

            fft.forward(frameIn.data(), frame.data());

            const float scale = 4.0f / (float)n;
            const int half = n / 2;

            double sumLin = 0.0;

            for (int k = 0; k <= half; ++k)
            {
                const float re = frame[(size_t)(2 * k)];
                const float im = frame[(size_t)(2 * k + 1)];
                const float p = (re * re + im * im) * scale * scale;

                powerDb[(size_t)k] = toDb(p);

                if (k >= kLow && k <= kHigh)
                    sumLin += p;

                auto& s = spectrum[(size_t)k];
                const float prev = s.load(std::memory_order_relaxed);
                s.store(prev + 0.35f * (powerDb[(size_t)k] - prev), std::memory_order_relaxed);
            }

            // Spektraler Fluss berechnen (Empfehlung 6)
            float maxFlux = 0.0f;
            int fluxBin = -1;

            for (int k = kLow; k <= kHigh; ++k)
            {
                const float flux = powerDb[(size_t)k] - prevPowerDb[(size_t)k];

                if (flux > maxFlux)
                {
                    maxFlux = flux;
                    fluxBin = k;
                }
            }

            prevPowerDb = powerDb;

            const float avgDb = toDb((float)(sumLin / (double)std::max(1, kHigh - kLow + 1)));

            const double s = sensitivity;
            const float relax = measurementMode ? measureRelaxDb : 0.0f;
            const float pnprMin = (float)(28.0 - 10.0 * s) - relax; // Nachbarabstand muss 4 dB größer sein
            const float paprMin = (float)(32.0 - 8.0 * s) - relax;  // Pegelüberhöhung muss 4 dB größer sein
            const float floorDb = -72.0f;

            numPeaks = 0;

            for (int k = kLow; k <= kHigh && numPeaks < maxPeaks; ++k)
            {
                const float p = powerDb[(size_t)k];

                if (p < floorDb || !isLocalMax(k))
                    continue;

                const float pnpr = p - neighbourDb(k);

                if (pnpr < 10.0f)
                    continue;

                peaks[(size_t)numPeaks++] = { (double)k + parabolicOffset(k), p, pnpr };
            }

            std::array<Candidate, maxEvents> cand{};
            int numCand = 0;

            for (int i = 0; i < numPeaks; ++i)
            {
                const auto& pk = peaks[(size_t)i];

                if (pk.level - avgDb < paprMin || pk.pnpr < pnprMin)
                    continue;

                Candidate c;
                c.bin = pk.bin;
                c.level = pk.level;

              

                // Empfehlung 4: Oberton-Test wieder aktiv
                c.harmonic = isPartOfHarmonicSeries(i);

                if (numCand < maxEvents)
                {
                    cand[(size_t)numCand++] = c;
                }
                else
                {
                    int weakest = 0;

                    for (int j = 1; j < maxEvents; ++j)
                        if (cand[(size_t)j].level < cand[(size_t)weakest].level)
                            weakest = j;

                    if (c.level > cand[(size_t)weakest].level)
                        cand[(size_t)weakest] = c;
                }
            }

            int written = 0;
            std::array<bool, maxTracks> hit{};

            for (int c = 0; c < numCand; ++c)
            {
                const auto& cd = cand[(size_t)c];

                int best = -1;
                double bestDist = 1.5;

                for (int t = 0; t < maxTracks; ++t)
                {
                    if (!tracks[(size_t)t].active || hit[(size_t)t])
                        continue;

                    const double d = std::abs(tracks[(size_t)t].freq / binHz - cd.bin);

                    if (d < bestDist)
                    {
                        bestDist = d;
                        best = t;
                    }
                }

                if (best < 0)
                    best = allocateTrack();

                auto& tr = tracks[(size_t)best];

                if (!tr.active)
                {
                    tr = Track{};
                    tr.active = true;
                }

                hit[(size_t)best] = true;

                if (tr.count > 0 && cd.level - tr.level[0] > 6.0f)
                {
                    tr.frames = 0;
                    tr.count = 0;
                }

                tr.missing = 0;
                tr.frames++;
                tr.freq = cd.bin * binHz;

                for (int h = historyLen - 1; h > 0; --h)
                {
                    tr.level[(size_t)h] = tr.level[(size_t)(h - 1)];
                    tr.freqBins[(size_t)h] = tr.freqBins[(size_t)(h - 1)];
                }

                tr.level[0] = cd.level;
                tr.freqBins[0] = (float)cd.bin;
                tr.count = std::min(tr.count + 1, historyLen);

                if (tr.count > 8 && tr.level[0] - tr.level[8] < -1.5f)
                    tr.frames = 0;

                // Empfehlung 9: Basis-Bestaetigung auf 5 Frames bei s=0.8
                int required = (int)std::lround(14.0 - 12.0 * s);
                double minSlope = -0.12;

                if (cd.harmonic)
                {
                    required *= 3;
                    minSlope = 0.1;
                }

                // Empfehlung 1: Adaptives Bestaetigungstempo
                if (tr.count >= 4)
                {
                    const double recentSlope = slopeOf(tr, 0, std::min(tr.count, 6));

                    if (recentSlope > 1.5)
                        required = std::max(3, required / 2);
                    else if (recentSlope > 0.8)
                        required = std::max(4, (required * 3) / 4);
                }

                // ---- Schmalband-Gate ------------------------------------------------
               // Ein Howl ist extrem schmalbandig (~1.5-2 Bins). Sprachformanten und
               // Gerauschbursts sind breiter und sterben hier. Bei sehr schwachen
               // Peaks (gerade entstehendes Feedback) ist die Breite im Rauschen
               // unverlaesslich -> Gate erst ab -60 dBFS.
                {
                    const auto& sp = getSpectrum();
                    const float top = sp[(size_t)cd.bin].load(std::memory_order_relaxed);

                    if (top > -60.0f)
                    {
                        const bool  meas = (relax > 0.0f);
                        const float limit = meas ? kMaxHowlWidthBins * 1.5f : kMaxHowlWidthBins;
                        if (peakWidthBins((int)cd.bin) > limit)
                            return {};
                    }
                }
                // Spektraler Fluss beschleunigt die Bestaetigung bei plaetzlichem Anstieg.
        // Transienten (Schlagzeug, Plosive) sind breitbandig und sterben bereits
        // im Gate – hier kommt nur echtes, anschwellendes Feedback durch.
                if (maxFlux > 6.0f && fluxBin > 0
                    && std::abs((double)fluxBin - cd.bin) < 2.0)
                {
                    required = std::max(3, (required * 3) / 4);
                }

                // Verdachtsliste des Schnell-Screens: Bestaetigung verkuerzen
                if (isSuspect(tr.freq))
                    required = std::max(3, (required * 2) / 3);

                // Messung im Einmessen-Modus: Bestaetigung zusaetzlich verkuerzen
                if (measurementMode)
                    required = std::max(3, (required * 2) / 3);

                if (nearNotch != nullptr && nearNotch(nearNotchCtx, tr.freq))
                {
                    required = std::max(2, required / 3);
                    minSlope = std::max(minSlope, -0.05);
                }

                if (tr.frames >= required
                    && qualifies(tr, minSlope)
                    && (!cd.harmonic || growsSteadily(tr))
                    && written < capacity)
                {
                    out[written].freq = tr.freq;
                    out[written].levelDb = cd.level;

                    // Pegelanstieg der letzten Frames in dB/s (positiv = waechst)
                    out[written].growthDbPerSec = (tr.count >= 3)
                        ? slopeOf(tr, 0, std::min(tr.count, 6)) * fs / (double)hop
                        : 0.0;

                    ++written;

                    tr.frames = 0;
                }
            }

            for (int t = 0; t < maxTracks; ++t)
            {
                auto& tr = tracks[(size_t)t];

                if (tr.active && !hit[(size_t)t] && ++tr.missing > 2)
                    tr.active = false;
            }

            return written;
        }

        bool isLocalMax(int k) const noexcept
        {
            const int half = (int)powerDb.size() - 1;
            const float p = powerDb[(size_t)k];

            if (k - 1 >= 0 && p <= powerDb[(size_t)(k - 1)]) return false;
            if (k + 1 <= half && p < powerDb[(size_t)(k + 1)]) return false;
            if (k - 2 >= 0 && p < powerDb[(size_t)(k - 2)]) return false;
            if (k + 2 <= half && p < powerDb[(size_t)(k + 2)]) return false;

            return true;
        }

        float neighbourDb(int k) const noexcept
        {
            const int half = (int)powerDb.size() - 1;

            double sum = 0.0;
            int cnt = 0;

            for (int d = 4; d <= 9; ++d)
            {
                const int lo = k - d;
                const int hi = k + d;

                if (lo >= 1) { sum += std::pow(10.0, powerDb[(size_t)lo] / 10.0); ++cnt; }
                if (hi <= half - 1) { sum += std::pow(10.0, powerDb[(size_t)hi] / 10.0); ++cnt; }
            }

            if (cnt < 3)
                return -200.0f;

            return toDb((float)(sum / (double)cnt));
        }

        // Empfehlung 5: 5-Punkt-Interpolation statt 3-Punkt-Parabel
        double parabolicOffset(int k) const noexcept
        {
            const int half = (int)powerDb.size() - 1;

            if (k < 2 || k >= half - 1)
                return 0.0;

            const double a = powerDb[(size_t)(k - 2)];
            const double b = powerDb[(size_t)(k - 1)];
            const double c = powerDb[(size_t)k];
            const double d = powerDb[(size_t)(k + 1)];
            const double e = powerDb[(size_t)(k + 2)];

            const double den = a - 2.0 * b + c;
            if (std::abs(den) < 1.0e-9)
                return 0.0;

            double off = 0.5 * (a - c) / den;

            const double den2 = c - 2.0 * d + e;
            if (std::abs(den2) > 1.0e-9)
            {
                double off2 = 0.5 * (c - e) / den2;
                off = (off + off2) * 0.5;
            }

            return std::min(0.5, std::max(-0.5, off));
        }

        bool qualifies(const Track& tr, double minSlope) const noexcept
        {
            const int w = std::min(tr.count, 16);

            if (w < 3)
                return false;

            double sx = 0, sy = 0, sxx = 0, sxy = 0, mf = 0;

            for (int i = 0; i < w; ++i)
            {
                const double x = -i;
                const double y = tr.level[(size_t)i];

                sx += x; sy += y; sxx += x * x; sxy += x * y;
                mf += tr.freqBins[(size_t)i];
            }

            const double slope = (w * sxy - sx * sy) / (w * sxx - sx * sx);
            mf /= w;

            double var = 0;

            for (int i = 0; i < w; ++i)
                var += (tr.freqBins[(size_t)i] - mf) * (tr.freqBins[(size_t)i] - mf);

            const double freqStd = std::sqrt(var / w);

            return slope > minSlope && freqStd < 0.35;
        }

        static double slopeOf(const Track& tr, int from, int len) noexcept
        {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;

            for (int i = from; i < from + len; ++i)
            {
                const double x = -i;
                const double y = tr.level[(size_t)i];

                sx += x; sy += y; sxx += x * x; sxy += x * y;
            }

            return (len * sxy - sx * sy) / (len * sxx - sx * sx);
        }

        static bool growsSteadily(const Track& tr) noexcept
        {
            return tr.count >= 24
                && slopeOf(tr, 0, 8) > 0.05
                && slopeOf(tr, 8, 8) > 0.05
                && slopeOf(tr, 16, 8) > 0.05;
        }

        int allocateTrack() noexcept
        {
            for (int t = 0; t < maxTracks; ++t)
                if (!tracks[(size_t)t].active)
                    return t;

            int victim = 0;

            for (int t = 1; t < maxTracks; ++t)
                if (tracks[(size_t)t].frames < tracks[(size_t)victim].frames)
                    victim = t;

            tracks[(size_t)victim].active = false;

            return victim;
        }

        // Empfehlung 4: Oberton-Test wieder aktiv
        bool isPartOfHarmonicSeries(int idx) const noexcept
        {
            const auto& me = peaks[(size_t)idx];

            for (int j = 0; j < numPeaks; ++j)
            {
                if (j == idx)
                    continue;

                const auto& other = peaks[(size_t)j];

                if (other.level < me.level - 30.0f)
                    continue;

                if (other.bin < me.bin)
                {
                    const double ratio = me.bin / other.bin;
                    const double m = std::round(ratio);

                    if (m >= 2.0 && m <= 8.0
                        && std::abs(me.bin - m * other.bin) <= std::max(1.5, 0.01 * me.bin))
                        return true;
                }
                else if (std::abs(other.bin - 2.0 * me.bin) <= std::max(1.5, 0.01 * other.bin)
                    && other.level > me.level - 24.0f)
                {
                    return true;
                }
            }

            return false;
        }

        double fs = 48000.0, binHz = 5.86;
        int n = 8192, hop = 512, hopDivisor = 16;
        int kLow = 7, kHigh = 3413;
        int writePos = 0, sinceHop = 0;

        double sensitivity = 0.8;
        bool measurementMode = false;

        RealFft fft;
        int maskN = 8191;

        std::vector<float> ring, window, powerDb, prevPowerDb, frame, frameIn;

        // Schnell-Screen
        RealFft fftFast;
        int maskFast = 2047;
        int nFast = 2048, hopFast = 256;
        int kLowFast = 2, kHighFast = 853;
        double binHzFast = 23.4;
        int writePosFast = 0, sinceHopFast = 0;
        std::array<Suspect, maxSuspects> suspects{};
        long long clock = 0;

        std::vector<float> ringFast, windowFast, powerDbFast, frameFast, frameFastIn;

        std::array<Track, maxTracks> tracks{};
        std::array<Peak, maxPeaks> peaks{};
        int numPeaks = 0;

        std::array<std::atomic<float>, maxSpecBins> spectrum{};

        bool (*nearNotch) (void*, double) = nullptr;
        void* nearNotchCtx = nullptr;
    };
}