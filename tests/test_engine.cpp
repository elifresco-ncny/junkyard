#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/JunkyardEngine.h"
#include <random>

using namespace jy;

namespace
{
// Run `x` (mono duplicated to nch) through the engine using the given block sizes, return left channel.
std::vector<float> run (JunkyardEngine& e, const std::vector<float>& x, int nch, const std::vector<int>& blocks, bool bypass = false)
{
    std::vector<float> l = x, r = x, out;
    out.reserve (x.size());
    size_t pos = 0, bi = 0;
    while (pos < x.size())
    {
        int n = blocks[bi++ % blocks.size()];
        n = (int) std::min<size_t> ((size_t) n, x.size() - pos);
        float* p[2] = { l.data() + pos, r.data() + pos };
        (void) nch;
        if (bypass) e.processBypassed (p, n); else e.process (p, n);
        out.insert (out.end(), l.begin() + (long) pos, l.begin() + (long) pos + n);
        pos += (size_t) n;
    }
    return out;
}
int argmaxAbs (const std::vector<float>& v)
{
    int bi = 0; float bm = 0;
    for (size_t i = 0; i < v.size(); ++i) if (std::fabs (v[i]) > bm) { bm = std::fabs (v[i]); bi = (int) i; }
    return bi;
}
std::vector<float> impulse (int len, int at, float amp = 0.5f) { std::vector<float> v ((size_t) len, 0.0f); v[(size_t) at] = amp; return v; }
std::vector<float> noise (int len, float amp, unsigned seed = 1)
{
    std::mt19937 g (seed); std::uniform_real_distribution<float> d (-amp, amp);
    std::vector<float> v ((size_t) len); for (auto& s : v) s = d (g); return v;
}
} // namespace

TEST_CASE ("latency equals measured impulse position: all section combos, rates, block sizes")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        for (int combo = 0; combo < 8; ++combo)
        {
            for (int order = 0; order < 2; ++order)
            {
                EngineParams p;
                p.codecOn = combo & 1; p.tapeOn = combo & 4; p.order = order;
                p.bitrate = 1.0f; p.swirl = 0; p.saturation = 0; p.wow = p.flutter = p.age = p.saturation = p.hiss = 0;
                JunkyardEngine e; e.setParams (p); e.prepare (sr, 4096, 2);
                const int L = e.getLatencySamples();
                const int at = 3000;
                auto in = impulse (at + L + 6000, at);
                for (int bs : { 1, 7, 64, 511, 512, 4096 })
                {
                    e.reset();
                    auto out = run (e, in, 2, { bs });
                    INFO ("sr " << sr << " combo " << combo << " order " << order << " bs " << bs);
                    REQUIRE (argmaxAbs (out) == at + L);
                }
            }
        }
    }
}

TEST_CASE ("latency is independent of parameters")
{
    EngineParams a, b; b.codecMode = 2; b.bitrate = 0; b.tapeOn = true; b.width = 0.3f; b.lowKeep = true;
    JunkyardEngine e1, e2; e1.setParams (a); e2.setParams (b);
    e1.prepare (48000, 512, 2); e2.prepare (48000, 512, 2);
    REQUIRE (e1.getLatencySamples() == e2.getLatencySamples());
    REQUIRE (e1.getLatencySamples() == 1024 + 480);
}

TEST_CASE ("mix 0 and bypass equal delayed input")
{
    auto in = noise (20000, 0.5f);
    for (double sr : { 44100.0, 48000.0 })
    {
        EngineParams p; p.mix = 0.0f; p.tapeOn = true; p.lowKeep = true;
        JunkyardEngine e; e.setParams (p); e.prepare (sr, 512, 2);
        const int L = e.getLatencySamples();
        for (bool bypass : { false, true })
        {
            e.reset();
            auto out = run (e, in, 2, { 100, 37, 512 }, bypass);
            double maxErr = 0;
            for (size_t i = (size_t) L; i < in.size(); ++i) maxErr = std::max (maxErr, (double) std::fabs (out[i] - in[i - (size_t) L]));
            INFO ("bypass " << bypass);
            REQUIRE (maxErr < 1e-6);
        }
    }
}

