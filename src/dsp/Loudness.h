#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

namespace jy
{
// ITU-R BS.1770-4 K-weighting (pre-filter high shelf + RLB high-pass) and an ungated loudness measure.
// Shared by junkyard_calibrate, the Learn analysis and the tests. Double precision throughout.
// Filter designs follow the libebur128 / pyloudnorm formulas, so they are valid at any sample rate.
struct KWeightChannel
{
    struct Bq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                inline double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Bq shelf, hp;

    void prepare (double fs)
    {
        constexpr double pi = 3.14159265358979323846;
        {   // high shelf
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {   // RLB high-pass
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
            // (the standard leaves the numerator un-normalised; the overall gain is unity above the corner)
        }
        shelf.z1 = shelf.z2 = hp.z1 = hp.z2 = 0.0;
    }
    inline double process (double x) { return hp.process (shelf.process (x)); }
};

namespace loud
{
// Loudness (LKFS/LUFS, ungated) of frames [from, to) of up to 2 channels. The filters run from frame 0, so the
// measured span is free of start-up transients when `from` > 0.
inline double lufs (const std::vector<float>* const* ch, int nch, size_t from, size_t to, double fs)
{
    if (to <= from) return -200.0;
    double sum = 0.0;
    for (int c = 0; c < nch; ++c)
    {
        KWeightChannel k; k.prepare (fs);
        const auto& v = *ch[c]; double acc = 0.0;
        for (size_t i = 0; i < to && i < v.size(); ++i)
        {
            const double z = k.process ((double) v[i]);
            if (i >= from) acc += z * z;
        }
        sum += acc / (double) (to - from);
    }
    return -0.691 + 10.0 * std::log10 (sum + 1e-30);
}
inline double lufs (const std::vector<float>& l, const std::vector<float>& r, size_t from, size_t to, double fs)
{
    const std::vector<float>* c[2] = { &l, &r };
    return lufs (c, 2, from, to, fs);
}
inline double lufs (const std::vector<float>& m, size_t from, size_t to, double fs)
{
    const std::vector<float>* c[1] = { &m };
    return lufs (c, 1, from, to, fs);
}
} // namespace loud
} // namespace jy
