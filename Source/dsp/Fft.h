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
#include <cmath>
#include <utility>
#include <vector>

namespace rsdsp
{
    class Fft
    {
    public:
        Fft() = default;
        explicit Fft(int size) { prepare(size); }

        void prepare(int size)
        {
            n = std::max(2, size);

            bits = 0;
            while ((1 << bits) < n)
                ++bits;

            rev.assign((size_t)n, 0);

            for (int i = 0; i < n; ++i)
            {
                int r = 0;

                for (int b = 0; b < bits; ++b)
                    if (i & (1 << b))
                        r |= 1 << (bits - 1 - b);

                rev[(size_t)i] = r;
            }

            twRe.assign((size_t)(n / 2), 0.0f);
            twIm.assign((size_t)(n / 2), 0.0f);

            for (int i = 0; i < n / 2; ++i)
            {
                const double a = -2.0 * kPi * (double)i / (double)n;
                twRe[(size_t)i] = (float)std::cos(a);
                twIm[(size_t)i] = (float)std::sin(a);
            }
        }

        int size() const noexcept { return n; }

        // data: interleaved re/im, Laenge 2*n
        void forward(float* data) const noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const int j = rev[(size_t)i];

                if (i < j)
                {
                    std::swap(data[2 * i], data[2 * j]);
                    std::swap(data[2 * i + 1], data[2 * j + 1]);
                }
            }

            for (int len = 2; len <= n; len <<= 1)
            {
                const int half = len >> 1;
                const int step = n / len;

                for (int start = 0; start < n; start += len)
                {
                    for (int k = 0; k < half; ++k)
                    {
                        const float wr = twRe[(size_t)(k * step)];
                        const float wi = twIm[(size_t)(k * step)];

                        const int e = 2 * (start + k);
                        const int o = e + len;

                        const float er = data[e], ei = data[e + 1];
                        const float orr = data[o], oi = data[o + 1];

                        const float tr = wr * orr - wi * oi;
                        const float ti = wr * oi + wi * orr;

                        data[e] = er + tr;
                        data[e + 1] = ei + ti;
                        data[o] = er - tr;
                        data[o + 1] = ei - ti;
                    }
                }
            }
        }

    private:
        int n = 0;
        int bits = 0;
        std::vector<int> rev;
        std::vector<float> twRe, twIm;
    };

    // FFT fuer reelle Eingangsdaten (halber Rechenaufwand).
    // Nutzt eine komplexe FFT der Groesse n/2 (gerade/ungerade Samples als Re/Im)
    // und trennt das Ergebnis anschliessend wieder auf. Das Ergebnis ist
    // mathematisch identisch zur komplexen FFT mit Imaginaerteil 0.
    class RealFft
    {
    public:
        RealFft() = default;
        explicit RealFft(int size) { prepare(size); }

        void prepare(int size)
        {
            n = std::max(4, size);
            half.prepare(n / 2);

            work.assign((size_t)n, 0.0f);

            twRe.assign((size_t)(n / 2 + 1), 0.0f);
            twIm.assign((size_t)(n / 2 + 1), 0.0f);

            for (int k = 0; k <= n / 2; ++k)
            {
                const double a = -2.0 * kPi * (double)k / (double)n;
                twRe[(size_t)k] = (float)std::cos(a);
                twIm[(size_t)k] = (float)std::sin(a);
            }
        }

        int size() const noexcept { return n; }

        // in:  n reelle Samples
        // out: interleaved re/im der Bins 0 .. n/2, Laenge n + 2
        void forward(const float* in, float* out) noexcept
        {
            const int N = n / 2;

            std::copy(in, in + n, work.begin());
            half.forward(work.data());

            const float z0r = work[0], z0i = work[1];

            out[0] = z0r + z0i;
            out[1] = 0.0f;
            out[2 * N] = z0r - z0i;
            out[2 * N + 1] = 0.0f;

            for (int k = 1; k < N; ++k)
            {
                const float zr = work[(size_t)(2 * k)];
                const float zi = work[(size_t)(2 * k + 1)];
                const float cr = work[(size_t)(2 * (N - k))];
                const float ci = -work[(size_t)(2 * (N - k) + 1)];

                // E = (Z[k] + conj(Z[N-k])) / 2,  O = (Z[k] - conj(Z[N-k])) / (2j)
                const float er = 0.5f * (zr + cr);
                const float ei = 0.5f * (zi + ci);
                const float orr = 0.5f * (zi - ci);
                const float oi = -0.5f * (zr - cr);

                const float wr = twRe[(size_t)k];
                const float wi = twIm[(size_t)k];

                out[2 * k] = er + wr * orr - wi * oi;
                out[2 * k + 1] = ei + wr * oi + wi * orr;
            }
        }

    private:
        int n = 0;
        Fft half;
        std::vector<float> work, twRe, twIm;
    };
}