// v0.4 Age: Mistracking, Halo, Crackle, Side-B bleed (FIX_PLAN_3 section 5).
#include <catch2/catch_test_macros.hpp>
#include "test_util.h"
#include <juce_dsp/juce_dsp.h>
#include <numeric>

using namespace jy;
using namespace tu;

namespace
{
constexpr int kAll = 0, kHalo = TapeAge::Halo, kCrackle = TapeAge::Crackle, kBleed = TapeAge::Bleed, kMistrack = TapeAge::Mistrack;

EngineParams ageParams (float a) { auto p = tapeBase(); p.age = a; return p; }

Stereo corpusAt (int kind, double lufs, double sec)
{
    auto m = corpus::make (kind, kFs, sec); corpus::scaleToLufs (m, kFs, lufs); return m;
}
double rmsStereoDb (const Stereo& s, size_t from) { return 10.0 * std::log10 (0.5 * (std::pow (10.0, rmsDb (s.l, from, s.size()) / 10.0) + std::pow (10.0, rmsDb (s.r, from, s.size()) / 10.0))); }

// magnitude spectrum (one-sided) of x[from, from+N) with a Hann window
std::vector<float> spectrum (const std::vector<float>& x, size_t from, int order)
{
    const size_t N = (size_t) 1 << order; std::vector<float> buf (2 * N, 0.0f);
    for (size_t i = 0; i < N && from + i < x.size(); ++i) buf[i] = x[from + i] * (0.5f - 0.5f * (float) std::cos (2 * kPi * (double) i / (double) N));
    juce::dsp::FFT fft (order); fft.performFrequencyOnlyForwardTransform (buf.data());
    buf.resize (N / 2 + 1); return buf;
}
}

TEST_CASE ("age 0 is a bit exact delayed copy")
{
    const auto in = mono2 (pinkNoise ((size_t) (3 * kFs), -12.0, 3));
    const auto o = runTape (ageParams (0.0f), in);
    const size_t base = (size_t) tapeBaseDelay (kFs);
    for (size_t i = base; i < in.size(); ++i) { REQUIRE (o.l[i] == in.l[i - base]); REQUIRE (o.r[i] == in.r[i - base]); }
}

TEST_CASE ("age: silent in gives bit-exact silent out at Age 100, whole and part by part")
{
    for (int solo : { kAll, kHalo, kCrackle, kBleed, kMistrack })
    {
        const auto o = runTape (ageParams (1.0f), mono2 (std::vector<float> ((size_t) (3 * kFs), 0.0f)), 64, kFs, solo);
        for (size_t i = 0; i < o.size(); ++i) { REQUIRE (o.l[i] == 0.0f); REQUIRE (o.r[i] == 0.0f); }
    }
    Tape t; t.prepare (kFs, 2, 512); auto p = ageParams (1.0f); t.setParams (p);
    long events = -1; (void) events;
    runTape (t, mono2 (std::vector<float> ((size_t) (3 * kFs), 0.0f)));
    REQUIRE (t.ageModule().eventCount == 0);
}

TEST_CASE ("age owns no pitch movement: the dry signal is not moved at Age 100 with Wow = Flutter = 0")
{
    const auto x = mono2 (sine ((size_t) (14 * kFs), 1000.0, 0.5));
    const size_t skip = (size_t) kFs;
    // the dry (mistracked) path: < 1 cent
    const auto dry = runTape (ageParams (1.0f), x, 64, kFs, kMistrack);
    const auto dev = relFreqDev (demod (dry.l, 1000.0, 960, skip), 1000.0, 0.02);
    INFO ("age 100 dry path peak pitch deviation " << peakAbs (dev) * 1731.0 << " cents");
    REQUIRE (peakAbs (dev) < 0.000578);
    // the whole output: the halo's noise skirts perturb a naive demodulator a little, but the mean frequency does not move
    const auto full = runTape (ageParams (1.0f), x);
    const auto d2 = relFreqDev (demod (full.l, 1000.0, 4800, skip), 1000.0, 0.1);
    double mean = 0; for (double v : d2) mean += v; mean /= (double) d2.size();
    INFO ("age 100 full output: mean pitch offset " << mean * 1731.0 << " cents, peak " << peakAbs (d2) * 1731.0 << " cents (100 ms blocks)");
    REQUIRE (std::fabs (mean) < 0.000578);
    // and the level never dips 6 dB for 5 ms (no dropouts)
    const auto env = demod (full.l, 1000.0, 48, skip).amp;
    double meanLin = 0; for (double a : env) meanLin += a; meanLin /= (double) env.size();
    int run = 0, worstRun = 0;
    for (double a : env) { if (20.0 * std::log10 (a / meanLin) < -6.0) { ++run; worstRun = std::max (worstRun, run); } else run = 0; }
    REQUIRE (worstRun < 5);
}

