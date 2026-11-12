#pragma once
#include "BandLayout.h"
#include "Psychoacoustic.h"
#include "Random.h"
#include <vector>
#include <cstdint>

namespace jy
{
struct AllocParams
{
    float budgetBits = 1000.0f;
    int kmax = 512;          // bins >= kmax are not coded
    float swirl = 0.0f;
    Random* rng = nullptr;
};

struct AllocResult
{
    float usedBits = 0.0f;
    int globalGain = 0;
    int zeroedBands = 0;
};

// Simplified MP3-style inner/outer quantization loops (§6.4) with swirl (§6.8).
class BitAllocator
{
public:
    void prepare (const BandLayout& layout);
    // x: input spectrum; xhat: dequantized output (zeros where starved).
    AllocResult run (const float* x, float* xhat, const PsyResult& psy, const AllocParams& p);
    const std::vector<uint8_t>& nonzero() const { return nz; }
    const std::vector<uint8_t>& bandZero() const { return bz; }

private:
    float costFor (int g, int kmax) const;
    int innerLoop (float budget, int kmax, float& costOut) const;
    void quantise (int g, int kmax, const float* x, float* xhat);

    const BandLayout* bl = nullptr;
    int N = 0;
    std::vector<float> a75;   // (|x|*sf)^0.75
    std::vector<int> q;
    std::vector<uint8_t> nz, bz;
    float sfAmp[kMaxBands] {}, sfDb[kMaxBands] {};
    std::vector<float> log2tab, pow43tab;
};
} // namespace jy
