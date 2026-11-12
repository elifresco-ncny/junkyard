// Hidden test ([.report]): prints the measured numbers quoted in docs/DECISIONS.md.  Run: build/junkyard_tests "[.report]"
#include <catch2/catch_test_macros.hpp>
#include "test_util.h"
#include "dsp/DriveCal.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>

using namespace jy; using namespace tu;
namespace
{
double rmsS (const Stereo& s, size_t from) { return 10.0 * std::log10 (0.5 * (std::pow (10.0, rmsDb (s.l, from, s.size()) / 10.0) + std::pow (10.0, rmsDb (s.r, from, s.size()) / 10.0))); }
EngineParams ap (float a) { auto p = tapeBase(); p.age = a; return p; }
}
TEST_CASE ("report v0.4 numbers", "[.report]")
{
    auto devs = [&] (float w, float f) { auto p = tapeBase(); p.wow = w; p.flutter = f; const auto x = mono2 (sine ((size_t) (30 * kFs), 1000.0, 0.5)); return relFreqDev (demod (runTape (p, x).l, 1000.0, 480), 1000.0, 0.01); };
    { auto d = devs (1, 0); std::printf ("wow100: peak %.2f %% dominant %.2f Hz\n", peakAbs (d, 200) * 100, dominantFreq (d, 100, 200)); }
    { auto d = devs (0, 1); std::printf ("flutter100: peak %.2f %% dominant %.2f Hz\n", peakAbs (d, 200) * 100, dominantFreq (d, 100, 200)); }
    { auto d = devs (0.2f, 0); std::printf ("wow20: peak %.3f %%\n", peakAbs (d, 200) * 100); }
    for (double lvl : { -10.0, -40.0 })
    {
        const auto x = mono2 (pinkNoise ((size_t) (6 * kFs), lvl, 51)); auto p = tapeBase(); p.hiss = 1; auto a = runTape (p, x).l; p.hiss = 0; auto b = runTape (p, x).l;
        for (size_t i = 0; i < a.size(); ++i) a[i] -= b[i];
        std::printf("hiss with pink %.0f dBFS: %.2f dBFS\n", lvl, rmsDb (a, (size_t) (2 * kFs), a.size()));
    }
    { auto m = corpus::make (corpus::Drums, kFs, 8.0); corpus::scaleToLufs (m, kFs, -10.0);
      for (float d : { 0.0f, 0.6f, 1.0f }) { auto p = tapeBase(); p.saturation = d; std::printf ("drums crest at Drive %.0f %%: %.2f dB (input %.2f)\n", d * 100, crest (runTape (p, m).l, (size_t) (0.5 * kFs)), crest (m.l, (size_t) (0.5 * kFs))); } }
    { double worst = 0; for (int k = 0; k < corpus::kNumKinds; ++k) { auto m = corpus::make (k, kFs, 8.0); corpus::scaleToLufs (m, kFs, drivecal::kDefaultL);
        for (int pct = 0; pct <= 100; pct += 10) { auto p = tapeBase(); p.saturation = pct * 0.01f; worst = std::max (worst, std::fabs (loudnessDelta (m, runTape (p, m), 480, (size_t) (0.5 * kFs)))); } }
      std::printf ("default curve worst |error| at kDefaultL=%.1f: %.2f dB\n", drivecal::kDefaultL, worst); }
    for (auto [lvl, age] : { std::pair<double, float> { -10, 1 }, { -45, 1 }, { -45, 0 } })
    { const auto x = mono2 (pinkNoise ((size_t) (8 * kFs), lvl, 71)); const auto o = runTape (ap (age), x, 64, kFs, TapeAge::Mistrack);
      std::printf ("mistracking pink %.0f dBFS Age %.0f: 8-12 kHz %.2f dB\n", lvl, age * 100, bandDb (o.l, 8000, 12000, (size_t) (2 * kFs)) - bandDb (x.l, 8000, 12000, (size_t) (2 * kFs) - 480)); }
    { auto m = corpus::make (corpus::FullMix, kFs, 20.0); corpus::scaleToLufs (m, kFs, -14.0); auto a0 = runTape (ap (0), m), a1 = runTape (ap (1), m);
      std::printf ("full mix > 8 kHz change at Age 100: %.2f dB\n", bandDb (a1.l, 8000, 20000, (size_t) (2 * kFs)) - bandDb (a0.l, 8000, 20000, (size_t) (2 * kFs))); }
    { auto m = corpus::make (corpus::FullMix, kFs, 16.0); corpus::scaleToLufs (m, kFs, -14.0); const size_t from = (size_t) (6 * kFs); const double sig = rmsS (m, from);
      for (float a : { 0.2f, 0.5f, 1.0f }) std::printf ("Age %.0f: halo %.1f dB, bleed %.1f dB, crackle %.1f dB re signal\n", a * 100,
          rmsS (runTape (ap (a), m, 64, kFs, TapeAge::Halo), from) - sig, rmsS (runTape (ap (a), m, 64, kFs, TapeAge::Bleed), from) - sig, rmsS (runTape (ap (a), m, 64, kFs, TapeAge::Crackle), from) - sig); }
    { double worst = 0; for (int k = 0; k < corpus::kNumKinds; ++k) for (double L : { -14.0, -30.0 }) { auto m = corpus::make (k, kFs, 10.0); corpus::scaleToLufs (m, kFs, L);
        auto a0 = runTape (ap (0), m), a1 = runTape (ap (1), m);
        const double d = loud::lufs (a1.l, a1.r, (size_t) kFs, m.size(), kFs) - loud::lufs (a0.l, a0.r, (size_t) kFs, m.size(), kFs); worst = std::max (worst, std::fabs (d));
        std::printf ("age loudness %s @%.0f: %+.2f dB\n", corpus::kindName (k), L, d); }
      std::printf ("age loudness worst %.2f dB\n", worst); }

    // halo hang
    { std::vector<float> x ((size_t) (4 * kFs), 0.0f); const size_t t0 = (size_t) (0.5 * kFs), len = (size_t) (0.2 * kFs);
      for (size_t i = 0; i < len; ++i) x[t0 + i] = (float) (std::pow (10.0, -10.0 / 20.0) * std::sin (2 * kPi * 1000.0 * (double) i / kFs));
      const auto o = runTape (ap (1), mono2 (x), 64, kFs, TapeAge::Halo); const size_t end = t0 + len + 480; const double burst = rmsDb (x, t0, t0 + len);
      std::printf ("halo hang: 300 ms after: %.1f dB re burst\n", rmsDb (o.l, end + (size_t) (0.275 * kFs), end + (size_t) (0.325 * kFs)) - burst); }
    // crackle
    { auto drums = corpus::make (corpus::Drums, kFs, 30.0); corpus::scaleToLufs (drums, kFs, -14.0);
      auto pad = corpus::detail::pad (kFs, 30.0); corpus::scaleToLufs (pad, kFs, -14.0);
      for (auto* in : { &drums, &pad }) { std::vector<uint32_t> ev; Tape t; t.prepare (kFs, 2, 512); t.setParams (ap (1)); t.ageModule().eventLog = &ev; runTape (t, *in, 64);
        std::printf ("crackle events/s on %s: %.1f\n", in == &drums ? "drums" : "pad", (double) ev.size() / 30.0); }
      auto o = runTape (ap (1), drums, 64, kFs, TapeAge::Crackle); const size_t from = (size_t) (2 * kFs);
      float pk = 0, ms = 0; for (size_t i = from + 1; i < o.size(); ++i) { pk = std::max (pk, std::fabs (o.l[i])); ms = std::max (ms, std::fabs (o.l[i] - o.l[i - 1])); }
      std::printf ("crackle on drums: level %.1f dB re signal, max step / peak %.2f\n", rmsS (o, from) - rmsS (drums, from), ms / pk); }
}