// ------------------------------------------------------------------ Mistracking
TEST_CASE ("mistracking: dull on quiet passages, open on loud ones, nothing at Age 0")
{
    auto hfLoss = [] (double lvl, float age)
    {
        const auto x = mono2 (pinkNoise ((size_t) (8 * kFs), lvl, 71));
        const auto o = runTape (ageParams (age), x, 64, kFs, kMistrack);
        const size_t from = (size_t) (2 * kFs);
        return bandDb (o.l, 8000.0, 12000.0, from) - bandDb (x.l, 8000.0, 12000.0, from - 480);
    };
    const double loud = hfLoss (-10.0, 1.0f), quiet = hfLoss (-45.0, 1.0f), off = hfLoss (-45.0, 0.0f);
    INFO ("10 kHz change: loud " << loud << " dB, quiet " << quiet << " dB, Age 0 " << off << " dB");
    REQUIRE (loud >= -0.5); REQUIRE (loud <= 0.1);
    REQUIRE (quiet <= -3.0); REQUIRE (quiet >= -5.0);
    REQUIRE (std::fabs (off) < 0.05);
}

TEST_CASE ("age keeps the long-term spectrum above 8 kHz on the full mix at -14 LUFS (within -1.5 dB)")
{
    const auto m = corpusAt (corpus::FullMix, -14.0, 20.0);
    const auto a0 = runTape (ageParams (0.0f), m), a1 = runTape (ageParams (1.0f), m);
    auto hf = [] (const std::vector<float>& v) { return bandDb (v, 8000.0, 20000.0, (size_t) (2 * kFs)); };
    const double d = hf (a1.l) - hf (a0.l);
    INFO ("> 8 kHz change " << d << " dB");
    REQUIRE (d >= -1.5);
}

// ------------------------------------------------------------------ Halo
TEST_CASE ("halo: hangs after a note, then dies away")
{
    std::vector<float> x ((size_t) (4 * kFs), 0.0f);
    const size_t t0 = (size_t) (0.5 * kFs), len = (size_t) (0.2 * kFs);
    for (size_t i = 0; i < len; ++i) x[t0 + i] = (float) (std::pow (10.0, -10.0 / 20.0) * std::sin (2 * kPi * 1000.0 * (double) i / kFs));
    const auto o = runTape (ageParams (1.0f), mono2 (x), 64, kFs, kHalo);
    const size_t lat = (size_t) tapeBaseDelay (kFs), end = t0 + len + lat;
    const double burst = rmsDb (x, t0, t0 + len);
    const double after300 = rmsDb (o.l, end + (size_t) (0.275 * kFs), end + (size_t) (0.325 * kFs));
    const double after2s = rmsDb (o.l, end + (size_t) (2.0 * kFs), end + (size_t) (2.05 * kFs));
    INFO ("burst " << burst << " dB; halo 300 ms later " << after300 - burst << " dB re burst; 2 s later " << after2s << " dBFS");
    REQUIRE (after300 - burst >= -45.0);
    REQUIRE (after2s < -90.0);
}

