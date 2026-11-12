#pragma once
#include "Random.h"
#include <cmath>
#include <algorithm>

namespace jy
{
// Smoothed random modulator ("value noise" with cubic interpolation). Output in [-1, 1].
class ValueNoise
{
public:
    void prepare (double sampleRate, uint64_t seed) { sr = sampleRate; rng.setSeed (seed); a = rng.bipolar(); b = rng.bipolar(); c = rng.bipolar(); d = rng.bipolar(); t = 0; }
    inline float next (float freqHz, int steps = 1)
    {
        t += freqHz * (float) steps / (float) sr;
        while (t >= 1.0f) { t -= 1.0f; a = b; b = c; c = d; d = rng.bipolar(); }
        // Catmull-Rom through a,b,c,d at positions -1,0,1,2 -> value between b and c
        const float c1 = 0.5f * (c - a), c2 = a - 2.5f * b + 2.0f * c - 0.5f * d, c3 = 0.5f * (d - a) + 1.5f * (b - c);
        const float y = ((c3 * t + c2) * t + c1) * t + b;
        return y > 1.0f ? 1.0f : (y < -1.0f ? -1.0f : y);
    }
private:
    double sr = 48000.0; Random rng; float a = 0, b = 0, c = 0, d = 0, t = 0;
};
} // namespace jy
