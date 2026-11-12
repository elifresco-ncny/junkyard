#pragma once
#include <cstdint>
#include <cmath>

namespace jy
{
// xorshift128+, per-instance and seedable. No allocation, no locks.
class Random
{
public:
    explicit Random (uint64_t seed = 0x9E3779B97F4A7C15ull) { setSeed (seed); }
    void setSeed (uint64_t seed)
    {
        // splitmix64 to spread the seed
        auto sm = [&seed]() { uint64_t z = (seed += 0x9E3779B97F4A7C15ull);
                              z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                              z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                              return z ^ (z >> 31); };
        s0 = sm(); s1 = sm();
        if (s0 == 0 && s1 == 0) s1 = 1;
    }
    inline uint64_t nextU64()
    {
        uint64_t x = s0; const uint64_t y = s1;
        s0 = y; x ^= x << 23;
        s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
        return s1 + y;
    }
    // [0,1)
    inline float uniform() { return (float) (nextU64() >> 40) * (1.0f / 16777216.0f); }
    // [-1,1)
    inline float bipolar() { return uniform() * 2.0f - 1.0f; }
    inline float range (float lo, float hi) { return lo + (hi - lo) * uniform(); }
    inline int index (int n) { return (int) (nextU64() % (uint64_t) n); }
    inline bool chance (float p) { return uniform() < p; }
    // approx gaussian (sum of 4 uniforms), unit-ish variance
    inline float gauss() { return (uniform() + uniform() + uniform() + uniform() - 2.0f) * 1.7320508f; }
private:
    uint64_t s0 = 1, s1 = 2;
};
} // namespace jy
