#pragma once

#include <algorithm>
#include <cmath>

namespace rsdsp
{
    inline constexpr double kPi = 3.14159265358979323846;
    inline constexpr double kLn2 = 0.6931471805599453;

    inline constexpr double kWindowSeconds = 0.16;
    inline constexpr int    kMinFftSize = 2048;
    inline constexpr int    kMaxFftSize = 32768;
    inline constexpr int    kFastFftSize = 2048;

    inline constexpr double kBandLowHz = 40.0;
    inline constexpr double kBandHighHz = 20000.0;
    // Schmalband-Gate: maximale -3-dB-Breite eines Howl-Peaks in Bins.
    // Reiner Sinus/Howl mit Hann-Fenster: ~1.5-2 Bins.
    // Sprachformanten: 10-25 Bins, Gerausch/Transienten: deutlich mehr.
    inline constexpr float kMaxHowlWidthBins = 4.0f;

    // Feste Notches: Q 18 (ca. 0.0795 Oktaven)
    inline constexpr double kNotchWidthOct = 0.0795;

    // Dynamische Notches: Q 20
    inline constexpr double kDynNotchQ = 20.0;
    inline constexpr double kDynMinBandHz = 12.0;

    inline double widthOctForQ(double q) noexcept
    {
        return 2.0 / kLn2 * std::asinh(1.0 / (2.0 * std::max(0.5, q)));
    }

    inline double qForWidthOct(double bw) noexcept
    {
        return 1.0 / (2.0 * std::sinh(kLn2 * 0.5 * std::max(1.0e-4, bw)));
    }

    inline double dynNotchWidthOct(double freqHz) noexcept
    {
        const double bwQ = widthOctForQ(kDynNotchQ);
        const double bwMin = kDynMinBandHz / (std::max(40.0, freqHz) * kLn2);
        return std::max(bwQ, bwMin);
    }

    inline constexpr double kMaxCutDb = -42.0;

    inline constexpr int kFixedSlots = 6;
    inline constexpr int kDynamicSlots = 18;
    inline constexpr int kTotalSlots = kFixedSlots + kDynamicSlots;

    inline constexpr int    kMaxChannels = 8;
    inline constexpr double kRampDbPerSec = 300.0;
    inline constexpr double kAttackRampDbPerSec = 600.0;
    inline constexpr int    kSubBlock = 32;
}