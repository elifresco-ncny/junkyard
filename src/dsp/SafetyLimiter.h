#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace jy
{
// Catches runaway bursts and NaNs only: transparent up to 0 dBFS, then smoothly saturates towards +6 dBFS.
// NaN/Inf are replaced by 0 and reported so the caller can reset its state.
struct SafetyLimiter
{
    static constexpr float ceiling = 1.9952623f; // +6 dBFS
    static constexpr float knee = 1.0f;          // 0 dBFS

    static inline bool isBad (float x)
    {
        uint32_t u; std::memcpy (&u, &x, 4);
        return (u & 0x7f800000u) == 0x7f800000u; // exp all ones: Inf or NaN
    }
    // returns true if x was bad
    inline bool process (float& x) const
    {
        if (isBad (x)) { x = 0.0f; return true; }
        const float a = std::fabs (x);
        if (a > knee)
        {
            const float r = ceiling - knee;
            const float y = knee + r * std::tanh ((a - knee) / r);
            x = std::copysign (y, x);
        }
        return false;
    }
};
} // namespace jy
