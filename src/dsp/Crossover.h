#pragma once
#include <cmath>

namespace jy
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x)
    {
        const double in = x;
        const double y = b0 * in + z1;
        z1 = b1 * in - a1 * y + z2;
        z2 = b2 * in - a2 * y;
        return (float) y;
    }
    void setLowpass (double fs, double f, double q = 0.70710678)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / fs, c = std::cos (w), s = std::sin (w), al = s / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void setHighpass (double fs, double f, double q = 0.70710678)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / fs, c = std::cos (w), s = std::sin (w), al = s / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    // RBJ shelves / peaking (gain in dB)
    void setLowShelf (double fs, double f, double gainDb, double q = 0.70710678)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * 3.14159265358979323846 * f / fs;
        const double c = std::cos (w), s = std::sin (w), al = s / (2 * q), sq = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) + (A - 1) * c + sq;
        b0 = A * ((A + 1) - (A - 1) * c + sq) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
        b2 = A * ((A + 1) - (A - 1) * c - sq) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
        a2 = ((A + 1) + (A - 1) * c - sq) / a0;
    }
    // RBJ high shelf with the shelf-slope parameter S (S = 1: steepest monotonic shelf)
    void setHighShelfSlope (double fs, double f, double gainDb, double S)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * 3.14159265358979323846 * f / fs;
        const double c = std::cos (w), s = std::sin (w), al = s / 2 * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), sq = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * c + sq;
        b0 = A * ((A + 1) + (A - 1) * c + sq) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        b2 = A * ((A + 1) + (A - 1) * c - sq) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        a2 = ((A + 1) - (A - 1) * c - sq) / a0;
    }
    // RBJ band-pass, constant 0 dB peak gain
    void setBandpass (double fs, double f, double q)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / fs, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = al / a0; b1 = 0.0; b2 = -al / a0; a1 = -2.0 * std::cos (w) / a0; a2 = (1.0 - al) / a0;
    }
    void setHighShelf (double fs, double f, double gainDb, double q = 0.70710678)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * 3.14159265358979323846 * f / fs;
        const double c = std::cos (w), s = std::sin (w), al = s / (2 * q), sq = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * c + sq;
        b0 = A * ((A + 1) + (A - 1) * c + sq) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        b2 = A * ((A + 1) + (A - 1) * c - sq) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        a2 = ((A + 1) - (A - 1) * c - sq) / a0;
    }
};

// Linkwitz-Riley 4th order (two cascaded Butterworth biquads per branch), one channel.
struct LR4
{
    Biquad lp1, lp2, hp1, hp2;
    void setFrequency (double fs, double f)
    {
        f = std::fmin (std::fmax (f, 10.0), fs * 0.45);
        lp1.setLowpass (fs, f); lp2.setLowpass (fs, f);
        hp1.setHighpass (fs, f); hp2.setHighpass (fs, f);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void process (float x, float& low, float& high)
    {
        low = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
    }
};
} // namespace jy
