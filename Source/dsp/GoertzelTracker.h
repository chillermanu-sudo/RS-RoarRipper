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
#include <cmath>

namespace rsdsp
{
    // Verfolgt bis zu kTotalSlots Frequenzen mit Goertzel-Filtern.
    // Ein Goertzel-Filter ist eine Einzelbin-DFT: extrem guenstig,
    // liefert aber nur den Pegel an genau einer Frequenz.
    //
    // Zweck: Zwischen zwei FFT-Frames sample-genau pruefen, ob an der
    // Notch-Frequenz im EINGANG noch Energie ist. Damit wird
    // secondsSinceHit zurueckgesetzt, ohne auf den naechsten Frame zu warten.
    class GoertzelTracker
    {
    public:
        // Messfenster: 2048 Samples = ~43 ms bei 48 kHz.
        // Hann-gefenstert: -3 dB-Breite ~34 Hz, Nebenkeulen -31 dB.
        // Ein Rechteckfenster mit 1024 Samples waere 47 Hz breit bei nur
        // -13 dB Nebenkeulen - das haelt schmale Q20-Notches zu lange,
        // weil Nachbarenergie (Sprache) mitgemessen wird.
        static constexpr int windowSamples = 2048;

        struct Voice
        {
            bool   active = false;
            double freq = 0.0;
            double coeff = 0.0;
            double s0 = 0.0, s1 = 0.0;
            double levelDb = -120.0;
            int    samplesAccum = 0;
        };

        void prepare(double sampleRate)
        {
            fs = sampleRate;

            // periodisches Hann-Fenster
            for (int i = 0; i < windowSamples; ++i)
                hann[(size_t)i] = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)i / (double)windowSamples);

            for (auto& v : voices)
                v = Voice{};
        }

        void setFrequency(int slot, double freq)
        {
            if (slot < 0 || slot >= kTotalSlots)
                return;

            auto& v = voices[(size_t)slot];
            v.active = true;
            v.freq = freq;
            v.coeff = 2.0 * std::cos(2.0 * kPi * freq / fs);
            v.s0 = v.s1 = 0.0;
            v.levelDb = -120.0;
            v.samplesAccum = 0;
        }

        void updateFrequency(int slot, double freq)
        {
            if (slot < 0 || slot >= kTotalSlots)
                return;

            auto& v = voices[(size_t)slot];

            if (!v.active)
            {
                setFrequency(slot, freq);
                return;
            }

            // Kleine Drift: nur den Koeffizienten nachziehen, Messung weiterlaufen
            // lassen. Die Engine zieht die Notch-Frequenz staendig mit Faktor 0.3
            // nach - wuerde man hier jedes Mal zuruecksetzen, lieferte der Goertzel
            // nie einen fertigen Pegel.
            const double resetThreshold = std::max(2.0, fs / (double)windowSamples * 0.5);

            if (std::abs(freq - v.freq) > resetThreshold)
            {
                setFrequency(slot, freq);
                return;
            }

            v.freq = freq;
            v.coeff = 2.0 * std::cos(2.0 * kPi * freq / fs);
        }

        void deactivate(int slot)
        {
            if (slot < 0 || slot >= kTotalSlots)
                return;
            voices[(size_t)slot] = Voice{};
        }

        void deactivateAll()
        {
            for (auto& v : voices)
                v = Voice{};
        }

        // Mono-Signal verarbeiten. Pegel wird alle windowSamples Samples aktualisiert.
        // Pegel in dB als Spitzenamplitude - gleiche Skala wie die FFT des HowlDetector.
        void process(const float* mono, int numSamples) noexcept
        {
            for (auto& v : voices)
            {
                if (!v.active)
                    continue;

                int pos = 0;
                while (pos < numSamples)
                {
                    // bis zum Fensterende arbeiten, dann auswerten
                    const int room = windowSamples - v.samplesAccum;
                    const int len = std::min(room, numSamples - pos);
                    const int base = v.samplesAccum;

                    double s0 = v.s0, s1 = v.s1;

                    for (int i = 0; i < len; ++i)
                    {
                        const double x = (double)mono[pos + i] * hann[(size_t)(base + i)];
                        const double s2 = s1;
                        s1 = s0;
                        s0 = x + v.coeff * s1 - s2;
                    }

                    v.s0 = s0;
                    v.s1 = s1;
                    v.samplesAccum += len;
                    pos += len;

                    if (v.samplesAccum >= windowSamples)
                    {
                        const double power = v.s0 * v.s0 + v.s1 * v.s1
                            - v.coeff * v.s0 * v.s1;

                        // Hann: Summe der Fensterwerte = N/2, daher |X| = A*N/4.
                        // scale 4/N liefert dieselbe Amplitudenskala wie die
                        // Hann-FFT im HowlDetector (dort scale 4/n).
                        const double scale = 4.0 / (double)windowSamples;
                        v.levelDb = 10.0 * std::log10(std::max(0.0, power) * scale * scale + 1.0e-20);

                        v.s0 = v.s1 = 0.0;
                        v.samplesAccum = 0;
                    }
                }
            }
        }

        float getLevelDb(int slot) const noexcept
        {
            if (slot < 0 || slot >= kTotalSlots)
                return -120.0f;
            return (float)voices[(size_t)slot].levelDb;
        }

        bool isActive(int slot) const noexcept
        {
            return slot >= 0 && slot < kTotalSlots && voices[(size_t)slot].active;
        }

    private:
        double fs = 48000.0;
        std::array<double, windowSamples> hann{};
        std::array<Voice, kTotalSlots> voices{};
    };
}