TEST_CASE ("output is block-size independent")
{
    auto in = noise (30000, 0.4f, 5);
    EngineParams p; p.lowKeep = true; p.width = 0.5f; p.inputGainDb = 3; p.outputGainDb = -2; p.mix = 0.7f;
    JunkyardEngine e; e.setParams (p); e.prepare (48000, 4096, 2);
    e.reset(); auto a = run (e, in, 2, { 512 });
    e.reset(); auto b = run (e, in, 2, { 1 });
    e.reset(); auto c = run (e, in, 2, { 37, 1000, 3 });
    for (size_t i = 0; i < in.size(); ++i) { REQUIRE (std::fabs (a[i] - b[i]) < 1e-5f); REQUIRE (std::fabs (a[i] - c[i]) < 1e-5f); }
}

TEST_CASE ("fuzz: random params, no NaN, peak below ceiling")
{
    std::mt19937 g (42);
    auto rnd = [&] (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (g); };
    for (int nch : { 1, 2 })
    for (int signal = 0; signal < 2; ++signal)
    {
        JunkyardEngine e; e.prepare (48000, 512, nch);
        std::vector<float> buf (512), buf2 (512);
        float peak = 0;
        for (int blk = 0; blk < 48000 * 10 / 512; ++blk)
        {
            EngineParams p;
            p.inputGainDb = rnd (-24, 24); p.outputGainDb = rnd (-24, 0);
            p.codecOn = g() & 1; p.codecMode = (int) (g() % 3); p.bitrate = rnd (0, 1); p.swirl = rnd (0, 1);
            p.hfLoss = rnd (0, 1); p.glitch = rnd (0, 1); p.tapeOn = g() & 1; p.order = (int) (g() & 1);
            p.width = rnd (0, 1.5f); p.lowKeep = g() & 1; p.lowKeepFreq = rnd (40, 400); p.mix = rnd (0, 1);
            e.setParams (p);
            for (size_t i = 0; i < buf.size(); ++i) buf[i] = buf2[i] = signal ? 0.0f : rnd (-0.5f, 0.5f);
            float* ptr[2] = { buf.data(), buf2.data() };
            e.process (ptr, 512);
            for (int c = 0; c < nch; ++c) for (float s : { ptr[c][0], ptr[c][100], ptr[c][511] }) REQUIRE (std::isfinite (s));
            for (size_t i = 0; i < 512; ++i) { REQUIRE (std::isfinite (buf[i])); peak = std::max (peak, std::fabs (buf[i])); }
        }
        REQUIRE (peak <= SafetyLimiter::ceiling);
    }
}

TEST_CASE ("safety limiter bounds and passes NaN guard")
{
    SafetyLimiter l; float x = 5.0f; REQUIRE_FALSE (l.process (x)); REQUIRE (x <= SafetyLimiter::ceiling);
    x = 0.5f; l.process (x); REQUIRE (x == 0.5f);
    x = std::nanf (""); REQUIRE (l.process (x)); REQUIRE (x == 0.0f);
}

// ------------------------------------------------------------------ determinism
TEST_CASE ("identical output after reset (reseed) with every section on, any block size")
{
    auto in = noise (60000, 0.3f, 21);
    EngineParams p; p.tapeOn = true; p.glitch = 0.3f; p.codecMode = 1;
    p.wow = 0.5f; p.age = 0.6f; p.toneHz = 9000.0f; p.swirl = 0.6f;
    JunkyardEngine e; e.setParams (p); e.prepare (48000, 512, 2);
    e.reset(); auto a = run (e, in, 2, { 512 });
    e.reset(); auto b = run (e, in, 2, { 97, 300 });
    REQUIRE (a == b);
    bool differs = false; for (size_t i = 0; i < a.size(); ++i) differs |= (a[i] != in[i]);
    REQUIRE (differs);
}