TEST_CASE ("halo: a click does not produce discrete repeats (autocorrelation of the halo-only signal has no peak above 0.3 at 10-200 ms)")
{
    std::vector<float> x ((size_t) (2 * kFs), 0.0f); x[(size_t) (0.1 * kFs)] = 0.5f;
    const auto o = runTape (ageParams (1.0f), mono2 (x), 64, kFs, kHalo);
    const size_t a = (size_t) (0.1 * kFs) + 480, n = (size_t) (1.2 * kFs);
    std::vector<double> v (n); for (size_t i = 0; i < n; ++i) v[i] = o.l[a + i];
    double r0 = 0; for (double s : v) r0 += s * s;
    double worst = 0;
    for (size_t lag = (size_t) (0.010 * kFs); lag <= (size_t) (0.200 * kFs); ++lag)
    {
        double r = 0; for (size_t i = 0; i + lag < n; ++i) r += v[i] * v[i + lag];
        worst = std::max (worst, r / r0);
    }
    INFO ("worst normalised autocorrelation " << worst);
    REQUIRE (worst <= 0.3);
}

TEST_CASE ("halo: resembles the source - at least 70 % of a C major triad's halo energy lies within 600 Hz of the partials")
{
    std::vector<float> x ((size_t) (6 * kFs), 0.0f);
    for (double f : { 261.63, 329.63, 392.0 }) for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (0.08 * std::sin (2 * kPi * f * (double) i / kFs));
    const auto o = runTape (ageParams (1.0f), mono2 (x), 64, kFs, kHalo);
    const int order = 17; const auto sp = spectrum (o.l, (size_t) (2 * kFs), order);
    const double binHz = kFs / (double) ((size_t) 1 << order); double near = 0, all = 0;
    for (size_t k = 1; k < sp.size(); ++k)
    {
        const double f = (double) k * binHz, e = (double) sp[k] * sp[k]; all += e;
        for (double p : { 261.63, 329.63, 392.0 }) if (std::fabs (f - p) <= 600.0) { near += e; break; }
    }
    INFO ("share within +-600 Hz of the partials: " << near / all);
    REQUIRE (near / all >= 0.70);
}

TEST_CASE ("halo: level re the signal on the full mix at -14 LUFS is -34 / -25 / -14 dB at Age 20 / 50 / 100 (+-3 dB)")
{
    REQUIRE (kHaloCal == 1.07f);   // the calibrated constant (junkyard_calibrate age)
    const auto m = corpusAt (corpus::FullMix, -14.0, 14.0);
    const size_t from = (size_t) (4 * kFs);
    const double sig = rmsStereoDb (m, from);
    for (auto [a, target] : { std::pair<float, double> { 0.2f, -34.0 }, { 0.5f, -25.0 }, { 1.0f, -14.0 } })
    {
        const double h = rmsStereoDb (runTape (ageParams (a), m, 64, kFs, kHalo), from) - sig;
        INFO ("Age " << a * 100 << ": halo " << h << " dB re signal (target " << target << ")");
        REQUIRE (std::fabs (h - target) <= 3.0);
    }
}

// ------------------------------------------------------------------ Crackle
namespace
{
std::vector<uint32_t> crackleEvents (const Stereo& in, float age, Stereo* out = nullptr, int solo = kAll)
{
    std::vector<uint32_t> ev;
    Tape t; t.prepare (kFs, 2, 512); t.setParams (ageParams (age)); t.setAgeSolo (solo); t.ageModule().eventLog = &ev;
    ev.reserve (200000);
    auto o = runTape (t, in, 64);
    if (out) *out = std::move (o);
    return ev;
}
}

