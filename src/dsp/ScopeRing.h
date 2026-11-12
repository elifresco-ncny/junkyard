#pragma once
#include <atomic>
#include <array>
#include <cstdint>

namespace jy
{
constexpr int kScopeBins = 64;

// One codec frame summarised for the Damage Scope: 64 log-spaced bins.
struct ScopeFrame
{
    uint8_t orig[kScopeBins] {};  // original energy, 0..255 = -100..0 dB
    uint8_t kept[kScopeBins] {};  // energy after the codec
    uint8_t glitch = 0;           // 1 = this frame was replaced/concealed by the glitch engine
};

// Lock-free single-producer / single-consumer ring. The audio thread pushes only while `active` is set.
class ScopeRing
{
public:
    static constexpr int kCapacity = 512;
    std::atomic<bool> active { false };

    void push (const ScopeFrame& f)
    {
        const size_t w = writeIdx.load (std::memory_order_relaxed);
        const size_t r = readIdx.load (std::memory_order_acquire);
        if (w - r >= (size_t) kCapacity) return; // full: drop
        buf[w % kCapacity] = f;
        writeIdx.store (w + 1, std::memory_order_release);
    }
    bool pop (ScopeFrame& f)
    {
        const size_t r = readIdx.load (std::memory_order_relaxed);
        if (r == writeIdx.load (std::memory_order_acquire)) return false;
        f = buf[r % kCapacity];
        readIdx.store (r + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<ScopeFrame, kCapacity> buf {};
    std::atomic<size_t> writeIdx { 0 }, readIdx { 0 };
};
} // namespace jy
