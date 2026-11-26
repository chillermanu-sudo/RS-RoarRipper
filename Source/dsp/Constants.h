#pragma once

#include <algorithm>
#include <cmath>

namespace rsdsp
{
    inline constexpr double kPi = 3.14159265358979323846;
    inline constexpr double kLn2 = 0.6931471805599453;

    // Analysefenster: ~0.16 s bei jeder Sample Rate
    inline constexpr double kWindowSeconds = 0.16;
    inline constexpr int    kMinFftSize = 2048;
    inline constexpr int    kMaxFftSize = 32768;
    inline constexpr int    kFastFftSize = 2048;

    // Auswerteband
    inline constexpr double kBandLowHz = 40.0;
    inline constexpr double kBandHighHz = 20000.0;

    // ---- Filterbreite ---------------------------------------------------
    // Feste Notches: breit und robust, Q ~14.4
    inline constexpr double kNotchWidthOct = 0.1;

    // Dynamische Notches: schmal, Q 20
    inline constexpr double kDynNotchQ = 20.0;

    // Untergrenze der absoluten Bandbreite dynamischer Notches.
    // Ohne sie waere ein Notch bei 100 Hz nur 5 Hz breit - schmaler als
    // ein FFT-Bin (5.86 Hz bei 48 kHz / 8192). Der Detektor koennte ihn
    // dann nicht mehr zuverlaessig auf das Feedback legen.
    inline constexpr double kDynMinBandHz = 12.0;

    // Q -> Oktavbandbreite (RBJ-Beziehung)
    inline double widthOctForQ(double q) noexcept
    {
        return 2.0 / kLn2 * std::asinh(1.0 / (2.0 * std::max(0.5, q)));
    }

    // Oktavbandbreite -> Q (fuer Anzeige / Debug)
    inline double qForWidthOct(double bw) noexcept
    {
        return 1.0 / (2.0 * std::sinh(kLn2 * 0.5 * std::max(1.0e-4, bw)));
    }

    // Dynamische Breite: Q 20, aber nie schmaler als kDynMinBandHz
    inline double dynNotchWidthOct(double freqHz) noexcept
    {
        const double bwQ = widthOctForQ(kDynNotchQ);
        const double bwMin = kDynMinBandHz / (std::max(40.0, freqHz) * kLn2);
        return std::max(bwQ, bwMin);
    }

    inline constexpr double kMaxCutDb = -42.0;   // Obergrenze gesamt pro Frequenz

    // ---- Slot-Budget: 6 fest + 18 dynamisch ------------------------------
    inline constexpr int kFixedSlots = 6;
    inline constexpr int kDynamicSlots = 18;
    inline constexpr int kTotalSlots = kFixedSlots + kDynamicSlots;

    inline constexpr int    kMaxChannels = 8;
    inline constexpr double kRampDbPerSec = 300.0;   // Freigeben
    inline constexpr double kAttackRampDbPerSec = 600.0;   // Absenken
    inline constexpr int    kSubBlock = 32;
}