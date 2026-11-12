#pragma once
// Deterministic synthetic test corpus shared by junkyard_calibrate and the tests (FIX_PLAN_3 §3.3). No audio files:
// everything is synthesised from fixed seeds, so the drive / Broken-trim tables are reproducible.
#include "Crossover.h"
#include "Loudness.h"
#include "Random.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace jy::corpus
{
struct Stereo
{
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
    void resize (size_t n) { l.assign (n, 0.0f); r.assign (n, 0.0f); }
};

enum Kind { Pink = 0, Drums = 1, Bass = 2, FullMix = 3, Mastered = 4, kNumKinds = 5 };
inline const char* kindName (int k)
{
    static const char* n[kNumKinds] = { "pink", "drums", "bass", "full mix", "mastered mix" };
    return n[k < 0 ? 0 : (k >= kNumKinds ? kNumKinds - 1 : k)];
}

namespace detail
{
constexpr double kPi = 3.14159265358979323846;

inline void bandpass (Biquad& b, double fs, double f, double q)   // RBJ constant 0 dB peak gain
{
    const double w = 2.0 * kPi * f / fs, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
    b.b0 = al / a0; b.b1 = 0.0; b.b2 = -al / a0; b.a1 = -2.0 * std::cos (w) / a0; b.a2 = (1.0 - al) / a0; b.reset();
}

inline float pinkStep (float* b, float w)
{
    b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
    b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
    const float p = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f; b[6] = w * 0.115926f;
    return p;
}

inline double rms (const std::vector<float>& v)
{
    double e = 0.0; for (float s : v) e += (double) s * s;
    return std::sqrt (e / (double) std::max<size_t> (1, v.size()));
}
inline double rms (const Stereo& s) { return std::sqrt (0.5 * (std::pow (rms (s.l), 2) + std::pow (rms (s.r), 2))); }
inline void scale (Stereo& s, double g) { for (auto& v : s.l) v = (float) (v * g); for (auto& v : s.r) v = (float) (v * g); }
inline void normaliseRms (Stereo& s) { const double r = rms (s); if (r > 0) scale (s, 1.0 / r); }

inline Stereo pink (double fs, double sec)
{
    Stereo s; const size_t n = (size_t) std::llround (fs * sec); s.resize (n);
    Random ra (101), rb (202); float ba[7] {}, bb[7] {};
    for (size_t i = 0; i < n; ++i) { s.l[i] = pinkStep (ba, ra.bipolar()); s.r[i] = pinkStep (bb, rb.bipolar()); }
    return s;
}

inline Stereo drums (double fs, double sec)
{
    Stereo s; const size_t n = (size_t) std::llround (fs * sec); s.resize (n);
    Random rn (303), rh (404), rh2 (505);
    const double beat = 0.5; // 120 BPM
    Biquad hpL, hpR; hpL.setHighpass (fs, 7000.0); hpR.setHighpass (fs, 7000.0);
    auto add = [&] (double t0, double dur, auto fn)
    {
        const size_t a = (size_t) std::llround (t0 * fs), len = (size_t) std::llround (dur * fs);
        for (size_t i = 0; i < len && a + i < n; ++i) { float l, r; fn ((double) i / fs, l, r); s.l[a + i] += l; s.r[a + i] += r; }
    };
    const int beats = (int) std::floor (sec / beat);
    for (int b = 0; b < beats; ++b)
    {
        const double t = b * beat; const int inBar = b % 4, bar = b / 4;
        if (true)   // kick on every beat (four on the floor) + a syncopated one on the last 16th of every second bar
        {
            double ph = 0.0;
            add (t, 0.7, [&] (double tt, float& l, float& r)
            {
                const double f = 45.0 + (200.0 - 45.0) * std::exp (-tt / 0.030);
                ph += 2.0 * kPi * f / fs;
                const float v = (float) (std::sin (ph) * std::exp (-tt / 0.140));
                l = r = v;
            });
        }
        if (inBar == 3 && (bar & 1))
        {
            double ph = 0.0;
            add (t + 0.75 * beat, 0.7, [&] (double tt, float& l, float& r)
            {
                const double f = 45.0 + (200.0 - 45.0) * std::exp (-tt / 0.030);
                ph += 2.0 * kPi * f / fs;
                l = r = (float) (std::sin (ph) * std::exp (-tt / 0.140));
            });
        }
        if (inBar == 1 || inBar == 3)   // snare
        {
            add (t, 0.35, [&] (double tt, float& l, float& r)
            {
                const float nz = rn.bipolar() * 0.35f * (float) std::exp (-tt / 0.060);
                const float body = (float) (0.3 * std::sin (2.0 * kPi * 190.0 * tt) * std::exp (-tt / 0.050));
                l = r = nz + body;
            });
        }
        for (int h = 0; h < 2; ++h)   // closed hats on the 8th notes
            add (t + h * 0.5 * beat, 0.12, [&] (double tt, float& l, float& r)
            {
                const float e = (float) std::exp (-tt / 0.018);
                l = 0.2f * e * hpL.process (rh.bipolar()); r = 0.2f * e * hpR.process (rh2.bipolar());
            });
    }
    return s;
}

inline Stereo bass (double fs, double sec)
{
    Stereo s; const size_t n = (size_t) std::llround (fs * sec); s.resize (n);
    static const double notes[16] = { 41.2, 41.2, 82.4, 41.2, 49.0, 49.0, 98.0, 55.0, 55.0, 110.0, 55.0, 73.4, 61.7, 61.7, 82.4, 46.2 };
    const double noteLen = 0.25; double ph = 0.0;
    Biquad lp1; lp1.setLowpass (fs, 700.0, 0.7071);   // 2-pole 700 Hz
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs; const int idx = (int) (t / noteLen); const double tt = t - idx * noteLen;
        const double f = notes[idx % 16];
        ph += f / fs; ph -= std::floor (ph);
        const double env = std::min (1.0, tt / 0.005) * std::exp (-tt / 0.18);
        const float v = lp1.process ((float) ((2.0 * ph - 1.0) * env));
        s.l[i] = s.r[i] = v;
    }
    return s;
}

inline Stereo pad (double fs, double sec)
{
    // 4 chords of 2 s, each with 4 saw voices (slightly detuned per channel); chords cross-fade over 100 ms so the pad never drops out.
    Stereo s; const size_t n = (size_t) std::llround (fs * sec); s.resize (n);
    static const double chords[4][4] = { { 110.0, 130.81, 164.81, 196.0 }, { 87.31, 110.0, 130.81, 164.81 },
                                         { 130.81, 164.81, 196.0, 261.63 }, { 98.0, 123.47, 146.83, 196.0 } };
    double phL[4][4] = {}, phR[4][4] = {};
    Biquad lpL, lpR; lpL.setLowpass (fs, 2500.0); lpR.setLowpass (fs, 2500.0);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs; const double pos = t / 2.0; const int k = (int) pos; const double tb = (pos - k) * 2.0;
        float l = 0, r = 0;
        double g[4] = { 0, 0, 0, 0 };
        const int cur = k % 4, nxt = (k + 1) % 4, prv = (k + 3) % 4;
        if (tb < 0.05)       { const double x = (tb + 0.05) / 0.1; g[cur] = 0.5 - 0.5 * std::cos (kPi * x); g[prv] = 0.5 + 0.5 * std::cos (kPi * x); }
        else if (tb > 1.95)  { const double x = (tb - 1.95) / 0.1; g[cur] = 0.5 + 0.5 * std::cos (kPi * x); g[nxt] = 0.5 - 0.5 * std::cos (kPi * x); }
        else                   g[cur] = 1.0;
        for (int c = 0; c < 4; ++c)
            for (int v = 0; v < 4; ++v)
            {
                phL[c][v] += chords[c][v] * 1.003 / fs; phL[c][v] -= std::floor (phL[c][v]);
                phR[c][v] += chords[c][v] * 0.997 / fs; phR[c][v] -= std::floor (phR[c][v]);
                if (g[c] > 0.0) { l += (float) (g[c] * (2.0 * phL[c][v] - 1.0)); r += (float) (g[c] * (2.0 * phR[c][v] - 1.0)); }
            }
        s.l[i] = lpL.process (l * 0.25f); s.r[i] = lpR.process (r * 0.25f);
    }
    return s;
}