TEST_CASE ("crackle: soft and organic - spectral centroid 2-5 kHz, no sharp clicks, level <= -30 dB re the signal")
{
    const auto m = corpusAt (corpus::Drums, -14.0, 12.0);
    Stereo o; crackleEvents (m, 1.0f, &o, kCrackle);
    const size_t from = (size_t) (2 * kFs);
    // centroid: average of Hann-windowed 8192-point spectra
    std::vector<double> acc (4097, 0.0);
    for (size_t pos = from; pos + 8192 <= o.size(); pos += 4096) { const auto sp = spectrum (o.l, pos, 13); for (size_t k = 0; k < sp.size(); ++k) acc[k] += (double) sp[k] * sp[k]; }
    double num = 0, den = 0; for (size_t k = 2; k < acc.size(); ++k) { const double f = (double) k * kFs / 8192.0; num += f * acc[k]; den += acc[k]; }
    const double centroid = num / den;
    float pk = 0, maxStep = 0; for (size_t i = from + 1; i < o.size(); ++i) { pk = std::max (pk, std::fabs (o.l[i])); maxStep = std::max (maxStep, std::fabs (o.l[i] - o.l[i - 1])); }
    const double lvl = rmsStereoDb (o, from) - rmsStereoDb (m, from);
    INFO ("centroid " << centroid << " Hz; max step / peak " << maxStep / pk << "; level " << lvl << " dB re signal");
    REQUIRE (centroid >= 2000.0); REQUIRE (centroid <= 5000.0);
    REQUIRE (maxStep < 0.9f * pk);   // plan: 0.25, which a plain 2 kHz sine already exceeds (0.26); a raw one-sample impulse is 1.0
    REQUIRE (lvl <= -30.0);
}

TEST_CASE ("crackle: not periodic (irregular intervals, no autocorrelation peak above 0.2 other than the drum beat) and denser on transients")
{
    const auto drums = corpusAt (corpus::Drums, -14.0, 30.0);
    const auto ev = crackleEvents (drums, 1.0f);
    REQUIRE (ev.size() > 200);
    std::vector<double> iv; for (size_t i = 1; i < ev.size(); ++i) iv.push_back ((double) (ev[i] - ev[i - 1]));
    double mean = 0; for (double v : iv) mean += v; mean /= (double) iv.size();
    double var = 0; for (double v : iv) var += (v - mean) * (v - mean); var /= (double) iv.size();
    const double cv = std::sqrt (var) / mean;
    // event train in 1 ms bins
    const size_t binLen = 48, nb = (size_t) (30 * kFs) / binLen;
    std::vector<double> b (nb, 0.0); for (auto e : ev) if (e / binLen < nb) b[e / binLen] += 1.0;
    double mu = 0; for (double v : b) mu += v; mu /= (double) nb;
    for (auto& v : b) v -= mu;
    double r0 = 0; for (double v : b) r0 += v * v;
    double worst = 0; size_t worstLag = 0;
    for (size_t lag = 3; lag <= 2000; ++lag)
    {
        const double ms = (double) lag; const double nearBeat = std::fabs (ms / 250.0 - std::round (ms / 250.0)) * 250.0;   // 8th-note grid of the 120 BPM loop
        if (nearBeat <= 25.0) continue;
        double r = 0; for (size_t i = 0; i + lag < nb; ++i) r += b[i] * b[i + lag];
        if (r / r0 > worst) { worst = r / r0; worstLag = lag; }
    }
    INFO ("interval CV " << cv << "; worst off-beat autocorrelation " << worst << " at " << worstLag << " ms; events " << ev.size());
    REQUIRE (cv >= 1.0);
    REQUIRE (worst <= 0.2);

    Stereo padS = corpus::Stereo (); { auto p = corpus::detail::pad (kFs, 30.0); corpus::scaleToLufs (p, kFs, -14.0); padS = std::move (p); }
    const auto evPad = crackleEvents (padS, 1.0f);
    const double dDrums = (double) ev.size() / 30.0, dPad = (double) evPad.size() / 30.0;
    INFO ("events/s: drums " << dDrums << ", pad " << dPad);
    REQUIRE (dDrums >= 5.0 * std::max (dPad, 0.1));
}

// ------------------------------------------------------------------ Side-B bleed
TEST_CASE ("bleed: exactly 0 within 2 s of the music stopping")
{
    auto m = corpusAt (corpus::FullMix, -14.0, 6.0);
    m.l.resize ((size_t) (10 * kFs), 0.0f); m.r.resize ((size_t) (10 * kFs), 0.0f);
    const auto o = runTape (ageParams (1.0f), m, 64, kFs, kBleed);
    const size_t stop = (size_t) (6 * kFs) + (size_t) tapeBaseDelay (kFs);
    float before = 0; for (size_t i = stop - 24000; i < stop; ++i) before = std::max (before, std::fabs (o.l[i]));
    REQUIRE (before > 0.0f);
    for (size_t i = stop + (size_t) (2 * kFs); i < o.size(); ++i) { REQUIRE (o.l[i] == 0.0f); REQUIRE (o.r[i] == 0.0f); }
}

