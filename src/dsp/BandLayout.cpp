#include "BandLayout.h"
#include <cmath>
#include <algorithm>

namespace jy
{
float BandLayout::hzToBark (float f)
{
    return 13.0f * std::atan (0.00076f * f) + 3.5f * std::atan ((f / 7500.0f) * (f / 7500.0f));
}

int BandLayout::binForHz (double f) const { return std::max (0, std::min (N, (int) std::lround (f / hz))); }

void BandLayout::prepare (double sr, int hop)
{
    N = hop; hz = sr * 0.5 / hop;
    bands.clear();
    // edges: one band per Bark, with a minimum width of 4 bins
    int prev = 0;
    for (int b = 1; b <= 25 && prev < N; ++b)
    {
        // invert bark -> Hz by bisection
        double lo = 0, hi = 40000;
        for (int it = 0; it < 40; ++it) { double mid = 0.5 * (lo + hi); (hzToBark ((float) mid) < b ? lo : hi) = mid; }
        int edge = (int) std::lround (0.5 * (lo + hi) / hz);
        edge = std::min (edge, N);
        if (edge - prev < 4 && edge < N) continue;
        if (edge <= prev) continue;
        if (N - edge < 4) edge = N;
        Band bd; bd.start = prev; bd.end = edge; bands.push_back (bd);
        prev = edge;
    }
    if (prev < N) { if (! bands.empty() && N - prev < 4) bands.back().end = N; else { Band bd; bd.start = prev; bd.end = N; bands.push_back (bd); } }
    if ((int) bands.size() > kMaxBands) bands.resize (kMaxBands);
    nBands = (int) bands.size();
    bands.back().end = N;
    for (auto& b : bands)
    {
        b.hz = (float) ((b.start + b.end) * 0.5 * hz);
        b.bark = hzToBark (b.hz);
    }
    spreadMat.assign ((size_t) kMaxBands * kMaxBands, 0.0f);
    for (int i = 0; i < nBands; ++i)
        for (int j = 0; j < nBands; ++j)
        {
            const float dz = bands[(size_t) i].bark - bands[(size_t) j].bark; // victim - masker
            const float slope = dz > 0 ? 10.0f : 25.0f;                      // dB per Bark
            spreadMat[(size_t) (i * kMaxBands + j)] = std::pow (10.0f, -slope * std::fabs (dz) / 10.0f);
        }
}
} // namespace jy