inline Stereo vocal (double fs, double sec)
{
    Stereo s; const size_t n = (size_t) std::llround (fs * sec); s.resize (n);
    Random r (606); double ph = 0.0;
    Biquad f1, f2, f3; bandpass (f1, fs, 700.0, 5.0); bandpass (f2, fs, 1200.0, 6.0); bandpass (f3, fs, 2600.0, 8.0);
    const double syl = 0.40; double f0 = 200.0; int lastIdx = -1;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs; const int idx = (int) (t / syl); const double tt = t - idx * syl;
        if (idx != lastIdx) { lastIdx = idx; f0 = 170.0 + 90.0 * r.uniform(); }
        const double env = (tt < 0.30) ? std::min (1.0, tt / 0.02) * std::min (1.0, (0.30 - tt) / 0.06) : 0.0;
        ph += f0 / fs; float pulse = 0.0f;
        if (ph >= 1.0) { ph -= 1.0; pulse = 1.0f; }
        const float x = pulse * (float) env;
        const float v = f1.process (x) + 0.6f * f2.process (x) + 0.3f * f3.process (x);
        s.l[i] = s.r[i] = v;
    }
    return s;
}

inline Stereo fullMix (double fs, double sec)
{
    auto d = drums (fs, sec), b = bass (fs, sec), p = pad (fs, sec), v = vocal (fs, sec);
    for (auto* x : { &d, &b, &p, &v }) normaliseRms (*x);
    const double gd = 1.0, gb = std::pow (10.0, -2.0 / 20.0), gp = std::pow (10.0, -8.0 / 20.0), gv = std::pow (10.0, -5.0 / 20.0);
    Stereo m; m.resize (d.size());
    for (size_t i = 0; i < m.size(); ++i)
    {
        m.l[i] = (float) (gd * d.l[i] + gb * b.l[i] + gp * p.l[i] + gv * v.l[i]);
        m.r[i] = (float) (gd * d.r[i] + gb * b.r[i] + gp * p.r[i] + gv * v.r[i]);
    }
    return m;
}

