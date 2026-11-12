#pragma once
#include <cmath>
#include <algorithm>

namespace jy
{
// Linear ramp smoother, evaluated per sample.
struct LinearSmoother
{
    void prepare (double sampleRate, double rampMs)
    {
        rampSamples = std::max (1, (int) std::lround (sampleRate * rampMs * 0.001));
        current = target;
        step = 0.0f;
        remaining = 0;
    }
    void setImmediate (float v) { current = target = v; remaining = 0; step = 0.0f; }
    void setTarget (float v)
    {
        if (v == target) return;
        target = v;
        remaining = rampSamples;
        step = (target - current) / (float) rampSamples;
    }
    inline float next()
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0) current = target;
        }
        return current;
    }
    void fill (float* dst, int n) { for (int i = 0; i < n; ++i) dst[i] = next(); }
    bool isSmoothing() const { return remaining > 0; }
    float getCurrent() const { return current; }
    float getTarget() const { return target; }

    int rampSamples = 1440;
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0;
};
} // namespace jy