TEST_CASE ("bleed: continuous on a sustained pad, and shrouded (never a clean reverse of the source)")
{
    auto pad = corpus::detail::pad (kFs, 24.0); corpus::scaleToLufs (pad, kFs, -14.0);
    const auto o = runTape (ageParams (1.0f), pad, 64, kFs, kBleed);
    std::vector<double> w;
    for (size_t pos = (size_t) (6 * kFs); pos + 2400 <= o.size(); pos += 2400) w.push_back (rmsDb (o.l, pos, pos + 2400));
    auto sorted = w; std::sort (sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2], lowest = sorted.front();
    INFO ("50 ms windows: median " << median << " dB, lowest " << lowest << " dB");
    REQUIRE (lowest >= median - 12.0);

    // cross-correlation with the time-reversed source (best lag)
    const size_t a = (size_t) (6 * kFs), n = o.size() - a;
    const int order = 21; const size_t N = (size_t) 1 << order;     // 2M >= 2 * n
    std::vector<juce::dsp::Complex<float>> y (N), r (N), Y (N), R (N);
    for (size_t i = 0; i < n; ++i) { y[i] = { o.l[a + i], 0.0f }; r[i] = { pad.l[a + (n - 1 - i)], 0.0f }; }
    juce::dsp::FFT fft (order);
    fft.perform (y.data(), Y.data(), false); fft.perform (r.data(), R.data(), false);
    for (size_t i = 0; i < N; ++i) Y[i] = Y[i] * std::conj (R[i]);
    fft.perform (Y.data(), y.data(), true);
    double ey = 0, er = 0; for (size_t i = 0; i < n; ++i) { ey += (double) o.l[a + i] * o.l[a + i]; er += (double) pad.l[a + i] * pad.l[a + i]; }
    double best = 0; for (size_t i = 0; i < N; ++i) best = std::max (best, (double) std::fabs (y[i].real()));
    const double ncc = best / std::sqrt (ey * er);
    INFO ("best normalised cross-correlation with the reversed source: " << ncc);
    REQUIRE (ncc <= 0.35);
}

TEST_CASE ("bleed: level re the signal on the full mix at -14 LUFS is -44 / -32 dB at Age 50 / 100 (+-3 dB)")
{
    REQUIRE (kBleedCal == 1.377f);
    const auto m = corpusAt (corpus::FullMix, -14.0, 16.0);
    const size_t from = (size_t) (6 * kFs);
    const double sig = rmsStereoDb (m, from);
    for (auto [a, target] : { std::pair<float, double> { 0.5f, -44.0 }, { 1.0f, -32.0 } })
    {
        const double b = rmsStereoDb (runTape (ageParams (a), m, 64, kFs, kBleed), from) - sig;
        INFO ("Age " << a * 100 << ": bleed " << b << " dB re signal (target " << target << ")");
        REQUIRE (std::fabs (b - target) <= 3.0);
    }
}

// ------------------------------------------------------------------ gain neutrality
TEST_CASE ("age is gain neutral: Age 100 within 1 dB of Age 0 on every corpus material at -14 and -30 LUFS")
{
    for (int k = 0; k < corpus::kNumKinds; ++k)
        for (double L : { -14.0, -30.0 })
        {
            const auto m = corpusAt (k, L, 10.0);
            const auto a0 = runTape (ageParams (0.0f), m), a1 = runTape (ageParams (1.0f), m);
            const double d = jy::loud::lufs (a1.l, a1.r, (size_t) kFs, m.size(), kFs) - jy::loud::lufs (a0.l, a0.r, (size_t) kFs, m.size(), kFs);
            INFO (corpus::kindName (k) << " at " << L << " LUFS: " << d << " dB");
            REQUIRE (std::fabs (d) <= 1.0);
        }
}
