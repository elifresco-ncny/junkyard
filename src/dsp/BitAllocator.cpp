#include "BitAllocator.h"
#include "CodecProfiles.h"
#include <cmath>
#include <algorithm>

namespace jy
{
namespace
{
constexpr int kTab = 1024;
inline float stepFromGain (int g) { return std::exp2 ((float) g * 0.25f); }
}

void BitAllocator::prepare (const BandLayout& layout)
{
    bl = &layout; N = layout.hop();
    a75.assign ((size_t) N, 0.0f); q.assign ((size_t) N, 0);
    nz.assign ((size_t) N, 0); bz.assign ((size_t) kMaxBands, 0);
    log2tab.resize (kTab); pow43tab.resize (kTab);
    for (int i = 0; i < kTab; ++i) { log2tab[(size_t) i] = i > 0 ? std::log2 ((float) i) : 0.0f; pow43tab[(size_t) i] = std::pow ((float) i, 4.0f / 3.0f); }
}

// Bit-cost stand-in for Huffman coding. Zeros above the last nonzero bin are free ("rzero" region).
float BitAllocator::costFor (int g, int kmax) const
{
    const auto& T = tuning();
    const float inv75 = std::exp2 (-0.75f * (float) g * 0.25f);
    float cost = 0.0f, pend = 0.0f;
    const auto& L = *bl;
    for (int b = 0; b < L.count(); ++b)
    {
        const int s = L[b].start, e = std::min (L[b].end, kmax);
        if (s >= e) break;
        bool active = false;
        for (int k = s; k < e; ++k)
        {
            const int qi = (int) (a75[(size_t) k] * inv75 + 0.4054f);
            if (qi == 0) pend += T.c0;
            else
            {
                active = true;
                cost += pend + T.c1 + T.c2 * (qi < kTab ? log2tab[(size_t) qi] : std::log2 ((float) qi));
                pend = 0.0f;
            }
        }
        if (active) cost += T.c3;
    }
    return cost;
}

int BitAllocator::innerLoop (float budget, int kmax, float& costOut) const
{
    int lo = -120, hi = 80; // smallest g with cost <= budget
    if (costFor (hi, kmax) > budget) { costOut = costFor (hi, kmax); return hi; }
    while (hi - lo > 1)
    {
        const int mid = (lo + hi) >> 1;
        if (costFor (mid, kmax) <= budget) hi = mid; else lo = mid;
    }
    costOut = costFor (hi, kmax);
    return hi;
}

void BitAllocator::quantise (int g, int kmax, const float* x, float* xhat)
{
    const float step = stepFromGain (g), inv75 = std::exp2 (-0.75f * (float) g * 0.25f);
    const auto& L = *bl;
    for (int k = 0; k < N; ++k) { xhat[k] = 0.0f; q[(size_t) k] = 0; nz[(size_t) k] = 0; }
    for (int b = 0; b < L.count(); ++b)
    {
        const int s = L[b].start, e = std::min (L[b].end, kmax);
        for (int k = s; k < e; ++k)
        {
            const int qi = (int) (a75[(size_t) k] * inv75 + 0.4054f);
            q[(size_t) k] = qi;
            if (qi > 0)
            {
                const float mag = (qi < kTab ? pow43tab[(size_t) qi] : std::pow ((float) qi, 4.0f / 3.0f)) * step / sfAmp[b];
                xhat[k] = x[k] < 0 ? -mag : mag;
                nz[(size_t) k] = 1;
            }
        }
    }
}

AllocResult BitAllocator::run (const float* x, float* xhat, const PsyResult& psy, const AllocParams& P)
{
    const auto& T = tuning();
    const auto& L = *bl;
    const int nb = L.count();
    AllocResult res;
    for (int b = 0; b < nb; ++b) { sfDb[b] = 0.0f; sfAmp[b] = 1.0f; bz[(size_t) b] = 0; }

    // jittered thresholds (swirl)
    float thr[kMaxBands];
    for (int b = 0; b < nb; ++b)
    {
        thr[b] = psy.thr[b];
        if (P.swirl > 0.0f && P.rng) thr[b] *= std::pow (10.0f, P.rng->bipolar() * P.swirl * 1.2f);
    }

    const float budget = P.budgetBits; // header already deducted by the caller
    if (budget <= 0.0f)
    {
        for (int k = 0; k < N; ++k) { xhat[k] = 0.0f; nz[(size_t) k] = 0; }
        for (int b = 0; b < nb; ++b) bz[(size_t) b] = 1;
        res.zeroedBands = nb;
        return res;
    }

    auto prepareA75 = [&]
    {
        for (int b = 0; b < nb; ++b)
            for (int k = L[b].start; k < std::min (L[b].end, P.kmax); ++k)
            {
                const float a = std::fabs (x[k]) * sfAmp[b];
                const float r = std::sqrt (a);
                a75[(size_t) k] = r * std::sqrt (r); // a^0.75
            }
    };

    prepareA75();
    float cost = 0.0f;
    int g = innerLoop (budget, P.kmax, cost);
    quantise (g, P.kmax, x, xhat);

    for (int pass = 0; pass < T.outerPasses; ++pass)
    {
        bool changed = false;
        for (int b = 0; b < nb; ++b)
        {
            const int s = L[b].start, e = std::min (L[b].end, P.kmax);
            if (s >= e) continue;
            float noise = 0.0f;
            for (int k = s; k < e; ++k) { const float d = x[k] - xhat[k]; noise += d * d; }
            noise /= (float) (e - s);
            const bool protectTonal = (P.swirl > 0.7f) && psy.tonality[b] > 0.5f; // swirl >70%: cut protection
            if (noise > thr[b] && psy.energy[b] > thr[b] && sfDb[b] < T.sfMaxDb && ! protectTonal)
            {
                sfDb[b] += T.sfStepDb;
                sfAmp[b] = std::pow (10.0f, sfDb[b] / 20.0f);
                changed = true;
            }
        }
        if (! changed) break;
        prepareA75();
        g = innerLoop (budget, P.kmax, cost);
        quantise (g, P.kmax, x, xhat);
    }

    // Swirl: bands near the zeroing threshold flicker on/off.
    if (P.swirl > 0.0f && P.rng)
    {
        const float step = stepFromGain (g);
        const float zeroThr = 0.5f * step;      // |x|*sf below this -> q = 0 (approx.)
        const float marginDb = P.swirl * 9.0f;
        const float pOff = 0.25f + 0.55f * P.swirl;
        for (int b = 0; b < nb; ++b)
        {
            const int s = L[b].start, e = std::min (L[b].end, P.kmax);
            if (s >= e) continue;
            float peak = 0.0f;
            bool any = false;
            for (int k = s; k < e; ++k) { peak = std::max (peak, std::fabs (x[k]) * sfAmp[b]); any |= (nz[(size_t) k] != 0); }
            const float mdb = 20.0f * std::log10 (peak / zeroThr + 1e-12f);
            if (any && mdb < marginDb)
            {
                if (P.rng->chance (pOff))
                    for (int k = s; k < e; ++k) { xhat[k] = 0.0f; nz[(size_t) k] = 0; }
            }
            else if (! any && mdb > -marginDb && mdb < 0.0f && P.rng->chance (pOff * 0.6f))
            {
                for (int k = s; k < e; ++k)
                    if (std::fabs (x[k]) * sfAmp[b] > zeroThr * 0.5f)
                    { xhat[k] = (x[k] < 0 ? -1.0f : 1.0f) * step / sfAmp[b]; nz[(size_t) k] = 1; }
            }
        }
    }

    for (int b = 0; b < nb; ++b)
    {
        bool any = false;
        for (int k = L[b].start; k < std::min (L[b].end, N); ++k) any |= (nz[(size_t) k] != 0);
        bz[(size_t) b] = any ? 0 : 1;
        res.zeroedBands += any ? 0 : 1;
    }
    res.usedBits = cost;
    res.globalGain = g;
    return res;
}
} // namespace jy
