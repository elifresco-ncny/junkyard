#pragma once
#include "BandLayout.h"
#include <vector>

namespace jy
{
struct PsyResult
{
    float energy[kMaxBands] {};    // mean energy per bin
    float thr[kMaxBands] {};       // allowed noise energy per bin
    float tonality[kMaxBands] {};  // 0 noise .. 1 tonal
    float pe = 0.0f;               // perceptual entropy estimate (bits)
};

// Crude psychoacoustic model (§6.3): spreading, tonality-dependent masking offset, absolute threshold of hearing.
class Psychoacoustic
{
public:
    void prepare (const BandLayout& layout);
    void analyse (const float* X, int kmax, PsyResult& out) const;
private:
    const BandLayout* bl = nullptr;
    float ath[kMaxBands] {};
};
} // namespace jy
