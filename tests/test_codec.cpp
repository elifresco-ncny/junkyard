#include <catch2/catch_test_macros.hpp>
#include "dsp/Mdct.h"
#include "dsp/CodecEngine.h"
#include "dsp/BandLayout.h"
#include <juce_dsp/juce_dsp.h>
#include <random>
#include <cmath>

using namespace jy;

namespace
{
std::vector<float> whiteNoise (int n, float amp, unsigned seed)
{
    std::mt19937 g (seed); std::normal_distribution<float> d (0.0f, amp);
    std::vector<float> v ((size_t) n); for (auto& s : v) s = d (g); return v;
}
// Music-like: a few decaying harmonic tones + noise bursts
std::vector<float> musicish (int n, double sr, unsigned seed)
{
    std::mt19937 g (seed); std::normal_distribution<float> d (0.0f, 1.0f);
    std::vector<float> v ((size_t) n, 0.0f);
    const double f0s[] = { 110, 220, 330, 440, 660, 880, 1320, 2640, 4000 };
    for (size_t t = 0; t < v.size(); ++t)
    {
        double s = 0;
        for (int h = 0; h < 9; ++h) s += std::sin (6.2831853 * f0s[h] * (double) t / sr) / (1.0 + h);
        v[t] = (float) (0.12 * s) + 0.02f * d (g);
        if ((t / 6000) % 5 == 0 && (t % 6000) < 400) v[t] += 0.3f * d (g);
    }
    return v;
}
std::vector<float> runCodec (CodecEngine& c, const std::vector<float>& in, int nch = 2, int block = 256)
{
    std::vector<float> l = in, r = in, out;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        const float* ip[2] = { l.data() + pos, r.data() + pos };
        float tl[4096], tr[4096]; float* op[2] = { tl, tr };
        c.process (ip, op, n);
        out.insert (out.end(), tl, tl + n);
        (void) nch;
    }
    return out;
}
}

TEST_CASE ("fast MDCT matches naive reference; inverse+OLA reconstructs perfectly")
{
    for (int N : { 512, 1024, 2048 })
    {
        Mdct m (N);
        auto x = whiteNoise (2 * N, 0.5f, 3);
        std::vector<float> X ((size_t) N);
        m.forward (x.data(), X.data());
        const double pi = 3.14159265358979323846, n0 = 0.5 + N / 2.0, sc = std::sqrt (2.0 / N);
        double maxErr = 0, peak = 0;
        for (int k = 0; k < N; k += (N > 600 ? 3 : 1))
        {
            double acc = 0;
            for (int n = 0; n < 2 * N; ++n)
                acc += std::sin (pi * (n + 0.5) / (2 * N)) * x[(size_t) n] * std::cos (pi / N * (n + n0) * (k + 0.5));
            acc *= sc;
            maxErr = std::max (maxErr, std::fabs (acc - X[(size_t) k])); peak = std::max (peak, std::fabs (acc));
        }
        INFO ("N " << N << " err " << maxErr << " peak " << peak);
        REQUIRE (maxErr < 1e-5 * std::max (1.0, peak));
    }
    // TDAC reconstruction
    const int N = 512; Mdct m (N);
    auto x = whiteNoise (N * 12, 0.5f, 9);
    std::vector<float> out (x.size(), 0.0f), X ((size_t) N), y ((size_t) 2 * N);
    for (size_t f = 0; f + 2 * (size_t) N <= x.size(); f += (size_t) N)
    {
        m.forward (x.data() + f, X.data());
        m.inverse (X.data(), y.data());
        for (int i = 0; i < 2 * N; ++i) out[f + (size_t) i] += y[(size_t) i];
    }
    double err = 0, sig = 0;
    for (size_t i = (size_t) N; i < x.size() - (size_t) N; ++i) { err += std::pow (out[i] - x[i], 2); sig += std::pow (x[i], 2); }
    REQUIRE (10 * std::log10 (err / sig + 1e-30) < -100);
}

TEST_CASE ("band layout covers all bins with sane band count")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        BandLayout bl; bl.prepare (sr, codecHop (sr));
        REQUIRE (bl.count() >= 18); REQUIRE (bl.count() <= 30);
        int prev = 0;
        for (int b = 0; b < bl.count(); ++b) { REQUIRE (bl[b].start == prev); REQUIRE (bl[b].width() >= 4); prev = bl[b].end; }
        REQUIRE (prev == codecHop (sr));
    }
}

TEST_CASE ("codec pass-through (no quantisation) is a perfect 2N delay")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        CodecEngine c; c.prepare (sr, 2, 4096); c.passThrough = true;
        auto in = whiteNoise (40000, 0.3f, 1);
        auto out = runCodec (c, in);
        const int L = c.getLatency();
        double err = 0, sig = 0;
        for (size_t i = (size_t) L + 4096; i < in.size(); ++i) { err += std::pow (out[i] - in[i - (size_t) L], 2); sig += std::pow (in[i - (size_t) L], 2); }
        REQUIRE (10 * std::log10 (err / sig + 1e-30) < -100);
    }
}

