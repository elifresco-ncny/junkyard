#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace jy
{
// Circular delay line. Allocate in prepare() only.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

    // Write x, return the sample written `delay` samples ago (delay may be 0).
    inline float process (float x, int delay)
    {
        buf[(size_t) (w & mask)] = x;
        const float y = buf[(size_t) ((w - delay) & mask)];
        ++w;
        return y;
    }
    inline void push (float x) { buf[(size_t) (w & mask)] = x; ++w; }
    // delay counted from the most recently pushed sample (0 = newest)
    inline float read (int delay) const { return buf[(size_t) ((w - 1 - delay) & mask)]; }
    // Catmull-Rom cubic read, fractional delay >= 1 relative to newest sample
    inline float readCubic (float delay) const
    {
        const int i = (int) delay;
        const float f = delay - (float) i;
        const float ym1 = read (i - 1), y0 = read (i), y1 = read (i + 1), y2 = read (i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
private:
    std::vector<float> buf;
    int mask = 15;
    int w = 0;
};
} // namespace jy
