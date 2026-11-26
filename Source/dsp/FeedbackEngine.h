#pragma once

#include "BellBank.h"
#include "GoertzelTracker.h"
#include "HowlDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace rsdsp
{
    class FeedbackEngine
    {
    public:
        enum class Mode { live, setup, standalone };

        struct Settings
        {
            double sensitivity = 0.8;
            double holdSeconds = 0.150;
            double releaseSeconds = 1.0;
            int    hopDivisor = 64;
            Mode   mode = Mode::live;
        };

        struct StoredNotch
        {
            float freq = 0.0f;
            float depthDb = 0.0f;
        };

        void prepare(double sampleRate, int maxBlock, int numChannels)
        {
            fs = sampleRate;

            detector.prepare(fs);
            detector.setNotchQuery(&FeedbackEngine::nearNotchThunk, this);

            bank.prepare(fs, numChannels);
            goertzel.prepare(fs);
            triggerDb.fill(-200.0);

            mono.assign((size_t)std::max(1, maxBlock), 0.0f);
            sinceDeepen.fill(1.0);

            applySettings();
        }

        void reset()
        {
            bank.releaseAll();
            detector.reset();
            sinceDeepen.fill(1.0);
            goertzel.deactivateAll();
            triggerDb.fill(-200.0);
        }

        void resetDetector() noexcept { detector.reset(); }

        void setMeasuring(bool m) noexcept
        {
            measuring = m;
            detector.setMeasurementMode(measuring && settings.mode == Mode::setup);
        }

        bool isMeasuring() const noexcept { return measuring; }

        void releaseFixed() noexcept
        {
            for (auto& s : bank.getSlots())
                if (s.fixed)
                {
                    s.active = false;
                    s.fixed = false;
                    s.targetDb = 0.0;
                }
        }

        void releaseDynamic() noexcept
        {
            for (auto& s : bank.getSlots())
                if (!s.fixed)
                {
                    s.active = false;
                    s.targetDb = 0.0;
                }
        }

        void setSettings(const Settings& s) noexcept { settings = s; applySettings(); }
        const Settings& getSettings() const noexcept { return settings; }

        void process(float* const* data, int numChannelsIn, int numSamples) noexcept
        {
            const int numChannels = std::min(numChannelsIn, kMaxChannels);

            if (numSamples > (int)mono.size())
            {
                for (int pos = 0; pos < numSamples; pos += (int)mono.size())
                {
                    const int len = std::min((int)mono.size(), numSamples - pos);
                    float* ptrs[kMaxChannels];
                    for (int c = 0; c < numChannels; ++c)
                        ptrs[c] = data[c] + pos;
                    process(ptrs, numChannels, len);
                }
                return;
            }

            // Goertzel: Pegel an den Frequenzen der dynamischen Notches im
            // Eingangssignal (VOR der Bank), sonst saehe er nur den abgesenkten Pegel.
            if (syncGoertzel())
            {
                downmix(data, numChannels, numSamples);
                goertzel.process(mono.data(), numSamples);
            }

            bank.process(data, numChannels, numSamples);
            downmix(data, numChannels, numSamples);

            HowlDetector::Event events[HowlDetector::maxEvents];
            const int ne = detector.push(mono.data(), numSamples, events);

            for (int e = 0; e < ne; ++e)
                handleEvent(events[e]);

            updateRelease((double)numSamples / fs);
            bank.publish();
        }

        void fixAllCurrent() noexcept
        {
            int fixedCount = countFixed();

            for (auto& s : bank.getSlots())
            {
                if (fixedCount >= kFixedSlots)
                    break;

                if (s.active && !s.fixed && (s.currentDb < -0.5 || s.targetDb < -0.5))
                {
                    // Breite bleibt erhalten -> kein Koeffizientensprung
                    s.fixed = true;
                    ++fixedCount;
                }
            }

            bank.publish();
        }

        void restoreFixed(const StoredNotch* in, int count) noexcept
        {
            auto& slots = bank.getSlots();
            int placed = countFixed();

            for (int i = 0; i < count && placed < kFixedSlots; ++i)
            {
                if (in[i].freq < 20.0f || in[i].depthDb > -0.5f)
                    continue;

                int target = -1;
                for (int k = 0; k < kTotalSlots; ++k)
                    if (!slots[(size_t)k].active && slots[(size_t)k].currentDb == 0.0)
                    {
                        target = k;
                        break;
                    }

                if (target < 0)
                    break;

                auto& s = slots[(size_t)target];
                s = BellBank::Slot{};
                s.active = true;
                s.fixed = true;
                s.freq = in[i].freq;
                s.widthOct = kNotchWidthOct;
                s.targetDb = std::max((double)in[i].depthDb, kMaxCutDb);
                s.currentDb = 0.0;
                s.secondsSinceHit = 0.0;

                bank.updateCoefficients(s);
                ++placed;
            }

            bank.publish();
        }

        BellBank& getBank()       noexcept { return bank; }
        const BellBank& getBank() const noexcept { return bank; }

        HowlDetector& getDetector()       noexcept { return detector; }
        const HowlDetector& getDetector() const noexcept { return detector; }

        int countFixed() const noexcept
        {
            int n = 0;
            for (const auto& s : bank.getSlots())
                n += (s.active && s.fixed) ? 1 : 0;
            return n;
        }

        int countDynamic() const noexcept
        {
            int n = 0;
            for (const auto& s : bank.getSlots())
                n += (s.active && !s.fixed) ? 1 : 0;
            return n;
        }

    private:
        static constexpr double goertzelHoldWindowDb = 12.0;
        static constexpr double growthRefDbPerSec = 10.0;
        static constexpr double growthExtraPerDbPerSec = 0.1;
        static constexpr double growthMaxExtraDb = 12.0;

        static double growthExtraCut(const HowlDetector::Event& ev) noexcept
        {
            return std::min(growthMaxExtraDb,
                std::max(0.0, (ev.growthDbPerSec - growthRefDbPerSec) * growthExtraPerDbPerSec));
        }

        void downmix(float* const* data, int numChannels, int numSamples) noexcept
        {
            const float g = numChannels > 0 ? 1.0f / (float)numChannels : 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                float sum = 0.0f;
                for (int c = 0; c < numChannels; ++c)
                    sum += data[c][i];
                mono[(size_t)i] = sum * g;
            }
        }

        bool syncGoertzel() noexcept
        {
            bool any = false;
            const auto& slots = bank.getSlots();

            for (int i = 0; i < kTotalSlots; ++i)
            {
                const auto& s = slots[(size_t)i];
                if (s.active && !s.fixed)
                {
                    goertzel.updateFrequency(i, s.freq);
                    any = true;
                }
                else if (goertzel.isActive(i))
                {
                    goertzel.deactivate(i);
                }
            }
            return any;
        }

        bool goertzelHolds(int slot) const noexcept
        {
            return goertzel.isActive(slot)
                && goertzel.getLevelDb(slot) >= triggerDb[(size_t)slot] - goertzelHoldWindowDb;
        }

        static bool nearNotchThunk(void* ctx, double f)
        {
            return static_cast<FeedbackEngine*> (ctx)->hasNotchNear(f);
        }

        // Umgebungsradius, in dem ein vorhandener Notch als "derselbe" gilt.
        // Haengt jetzt von der jeweiligen Slot-Breite ab.
        static double matchOctFor(double widthOct) noexcept
        {
            return std::max(widthOct * 0.5, 1.0 / 24.0);
        }

        bool hasNotchNear(double f) const noexcept
        {
            for (const auto& s : bank.getSlots())
            {
                if (!s.active || s.freq <= 0.0)
                    continue;
                if (std::abs(std::log2(f / s.freq)) < matchOctFor(s.widthOct))
                    return true;
            }
            return false;
        }

        double headroomDb(double f) const noexcept
        {
            double fixedSum = 0.0;
            for (const auto& s : bank.getSlots())
                if (s.active && s.fixed && s.currentDb < -0.01)
                    fixedSum += bellMagnitudeDb(s.freq, s.currentDb, s.widthOct, f, fs);

            return std::max(kMaxCutDb, kMaxCutDb - fixedSum);
        }

        int findDynamicSlot(double f) const noexcept
        {
            int best = -1;
            double bestDist = 1.0e9;
            const auto& slots = bank.getSlots();

            for (int i = 0; i < kTotalSlots; ++i)
            {
                const auto& s = slots[(size_t)i];
                if (!s.active || s.fixed)
                    continue;

                const double d = std::abs(std::log2(f / s.freq));
                if (d < matchOctFor(s.widthOct) && d < bestDist)
                {
                    bestDist = d;
                    best = i;
                }
            }
            return best;
        }

        int findFixedSlot(double f) const noexcept
        {
            int best = -1;
            double bestDist = 1.0e9;
            const auto& slots = bank.getSlots();

            for (int i = 0; i < kTotalSlots; ++i)
            {
                const auto& s = slots[(size_t)i];
                if (!s.active || !s.fixed)
                    continue;

                const double d = std::abs(std::log2(f / s.freq));
                if (d < matchOctFor(s.widthOct) && d < bestDist)
                {
                    bestDist = d;
                    best = i;
                }
            }
            return best;
        }

        int findFreeSlot() const noexcept
        {
            const auto& slots = bank.getSlots();
            for (int i = 0; i < kTotalSlots; ++i)
                if (!slots[(size_t)i].active && slots[(size_t)i].currentDb == 0.0)
                    return i;
            return -1;
        }

        int shallowestDynamicSlot() const noexcept
        {
            const auto& slots = bank.getSlots();
            int victim = -1;
            double shallowest = -1.0e9;

            for (int i = 0; i < kTotalSlots; ++i)
            {
                const auto& s = slots[(size_t)i];
                if (s.active && !s.fixed && s.targetDb > shallowest)
                {
                    shallowest = s.targetDb;
                    victim = i;
                }
            }
            return victim;
        }

        void handleEvent(const HowlDetector::Event& ev) noexcept
        {
            // Einpfeifen: ohne laufende Messung keine neuen oder veraenderten Notches
            if (settings.mode == Mode::setup && !measuring)
                return;

            auto& slots = bank.getSlots();

            // 1) Besteht bereits ein dynamischer Notch in der Naehe?
            const int dyn = findDynamicSlot(ev.freq);
            if (dyn >= 0)
            {
                auto& s = slots[(size_t)dyn];
                s.secondsSinceHit = 0.0;
                triggerDb[(size_t)dyn] = std::max(triggerDb[(size_t)dyn], ev.levelDb - s.currentDb);

                if (sinceDeepen[(size_t)dyn] >= 0.04)
                {
                    const double headroom = headroomDb(s.freq);
                    const double step = 3.0 + 3.0 * settings.sensitivity;
                    s.targetDb = std::max(s.targetDb - step, headroom);
                    sinceDeepen[(size_t)dyn] = 0.0;
                }

                s.freq = s.freq * std::pow(ev.freq / s.freq, 0.3);
                s.widthOct = dynNotchWidthOct(s.freq);   // Breite mitziehen
                bank.updateCoefficients(s);
                return;
            }

            // 2) Besteht bereits ein FESTER Notch in der Naehe?
            const int fix = findFixedSlot(ev.freq);
            if (fix >= 0)
            {
                auto& s = slots[(size_t)fix];
                s.secondsSinceHit = 0.0;

                if (sinceDeepen[(size_t)fix] >= 0.04)
                {
                    const double step = 3.0 + 3.0 * settings.sensitivity;
                    s.targetDb = std::max(s.targetDb - step, kMaxCutDb);
                    sinceDeepen[(size_t)fix] = 0.0;
                }
                return;
            }

            // 3) Neuen Slot anlegen
            const bool wantFixed = (settings.mode == Mode::setup);
            const bool makeFixed = wantFixed && countFixed() < kFixedSlots;

            if (!makeFixed && countDynamic() >= kDynamicSlots)
            {
                const int victim = shallowestDynamicSlot();
                if (victim < 0)
                    return;
                placeAt(victim, ev, false);
                return;
            }

            const int target = findFreeSlot();
            if (target < 0)
                return;

            placeAt(target, ev, makeFixed);
        }

        void placeAt(int idx, const HowlDetector::Event& ev, bool asFixed) noexcept
        {
            const double headroom = headroomDb(ev.freq);
            if (headroom > -0.5)
                return;

            auto& s = bank.getSlots()[(size_t)idx];
            const bool reuse = s.currentDb < -0.01;
            const double firstCut = 8.0 + 10.0 * settings.sensitivity + growthExtraCut(ev);

            s.active = true;
            s.fixed = asFixed;
            s.freq = ev.freq;
            s.widthOct = asFixed ? kNotchWidthOct : dynNotchWidthOct(ev.freq);
            s.targetDb = std::max(-firstCut, headroom);
            s.secondsSinceHit = 0.0;

            if (!reuse)
            {
                s.currentDb = 0.0;
                s.z1.fill(0.0);
                s.z2.fill(0.0);
            }

            bank.updateCoefficients(s);
            sinceDeepen[(size_t)idx] = 0.0;
            triggerDb[(size_t)idx] = ev.levelDb - s.currentDb;
        }

        void updateRelease(double dt) noexcept
        {
            const double rate = (-kMaxCutDb) / std::max(0.001, settings.releaseSeconds);

            for (int i = 0; i < kTotalSlots; ++i)
            {
                auto& s = bank.getSlots()[(size_t)i];
                sinceDeepen[(size_t)i] += dt;

                if (!s.active || s.fixed)
                    continue;

                if (goertzelHolds(i))
                {
                    s.secondsSinceHit = 0.0;
                    continue;
                }

                s.secondsSinceHit += dt;

                if (s.secondsSinceHit > settings.holdSeconds)
                {
                    s.targetDb = std::min(0.0, s.targetDb + rate * dt);
                    if (s.targetDb > -0.5)
                    {
                        s.targetDb = 0.0;
                        s.active = false;
                    }
                }
            }
        }

        void applySettings() noexcept
        {
            detector.setSensitivity(settings.sensitivity);
            detector.setHopDivisor(settings.hopDivisor);
            detector.setMeasurementMode(measuring && settings.mode == Mode::setup);
        }

        double fs = 48000.0;
        Settings settings;
        bool measuring = false;

        BellBank bank;
        HowlDetector detector;
        GoertzelTracker goertzel;

        std::vector<float> mono;
        std::array<double, kTotalSlots> sinceDeepen{};
        std::array<double, kTotalSlots> triggerDb{};
    };
}