TEST_CASE ("impulse through the quantising codec at 320 kbps lands at reported latency")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        CodecEngine c; c.prepare (sr, 2, 4096);
        EngineParams p; p.bitrate = 1.0f; p.swirl = 0; c.setParams (p);
        std::vector<float> in (30000, 0.0f); in[9000] = 0.8f;
        auto out = runCodec (c, in);
        int am = 0; for (size_t i = 0; i < out.size(); ++i) if (std::fabs (out[i]) > std::fabs (out[(size_t) am])) am = (int) i;
        REQUIRE (am == 9000 + c.getLatency());
    }
}

TEST_CASE ("lower bitrate -> fewer nonzero bins")
{
    for (int sig = 0; sig < 2; ++sig)
    {
        auto in = sig ? musicish (96000, 48000, 1) : whiteNoise (96000, 0.1f, 2);
        double prev = 1e18;
        for (float kbps : { 320.0f, 192.0f, 128.0f, 96.0f, 64.0f, 32.0f, 16.0f, 8.0f })
        {
            CodecEngine c; c.prepare (48000, 2, 4096);
            EngineParams p; p.bitrate = kbpsToBitrate (0, kbps); p.swirl = 0; c.setParams (p);
            runCodec (c, in);
            INFO ("signal " << sig << " kbps " << kbps << " nonzero " << c.diag.nonzeroBins);
            REQUIRE (c.diag.nonzeroBins <= prev * 1.001);
            prev = c.diag.nonzeroBins;
        }
    }
}

TEST_CASE ("measured -20 dB bandwidth tracks the cutoff table (+-10%)")
{
    auto in = whiteNoise (96000, 0.2f, 4);
    for (float kbps : { 192.0f, 128.0f, 96.0f, 64.0f, 48.0f })
    {
        CodecEngine c; c.prepare (48000, 2, 4096);
        EngineParams p; p.bitrate = kbpsToBitrate (0, kbps); p.swirl = 0; c.setParams (p);
        auto out = runCodec (c, in);
        // averaged magnitude spectrum
        const int F = 2048; juce::dsp::FFT fft (11);
        std::vector<float> buf ((size_t) 2 * F); std::vector<double> psd ((size_t) F / 2, 0.0);
        int frames = 0;
        for (size_t s = 8192; s + F < out.size(); s += F)
        {
            for (int i = 0; i < F; ++i) buf[(size_t) i] = out[s + (size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / F));
            std::fill (buf.begin() + F, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int k = 0; k < F / 2; ++k) psd[(size_t) k] += buf[(size_t) k] * buf[(size_t) k];
            ++frames;
        }
        double ref = 0; int cnt = 0;
        for (int k = 20; k < 200; ++k) { ref += psd[(size_t) k]; ++cnt; }
        ref /= cnt;
        int top = 0;
        for (int k = 20; k < F / 2; ++k) if (psd[(size_t) k] > ref * 0.01 * 0 + ref * 0.01) top = k; // -20 dB in power
        const double bw = top * 48000.0 / F;
        const double expect = cutoffHz (kbps);
        INFO ("kbps " << kbps << " measured " << bw << " expect " << expect);
        REQUIRE (std::fabs (bw - expect) < 0.10 * expect);
    }
}

TEST_CASE ("codec is deterministic for the same seed and input")
{
    auto in = musicish (50000, 48000, 7);
    EngineParams p; p.bitrate = kbpsToBitrate (0, 48.0f); p.swirl = 0.6f;
    CodecEngine a, b; a.prepare (48000, 2, 4096); b.prepare (48000, 2, 4096);
    a.setParams (p); b.setParams (p); a.seed (5); b.seed (5);
    auto oa = runCodec (a, in), ob = runCodec (b, in);
    REQUIRE (oa == ob);
}

TEST_CASE ("codec output is block-size independent and finite across rates and extremes")
{
    auto in = musicish (30000, 44100, 3);
    EngineParams p; p.bitrate = 0.3f; p.swirl = 0.5f;
    CodecEngine a, b; a.prepare (44100, 2, 4096); b.prepare (44100, 2, 4096);
    a.setParams (p); b.setParams (p);
    auto oa = runCodec (a, in, 2, 1), ob = runCodec (b, in, 2, 1000);
    REQUIRE (oa == ob);
    for (float v : oa) REQUIRE (std::isfinite (v));
}