// Brickwall peak limiter, offline: 2 ms lookahead (true look-ahead min + moving-average attack), 50 ms release.
inline void brickwall (Stereo& s, double fs, double ceilingDb)
{
    const size_t n = s.size(); const int la = std::max (1, (int) std::lround (0.002 * fs));
    const float ceil = (float) std::pow (10.0, ceilingDb / 20.0);
    std::vector<float> req (n), mn (n), g (n);
    for (size_t i = 0; i < n; ++i) { const float p = std::max (std::fabs (s.l[i]), std::fabs (s.r[i])); req[i] = p > ceil ? ceil / p : 1.0f; }
    for (size_t i = 0; i < n; ++i) { float m = 1.0f; for (int k = 0; k < la && i + (size_t) k < n; ++k) m = std::min (m, req[i + (size_t) k]); mn[i] = m; }
    const float rel = 1.0f - (float) std::exp (-1.0 / (0.050 * fs));
    float gp = 1.0f;
    for (size_t i = 0; i < n; ++i) { gp = std::min (mn[i], gp + (1.0f - gp) * rel); g[i] = gp; }
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        acc += g[i]; if (i >= (size_t) la) acc -= g[i - (size_t) la];
        const size_t cnt = std::min<size_t> (i + 1, (size_t) la);
        const float gs = (float) (acc / (double) cnt);
        s.l[i] *= gs; s.r[i] *= gs;
    }
}

inline Stereo mastered (double fs, double sec)
{
    auto m = fullMix (fs, sec);
    double pk = 0.0; for (size_t i = 0; i < m.size(); ++i) pk = std::max ({ pk, (double) std::fabs (m.l[i]), (double) std::fabs (m.r[i]) });
    if (pk > 0) scale (m, 1.0 / pk);
    for (auto& v : m.l) v = std::tanh (2.0f * v);
    for (auto& v : m.r) v = std::tanh (2.0f * v);
    // push into the limiter until the loudness is about -8 LUFS
    double lo = 0.0, hi = 60.0;   // pre-gain, dB
    Stereo best;
    for (int it = 0; it < 14; ++it)
    {
        const double mid = 0.5 * (lo + hi); auto t = m; scale (t, std::pow (10.0, mid / 20.0)); brickwall (t, fs, -0.1);
        const double L = loud::lufs (t.l, t.r, 0, t.size(), fs);
        best = std::move (t);
        if (L < -8.0) lo = mid; else hi = mid;
    }
    return best;
}
} // namespace detail

inline Stereo make (int kind, double fs, double seconds)
{
    switch (kind)
    {
        case Pink: return detail::pink (fs, seconds);
        case Drums: return detail::drums (fs, seconds);
        case Bass: return detail::bass (fs, seconds);
        case FullMix: return detail::fullMix (fs, seconds);
        default: return detail::mastered (fs, seconds);
    }
}

inline double lufs (const Stereo& s, double fs, size_t from = 0) { return loud::lufs (s.l, s.r, from, s.size(), fs); }

// Scales in place so the whole span reads `target` LUFS (ungated).
inline void scaleToLufs (Stereo& s, double fs, double target)
{
    const double L = lufs (s, fs);
    detail::scale (s, std::pow (10.0, (target - L) / 20.0));
}

inline double crestDb (const Stereo& s, size_t from = 0)
{
    double pk = 0.0, e = 0.0; size_t k = 0;
    for (size_t i = from; i < s.size(); ++i)
    {
        pk = std::max ({ pk, (double) std::fabs (s.l[i]), (double) std::fabs (s.r[i]) });
        e += (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i]; k += 2;
    }
    return 20.0 * std::log10 (pk + 1e-30) - 10.0 * std::log10 (e / (double) std::max<size_t> (1, k) + 1e-30);
}
} // namespace jy::corpus
