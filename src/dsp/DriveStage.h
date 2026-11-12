#pragma once
#include "Crossover.h"
#include <cmath>

namespace jy
{
// First-order antiderivative anti-aliased asymmetric tanh. All state and arithmetic in double: the ADAA difference
// quotient cancels catastrophically in float for tiny, close inputs (that turned a -90 dB floor into audible noise).
struct AdaaTanh
{
    double x1 = 0.0; bool adaa = true;
    static constexpr double bias = 0.08;
    static inline const double tanhBias = std::tanh (bias);
    static inline double lncosh (double u) { const double a = std::fabs (u); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; }
    inline float process (float xf)
    {
        const double x = xf;
        double y;
        if (! adaa) y = std::tanh (x + bias) - tanhBias;
        else
        {
            const double dx = x - x1;
            if (std::fabs (dx) < 1e-6) y = std::tanh (0.5 * (x + x1) + bias) - tanhBias;
            else y = ((lncosh (x + bias) - x * tanhBias) - (lncosh (x1 + bias) - x1 * tanhBias)) / dx;
        }
        x1 = x;
        return (float) y;
    }
};

// The tape Drive stage for ONE channel: pre-emphasis (+6 dB high shelf at 3 kHz) -> drive gain -> ADAA tanh ->
// de-emphasis -> head bump (low shelf at 90 Hz, 3 dB at +30 dB drive) -> DC blocker.
// No makeup lives here: the level match is a fixed curve (DriveCalTable.h / the learned curve). Tape, junkyard_calibrate
// and the Learn analysis all run this exact code.
struct DriveStage
{
    void prepare (double fs) { sr = fs; reset(); }
    void reset()
    {
        nl.x1 = 0.0; nl.adaa = adaa;
        pre.setHighShelf (sr, 3000.0, 6.0); pre.reset();
        de.setHighShelf (sr, 3000.0, -6.0); de.reset();
        bumpDb = 0.0f; bump.setLowShelf (sr, 90.0, 0.0); bump.reset();
        dcX = dcY = 0.0f; gain = 1.0f;
    }
    // Pre-gain and head bump. Call on the 16-sample control cadence (the bump coefficients are only rebuilt when they move).
    void setDriveDb (float db)
    {
        gain = std::pow (10.0f, db / 20.0f);
        const float b = 3.0f * db / 30.0f;
        if (std::fabs (b - bumpDb) > 0.01f)
        {
            bumpDb = b; const auto k = bump;
            bump.setLowShelf (sr, 90.0, b); bump.z1 = k.z1; bump.z2 = k.z2;
        }
    }
    inline float process (float x)
    {
        float v = pre.process (x) * gain;
        v = nl.process (v);
        v = de.process (v);
        v = bump.process (v);
        const float o = v - dcX + 0.9995f * dcY; dcX = v; dcY = o; // DC blocker (asymmetric curve)
        return o;
    }

    bool adaa = true;
    double sr = 48000.0;
    AdaaTanh nl;
    Biquad pre, de, bump;
    float dcX = 0.0f, dcY = 0.0f, gain = 1.0f, bumpDb = 0.0f;
};
} // namespace jy
