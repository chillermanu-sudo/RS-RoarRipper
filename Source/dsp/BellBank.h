/*
  RS Roar Ripper - Feedback Suppressor
  Copyright (c) 2026 Roar Sound

  SPDX-License-Identifier: AGPL-3.0-only

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published by
  the Free Software Foundation, version 3 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <https://www.gnu.org/licenses/>.

  This software uses the JUCE framework (https://juce.com), which is also
  licensed under the GNU Affero General Public License version 3.
*/
#pragma once

#include "Constants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>

namespace rsdsp
{
    // Amplitudengang eines RBJ-Peaking-Cuts in dB an Frequenz f.
    inline double bellMagnitudeDb(double slotFreq, double slotDb,
        double widthOct, double f, double fs)
    {
        if (slotDb > -0.01 || f <= 0.0 || fs <= 0.0)
            return 0.0;

        const double sf = std::min(std::max(slotFreq, 20.0), fs * 0.49);
        const double A = std::pow(10.0, slotDb / 40.0);
        const double w0 = 2.0 * kPi * sf / fs;
        const double sw = std::sin(w0);

        if (std::abs(sw) < 1.0e-9)
            return 0.0;

        const double alpha = sw * std::sinh(kLn2 / 2.0 * widthOct * w0 / sw);
        const double a0 = 1.0 + alpha / A;

        const double b0 = (1.0 + alpha * A) / a0;
        const double b1 = (-2.0 * std::cos(w0)) / a0;
        const double b2 = (1.0 - alpha * A) / a0;
        const double a2 = (1.0 - alpha / A) / a0;

        const double w = 2.0 * kPi * f / fs;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = std::polar(1.0, -2.0 * w);

        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + b1 * z1 + a2 * z2;

        return 20.0 * std::log10(std::abs(num / den) + 1.0e-12);
    }

    // Bank aus schmalen Peaking-Cuts. Keine Latenz.
    // Slots mit 0 dB werden uebersprungen -> idle ist bit-transparent.
    class BellBank
    {
    public:
        struct Slot
        {
            bool   active = false;
            bool   fixed = false;
            double freq = 1000.0;

            // Breite pro Slot: fest = breit, dynamisch = schmal.
            // Bleibt beim Umwandeln dynamisch -> fest erhalten, damit es
            // keinen Koeffizientensprung (Knacks) gibt.
            double widthOct = kNotchWidthOct;

            double targetDb = 0.0;
            double currentDb = 0.0;
            double secondsSinceHit = 0.0;

            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

            std::array<double, kMaxChannels> z1{}, z2{};
        };

        struct Published
        {
            std::atomic<float> freq{ 0.0f };
            std::atomic<float> depthDb{ 0.0f };
            std::atomic<float> widthOct{ (float)kNotchWidthOct };
            std::atomic<bool>  active{ false };
            std::atomic<bool>  fixed{ false };
        };

        void prepare(double sampleRate, int numChannels)
        {
            fs = sampleRate;
            channels = std::min(std::max(numChannels, 1), kMaxChannels);
            reset();
        }

        void reset()
        {
            for (auto& s : slots)
                s = Slot{};
            publish();
        }

        void releaseAll() noexcept
        {
            for (auto& s : slots)
            {
                s.active = false;
                s.fixed = false;
                s.targetDb = 0.0;
                s.secondsSinceHit = 0.0;
            }
        }

        std::array<Slot, kTotalSlots>& getSlots()       noexcept { return slots; }
        const std::array<Slot, kTotalSlots>& getSlots() const noexcept { return slots; }

        double sampleRate() const noexcept { return fs; }
        double widthOct()   const noexcept { return kNotchWidthOct; }

        void updateCoefficients(Slot& s) const noexcept
        {
            const double sf = std::min(std::max(s.freq, 20.0), fs * 0.49);
            const double A = std::pow(10.0, s.currentDb / 40.0);
            const double w0 = 2.0 * kPi * sf / fs;
            const double sw = std::sin(w0), cw = std::cos(w0);
            const double alpha = sw * std::sinh(kLn2 / 2.0 * s.widthOct * w0 / sw);
            const double a0 = 1.0 + alpha / A;

            s.b0 = (1.0 + alpha * A) / a0;
            s.b1 = (-2.0 * cw) / a0;
            s.b2 = (1.0 - alpha * A) / a0;
            s.a1 = (-2.0 * cw) / a0;
            s.a2 = (1.0 - alpha / A) / a0;
        }

        void process(float* const* data, int numChannels, int numSamples) noexcept
        {
            const int ch = std::min(numChannels, channels);
            if (ch <= 0 || numSamples <= 0)
                return;

            const double maxRelease = kRampDbPerSec * (double)kSubBlock / fs;
            const double maxAttack = kAttackRampDbPerSec * (double)kSubBlock / fs;

            for (int start = 0; start < numSamples; start += kSubBlock)
            {
                const int len = std::min(kSubBlock, numSamples - start);

                for (auto& s : slots)
                {
                    if (!s.active && s.currentDb == 0.0)
                        continue;

                    const double diff = s.targetDb - s.currentDb;

                    if (diff != 0.0)
                    {
                        // diff < 0: tiefer absenken (Angriff), diff > 0: freigeben
                        const double lim = (diff < 0.0) ? maxAttack : maxRelease;
                        s.currentDb += (diff > lim) ? lim
                            : (diff < -lim) ? -lim
                            : diff;
                        updateCoefficients(s);
                    }

                    if (s.currentDb > -0.01)
                    {
                        if (!s.active)
                        {
                            s.currentDb = 0.0;
                            s.z1.fill(0.0);
                            s.z2.fill(0.0);
                        }
                        continue;
                    }

                    for (int c = 0; c < ch; ++c)
                    {
                        float* x = data[c] + start;
                        double z1 = s.z1[(size_t)c];
                        double z2 = s.z2[(size_t)c];

                        for (int i = 0; i < len; ++i)
                        {
                            const double in = x[i];
                            const double out = s.b0 * in + z1;
                            z1 = s.b1 * in - s.a1 * out + z2;
                            z2 = s.b2 * in - s.a2 * out;
                            x[i] = (float)out;
                        }

                        s.z1[(size_t)c] = z1;
                        s.z2[(size_t)c] = z2;
                    }
                }
            }
        }

        void publish() noexcept
        {
            for (int i = 0; i < kTotalSlots; ++i)
            {
                auto& p = pub[(size_t)i];
                const auto& s = slots[(size_t)i];

                p.freq.store((float)s.freq, std::memory_order_relaxed);
                p.depthDb.store((float)s.currentDb, std::memory_order_relaxed);
                p.widthOct.store((float)s.widthOct, std::memory_order_relaxed);
                p.active.store(s.active, std::memory_order_relaxed);
                p.fixed.store(s.fixed, std::memory_order_relaxed);
            }
        }

        const std::array<Published, kTotalSlots>& published() const noexcept { return pub; }

    private:
        double fs = 48000.0;
        int channels = 2;

        std::array<Slot, kTotalSlots> slots{};
        std::array<Published, kTotalSlots> pub{};
    };
}