TEST_CASE ("all modes: finite output, bounded, deterministic; extreme settings")
{
    auto in = musicish (96000, 48000, 11);
    for (int mode = 0; mode < 3; ++mode)
        for (float br : { 0.0f, 0.3f, 1.0f })
            for (float gl : { 0.0f, 0.5f, 1.0f })
            {
                EngineParams p; p.codecMode = mode; p.bitrate = br; p.glitch = gl; p.swirl = 1.0f;
                CodecEngine a, b; a.prepare (48000, 2, 4096); b.prepare (48000, 2, 4096);
                a.setParams (p); b.setParams (p);
                auto oa = runCodec (a, in), ob = runCodec (b, in);
                REQUIRE (oa == ob);
                float pk = 0; for (float v : oa) { REQUIRE (std::isfinite (v)); pk = std::max (pk, std::fabs (v)); }
                INFO ("mode " << mode << " br " << br << " gl " << gl << " peak " << pk);
                REQUIRE (pk < 200.0f); // raw codec can spike in Broken; the engine limiter bounds it
            }
}

TEST_CASE ("glitch amount raises the bad-frame fraction; Stream buffering produces long gaps")
{
    auto in = musicish (48000 * 20, 48000, 12);
    double prev = -1;
    for (float gl : { 0.0f, 0.3f, 0.6f, 1.0f })
    {
        EngineParams p; p.codecMode = ModeStream; p.bitrate = 0.7f; p.glitch = gl; p.swirl = 0;
        CodecEngine c; c.prepare (48000, 2, 4096); c.setParams (p);
        runCodec (c, in);
        const double frac = c.diag.badFrames / c.diag.frames;
        INFO ("glitch " << gl << " bad fraction " << frac);
        REQUIRE (frac >= prev); prev = frac;
        if (gl == 0.0f) { REQUIRE (c.diag.badFrames == 0); REQUIRE (c.diag.bufferingFrames == 0); }
        if (gl == 1.0f) REQUIRE (c.diag.bufferingFrames > 0);
    }
}

TEST_CASE ("Stream and Broken: impulse at high bitrate lands at the reported latency")
{
    CodecEngine c; c.prepare (48000, 2, 4096);
    EngineParams p; p.codecMode = ModeStream; p.bitrate = 1.0f; p.swirl = 0; c.setParams (p);
    std::vector<float> in (30000, 0.0f); in[9000] = 0.8f;
    auto out = runCodec (c, in);
    int am = 0; for (size_t i = 0; i < out.size(); ++i) if (std::fabs (out[i]) > std::fabs (out[(size_t) am])) am = (int) i;
    REQUIRE (am == 9000 + c.getLatency());
}

TEST_CASE ("mode switching mid-stream never produces non-finite or huge jumps")
{
    auto in = musicish (48000 * 6, 48000, 13);
    CodecEngine c; c.prepare (48000, 2, 4096);
    std::vector<float> out; std::vector<float> l = in;
    EngineParams p; p.bitrate = 0.6f; p.glitch = 0.2f;
    for (size_t pos = 0, blk = 0; pos + 512 <= in.size(); pos += 512, ++blk)
    {
        p.codecMode = (int) ((blk / 40) % 3); c.setParams (p);
        const float* ip[2] = { l.data() + pos, l.data() + pos };
        float a[512], b[512]; float* op[2] = { a, b };
        c.process (ip, op, 512);
        for (float v : a) { REQUIRE (std::isfinite (v)); REQUIRE (std::fabs (v) < 200.0f); }
    }
}

// ------------------------------------------------------------------ M5: Tape
#include "dsp/Tape.h"

TEST_CASE ("tape saturation: ADAA aliases less than plain tanh on a 5 kHz sine at high drive")
{
    auto aliasEnergy = [] (bool adaa)
    {
        AdaaTanh s; s.adaa = adaa;
        const int n = 16384; std::vector<float> y ((size_t) n);
        for (int i = 0; i < n; ++i) y[(size_t) i] = s.process (10.0f * std::sin (2.0f * 3.14159265f * 5000.0f * (float) i / 48000.0f));
        juce::dsp::FFT fft (14); std::vector<float> buf ((size_t) 2 * n, 0.0f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = y[(size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / n));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double alias = 0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double f = k * 48000.0 / n, h = f / 5000.0;
            if (std::fabs (h - std::round (h)) * 5000.0 > 150.0) alias += (double) buf[(size_t) k] * buf[(size_t) k];
        }
        return alias;
    };
    const double plain = aliasEnergy (false), adaa = aliasEnergy (true);
    INFO ("plain " << plain << " adaa " << adaa);
    REQUIRE (adaa < plain);
}

TEST_CASE ("tape full chain: finite output at extremes")
{
    Tape t; t.prepare (48000, 2, 512);
    EngineParams p; p.saturation = 1.0f; p.hiss = 0.0f; p.wow = p.flutter = p.age = 1.0f; p.toneHz = 4000.0f; t.setParams (p);
    auto in = musicish (48000 * 5, 48000, 3); std::vector<float> out (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, in.size() - pos);   // the last block is partial (240000 is not a multiple of 256)
        const float* ip[2] = { in.data() + pos, in.data() + pos }; float* op[2] = { out.data() + pos, out.data() + pos };
        t.process (ip, op, n);
    }
    for (float v : out) REQUIRE (std::isfinite (v));
}
