#pragma once
// Shared helpers for the v0.4 tests.
#include "dsp/Tape.h"
#include "dsp/Corpus.h"
#include "dsp/Loudness.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace tu
{
constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;
using Stereo = jy::corpus::Stereo;

inline double rmsDb (const std::vector<float>& v, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) v[i] * v[i];
    return 10.0 * std::log10 (e / (double) std::max<size_t> (1, b - a) + 1e-30);
}
inline double rmsDb (const std::vector<float>& v) { return rmsDb (v, 0, v.size()); }

inline std::vector<float> sine (size_t n, double freq, double amp, double fs = kFs)
{
    std::vector<float> v (n); for (size_t i = 0; i < n; ++i) v[i] = (float) (amp * std::sin (2.0 * kPi * freq * (double) i / fs)); return v;
}

// Pink noise scaled to a given RMS (dBFS)
inline std::vector<float> pinkNoise (size_t n, double rmsDbfs, unsigned seed = 5)
{
    std::mt19937 g (seed); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> v (n); float b[7] {};
    for (auto& s : v)
    {
        const float w = d (g);
        b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
        b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
        s = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f; b[6] = w * 0.115926f;
    }
    const double cur = rmsDb (v);
    const float g2 = (float) std::pow (10.0, (rmsDbfs - cur) / 20.0);
    for (auto& s : v) s *= g2;
    return v;
}

// Run a Tape (stereo) over l/r; returns the output
inline Stereo runTape (jy::Tape& t, const Stereo& in, int block = 64)
{
    Stereo o; o.resize (in.size());
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        const float* ip[2] = { in.l.data() + pos, in.r.data() + pos }; float* op[2] = { o.l.data() + pos, o.r.data() + pos };
        t.process (ip, op, n);
    }
    return o;
}
inline Stereo runTape (const jy::EngineParams& p, const Stereo& in, int block = 64, double fs = kFs, int solo = 0)
{
    jy::Tape t; t.prepare (fs, 2, 512); t.setParams (p); t.setAgeSolo (solo);
    return runTape (t, in, block);
}
inline Stereo mono2 (const std::vector<float>& m) { Stereo s; s.l = m; s.r = m; return s; }

inline jy::EngineParams tapeBase()
{
    jy::EngineParams p; p.codecOn = false; p.tapeOn = true; p.saturation = 0; p.wow = p.flutter = p.age = p.hiss = 0; p.toneHz = 20000.0f; p.mix = 1.0f;
    return p;
}

// Complex demodulation of a tone at f0: per-block mean of x * e^{-j w t}
struct Demod { std::vector<double> amp, phase; };
inline Demod demod (const std::vector<float>& x, double f0, size_t blk, size_t from = 0)
{
    Demod d;
    for (size_t pos = from; pos + blk <= x.size(); pos += blk)
    {
        double re = 0, im = 0;
        for (size_t i = pos; i < pos + blk; ++i)
        {
            const double ph = 2.0 * kPi * f0 * (double) i / kFs;
            re += x[i] * std::cos (ph); im -= x[i] * std::sin (ph);
        }
        d.amp.push_back (2.0 * std::hypot (re, im) / (double) blk); d.phase.push_back (std::atan2 (im, re));
    }
    return d;
}
inline std::vector<double> relFreqDev (const Demod& d, double f0, double blkSec)
{
    std::vector<double> v;
    for (size_t i = 1; i < d.phase.size(); ++i)
    {
        double dp = d.phase[i] - d.phase[i - 1];
        while (dp > kPi) dp -= 2 * kPi; while (dp < -kPi) dp += 2 * kPi;
        v.push_back (dp / (2 * kPi * blkSec) / f0);
    }
    return v;
}
inline double peakAbs (const std::vector<double>& v, size_t skip = 0) { double m = 0; for (size_t i = skip; i < v.size(); ++i) m = std::max (m, std::fabs (v[i])); return m; }

// dominant modulation frequency of a series sampled at `rate` Hz (search 0.2 .. rate/2)
inline double dominantFreq (const std::vector<double>& v, double rate, size_t skip = 0)
{
    double best = 0, bf = 0;
    for (double f = 0.2; f < rate * 0.5; f += 0.05)
    {
        double re = 0, im = 0;
        for (size_t i = skip; i < v.size(); ++i) { re += v[i] * std::cos (2 * kPi * f * (double) i / rate); im += v[i] * std::sin (2 * kPi * f * (double) i / rate); }
        const double e = re * re + im * im; if (e > best) { best = e; bf = f; }
    }
    return bf;
}

// Band RMS (dB) of v between lo and hi Hz via cascaded 2-pole HP and LP (two stages each)
inline double bandDb (const std::vector<float>& v, double lo, double hi, size_t from = 0, double fs = kFs)
{
    jy::Biquad h1, h2, l1, l2; h1.setHighpass (fs, lo); h2.setHighpass (fs, lo); l1.setLowpass (fs, hi); l2.setLowpass (fs, hi);
    double e = 0; size_t k = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const float y = l2.process (l1.process (h2.process (h1.process (v[i]))));
        if (i >= from) { e += (double) y * y; ++k; }
    }
    return 10.0 * std::log10 (e / (double) std::max<size_t> (1, k) + 1e-30);
}

inline double crest (const std::vector<float>& v, size_t from)
{
    double pk = 0, e = 0; size_t k = 0;
    for (size_t i = from; i < v.size(); ++i) { pk = std::max (pk, (double) std::fabs (v[i])); e += (double) v[i] * v[i]; ++k; }
    return 20.0 * std::log10 (pk + 1e-30) - 10.0 * std::log10 (e / (double) k + 1e-30);
}
// loudness of `out` vs `in` over [from, end) where out is delayed by `lat`
inline double loudnessDelta (const Stereo& in, const Stereo& out, size_t lat, size_t from)
{
    const size_t n = in.size() - lat;
    Stereo o, i; o.l.assign (out.l.begin() + (long) lat, out.l.begin() + (long) lat + (long) n); o.r.assign (out.r.begin() + (long) lat, out.r.begin() + (long) lat + (long) n);
    i.l.assign (in.l.begin(), in.l.begin() + (long) n); i.r.assign (in.r.begin(), in.r.begin() + (long) n);
    return jy::loud::lufs (o.l, o.r, from, n, kFs) - jy::loud::lufs (i.l, i.r, from, n, kFs);
}
} // namespace tu
