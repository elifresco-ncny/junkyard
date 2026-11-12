#include "Psychoacoustic.h"
#include "CodecProfiles.h"
#include <cmath>
#include <algorithm>

namespace jy
{
void Psychoacoustic::prepare (const BandLayout& layout)
{
    bl = &layout;
    for (int b = 0; b < layout.count(); ++b)
    {
        const float f = std::max (20.0f, layout[b].hz) / 1000.0f;
        // Terhardt absolute threshold in quiet (dB SPL)
        const float dB = 3.64f * std::pow (f, -0.8f) - 6.5f * std::exp (-0.6f * (f - 3.3f) * (f - 3.3f)) + 0.001f * std::pow (f, 4.0f);
        // 0 dBFS sine ~ 92 dB SPL, sine power 0.5
        ath[b] = 0.5f * std::pow (10.0f, (dB - 92.0f + tuning().athOffsetDb) / 10.0f);
    }
}

void Psychoacoustic::analyse (const float* X, int kmax, PsyResult& r) const
{
    const auto& L = *bl;
    const int nb = L.count();
    const auto& T = tuning();
    for (int b = 0; b < nb; ++b)
    {
        const auto& bd = L[b];
        const int e = std::min (bd.end, kmax);
        float sum = 0.0f, logSum = 0.0f;
        int cnt = 0;
        for (int k = bd.start; k < e; ++k)
        {
            const float p = X[k] * X[k];
            sum += p; logSum += std::log (p + 1e-20f); ++cnt;
        }
        if (cnt == 0) { r.energy[b] = 0; r.tonality[b] = 0; continue; }
        const float mean = sum / (float) cnt;
        r.energy[b] = mean;
        const float sfmDb = 10.0f * (logSum / (float) cnt - std::log (mean + 1e-20f)) * 0.4342945f;
        r.tonality[b] = std::min (1.0f, std::max (0.0f, sfmDb / -60.0f));
    }
    float pe = 0.0f;
    for (int i = 0; i < nb; ++i)
    {
        float sp = 0.0f;
        for (int j = 0; j < nb; ++j) sp += L.spread (i, j) * r.energy[j];
        const float a = r.tonality[i];
        const float offDb = a * (T.tonalOffsetDb + L[i].bark) + (1.0f - a) * T.noiseOffsetDb;
        const float t = sp * std::pow (10.0f, -offDb / 10.0f);
        r.thr[i] = std::max (std::max (t, ath[i]), 1e-14f);
        if (L[i].start < kmax)
            pe += (float) (std::min (L[i].end, kmax) - L[i].start) * std::log2 (1.0f + std::sqrt (r.energy[i] / r.thr[i]));
    }
    r.pe = pe;
}
} // namespace jy
