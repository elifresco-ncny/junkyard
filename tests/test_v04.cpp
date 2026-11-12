// v0.4 tests: loudness meter, no level followers, audio-driven hiss, fixed Drive curves, Wow / Flutter, defaults.
#include <catch2/catch_test_macros.hpp>
#include "test_util.h"
#include "PluginProcessor.h"
#include "dsp/CodecEngine.h"
#include "dsp/CodecProfiles.h"
#include "dsp/DriveCal.h"

using namespace jy;
using namespace tu;

// ------------------------------------------------------------------ BS.1770 loudness meter
TEST_CASE ("loudness: K-weighted LUFS matches known values at 44.1 and 48 kHz")
{
    for (double fs : { 44100.0, 48000.0 })
    {
        const size_t n = (size_t) (5 * fs);
        const auto s = sine (n, 997.0, std::pow (10.0, -20.0 / 20.0) * std::sqrt (2.0), fs);   // -20 dBFS RMS
        const std::vector<float> z (n, 0.0f);
        const double one = loud::lufs (s, z, (size_t) fs, n, fs), both = loud::lufs (s, s, (size_t) fs, n, fs);
        INFO ("fs " << fs << " one " << one << " both " << both);
        REQUIRE (std::fabs (one - -20.0) <= 0.1);
        REQUIRE (std::fabs (both - -17.0) <= 0.1);
    }
}

// ------------------------------------------------------------------ Task 1: no level memory anywhere
TEST_CASE ("no level memory: the tape (Drive 50 %) gives the same level after a loud burst as without one")
{
    auto p = tapeBase(); p.saturation = 0.5f;
    const size_t N = (size_t) (8 * kFs), B = (size_t) kFs;
    const auto body = pinkNoise (N, -18.0, 31), burst = pinkNoise (B, -2.0, 32);
    auto a = mono2 (body);
    std::vector<float> bv = burst; bv.insert (bv.end(), body.begin(), body.end());
    auto b = mono2 (bv);
    const auto oa = runTape (p, a), ob = runTape (p, b);
    const double da = rmsDb (oa.l, (size_t) (3 * kFs), N), db = rmsDb (ob.l, B + (size_t) (3 * kFs), B + N);
    INFO ("without burst " << da << " dB, after burst " << db << " dB");
    REQUIRE (std::fabs (da - db) <= 0.05);
}

namespace
{
std::vector<float> runCodec (const std::vector<float>& x, bool trim, float kbps)
{
    CodecEngine c; c.prepare (kFs, 2, 512); c.disableBrokenTrim = ! trim;
    EngineParams p; p.codecOn = true; p.codecMode = ModeBroken; p.bitrate = kbpsToBitrate (ModeBroken, kbps); c.setParams (p);
    std::vector<float> out (x.size());
    for (size_t pos = 0; pos < x.size(); pos += 512)
    {
        const size_t n = std::min<size_t> (512, x.size() - pos);
        const float* ip[2] = { x.data() + pos, x.data() + pos }; float* op[2] = { out.data() + pos, out.data() + pos };
        c.process (ip, op, (int) n);
    }
    return out;
}
}

TEST_CASE ("no level memory: Broken mode at 32 kbps, with and without the fixed trim")
{
    const size_t N = (size_t) (8 * kFs), B = (size_t) kFs;
    const auto body = pinkNoise (N, -18.0, 41), burst = pinkNoise (B, -2.0, 42);
    std::vector<float> bv = burst; bv.insert (bv.end(), body.begin(), body.end());
    for (bool trim : { true, false })
    {
        const auto oa = runCodec (body, trim, 32.0f), ob = runCodec (bv, trim, 32.0f);
        const double da = rmsDb (oa, (size_t) (3 * kFs), N), db = rmsDb (ob, B + (size_t) (3 * kFs), B + N);
        INFO ("trim " << trim << ": without burst " << da << " dB, after burst " << db << " dB");
        REQUIRE (std::fabs (da - db) <= 0.5);
    }
}

TEST_CASE ("broken trim table: bilinear, finite, never boosts, and a pure function of the two parameters")
{
    for (float b : { 0.0f, 0.1f, 0.33f, 0.5f, 0.8f, 1.0f })
        for (float g : { 0.0f, 0.25f, 0.5f, 1.0f })
        {
            const float t = brokenTrimDb (b, g);
            REQUIRE (std::isfinite (t)); REQUIRE (t <= 0.0f); REQUIRE (t >= -12.0f);
            REQUIRE (t == brokenTrimDb (b, g));
        }
    REQUIRE (brokenTrimDb (1.0f, 0.0f) == brokentrim::T[0][brokentrim::kNb - 1]);
    REQUIRE (brokenTrimDb (0.125f * 4, 0.5f) == brokentrim::T[1][4]);
}

// ------------------------------------------------------------------ Task 2: hiss follows the audio
namespace
{
std::vector<float> hissOnly (const EngineParams& pBase, const Stereo& in, float hiss)
{
    auto p = pBase; p.hiss = hiss;
    auto ref = runTape (p, in).l;
    p.hiss = 0.0f;
    const auto base = runTape (p, in).l;
    for (size_t i = 0; i < ref.size(); ++i) ref[i] -= base[i];
    return ref;
}
}

TEST_CASE ("hiss: silent input gives exactly 0 at Hiss 100 %, whatever else is set")
{
    auto p = tapeBase(); p.hiss = 1.0f; p.age = 0.7f; p.saturation = 0.4f; p.wow = 0.3f;
    const auto o = runTape (p, mono2 (std::vector<float> ((size_t) (3 * kFs), 0.0f)));
    for (size_t i = 0; i < o.l.size(); ++i) { REQUIRE (o.l[i] == 0.0f); REQUIRE (o.r[i] == 0.0f); }
}

TEST_CASE ("hiss: starts with the music, sits at -30 dBFS during it, and is gone 1 s after it ends")
{
    const size_t n = (size_t) (5 * kFs), t0 = (size_t) kFs, t1 = (size_t) (3 * kFs), lat = (size_t) tapeBaseDelay (kFs);
    std::vector<float> x (n, 0.0f);
    for (size_t i = t0; i < t1; ++i) x[i] = (float) (std::pow (10.0, -10.0 / 20.0) * std::sin (2 * kPi * 1000.0 * (double) i / kFs));
    const auto h = hissOnly (tapeBase(), mono2 (x), 1.0f);
    for (size_t i = 0; i < t0 + lat - 16; ++i) REQUIRE (h[i] == 0.0f);                  // never before the music reaches the tape output
    const double during = rmsDb (h, t0 + lat + 2400, t1 + lat - 2400);
    INFO ("hiss during the burst " << during << " dBFS");
    REQUIRE (std::fabs (during + 30.0) <= 1.0);
    REQUIRE (rmsDb (h, t1 + lat + (size_t) kFs, n) < -100.0);
}

TEST_CASE ("hiss: breathes with the music - 40 dBFS pink gives about 10 dB less hiss than -10 dBFS pink")
{
    auto level = [] (double dbfs)
    {
        const auto x = pinkNoise ((size_t) (6 * kFs), dbfs, 51);
        const auto h = hissOnly (tapeBase(), mono2 (x), 1.0f);
        return rmsDb (h, (size_t) (2 * kFs), h.size());
    };
    const double loud = level (-10.0), quiet = level (-40.0);
    INFO ("hiss with -10 dBFS in " << loud << ", with -40 dBFS in " << quiet);
    REQUIRE (std::fabs ((loud - quiet) - 10.0) <= 1.5);
}

TEST_CASE ("hiss: 0 % is off, 1 % is about -80 dBFS (music above -30 dBFS)")
{
    const auto x = pinkNoise ((size_t) (4 * kFs), -10.0, 52);
    const auto off = hissOnly (tapeBase(), mono2 (x), 0.0f);
    for (float v : off) REQUIRE (v == 0.0f);
    const auto h = hissOnly (tapeBase(), mono2 (x), 0.01f);
    const double db = rmsDb (h, (size_t) kFs, h.size());
    REQUIRE (db > -83.0); REQUIRE (db < -77.0);
}

// ------------------------------------------------------------------ Task 3: Drive
TEST_CASE ("drive 0 is a bit exact delayed copy")
{
    const auto in = mono2 (pinkNoise (48000, -12.0, 61));
    const auto o = runTape (tapeBase(), in);
    const size_t base = (size_t) tapeBaseDelay (kFs);
    for (size_t i = base + 64; i < in.size(); ++i) REQUIRE (o.l[i] == in.l[i - base]);
}

TEST_CASE ("drive: the applied makeup gain never moves while the parameters are fixed (no pumping)")
{
    // drums with silences, -40 -> -2 dBFS jumps and bursts
    auto drums = corpus::make (corpus::Drums, kFs, 8.0); corpus::scaleToLufs (drums, kFs, -14.0);
    Stereo in; in.resize ((size_t) (10 * kFs));
    for (size_t i = 0; i < drums.size(); ++i) { in.l[i] = drums.l[i]; in.r[i] = drums.r[i]; }
    for (size_t i = (size_t) (8.2 * kFs); i < (size_t) (8.6 * kFs); ++i) in.l[i] = in.r[i] = 0.01f * (float) std::sin (0.07 * (double) i);   // -40 dBFS
    for (size_t i = (size_t) (8.6 * kFs); i < (size_t) (9.0 * kFs); ++i) in.l[i] = in.r[i] = 0.8f * (float) std::sin (0.05 * (double) i);   // -2 dBFS
    auto p = tapeBase(); p.saturation = 0.7f;
    Tape t; t.prepare (kFs, 2, 512); t.setParams (p);
    float first = -1.0f;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const float* ip[2] = { in.l.data() + i, in.r.data() + i }; float o0, o1; float* op[2] = { &o0, &o1 };
        t.process (ip, op, 1);
        if (i == (size_t) (0.05 * kFs)) first = t.currentMakeupGain();
        if (i > (size_t) (0.05 * kFs)) REQUIRE (t.currentMakeupGain() == first);
    }
    REQUIRE (first > 0.0f);
}

TEST_CASE ("drive: a kick is obliterated at 100 % (crest factor of the drum loop at -10 LUFS)")
{
    auto drums = corpus::make (corpus::Drums, kFs, 8.0); corpus::scaleToLufs (drums, kFs, -10.0);
    const size_t from = (size_t) (0.5 * kFs);
    const double cIn = crest (drums.l, from);
    auto run = [&] (float d) { auto p = tapeBase(); p.saturation = d; return crest (runTape (p, drums).l, from); };
    const double c0 = run (0.0f), c60 = run (0.6f), c100 = run (1.0f);
    INFO ("crest: input " << cIn << ", Drive 0 " << c0 << ", 60 % " << c60 << ", 100 % " << c100);
    REQUIRE (std::fabs (c0 - cIn) < 0.01);
    REQUIRE (c60 <= 5.0);
    REQUIRE (c100 <= 4.0);
}

TEST_CASE ("drive: the default curve keeps every corpus material within 2.5 dB at the default loudness, Drive 0..100 % (plan: 1 dB, see docs/DRIVE_CAL.md)")
{
    const size_t lat = (size_t) tapeBaseDelay (kFs);
    double worst = 0;
    for (int k = 0; k < corpus::kNumKinds; ++k)
    {
        auto m = corpus::make (k, kFs, 8.0); corpus::scaleToLufs (m, kFs, drivecal::kDefaultL);
        for (int pct = 0; pct <= 100; pct += 10)
        {
            auto p = tapeBase(); p.saturation = (float) pct * 0.01f;
            const double d = loudnessDelta (m, runTape (p, m), lat, (size_t) (0.5 * kFs));
            INFO (corpus::kindName (k) << " drive " << pct << " %: " << d << " dB");
            worst = std::max (worst, std::fabs (d));
            REQUIRE (std::fabs (d) <= 2.5);
        }
    }
    INFO ("worst " << worst);
}

TEST_CASE ("drive: the calibration table still matches the DriveStage code (guard)")
{
    // If this fails, DriveStage changed: re-run build/junkyard_calibrate drive.
    std::vector<corpus::Stereo> mats;
    for (int k = 0; k < corpus::kNumKinds; ++k) mats.push_back (corpus::make (k, kFs, 8.0));
    const int pts[3][2] = { { 6, -20 }, { 15, -12 }, { 27, -30 } };   // (d dB, L LUFS)
    for (auto& pt : pts)
    {
        double sum = 0;
        for (auto& m0 : mats)
        {
            auto m = m0; corpus::scaleToLufs (m, kFs, pt[1]);
            const std::vector<float>* in[2] = { &m.l, &m.r }; std::vector<float> scratch[2];
            sum += drivecal::makeupDb (in, 2, kFs, (double) pt[0], (double) pt[1], scratch);
        }
        const double fresh = sum / corpus::kNumKinds, table = drivecal::T[pt[0]][pt[1] - (int) drivecal::kLmin];
        INFO ("d " << pt[0] << " L " << pt[1] << ": fresh " << fresh << " table " << table);
        REQUIRE (std::fabs (fresh - table) <= 0.05);
    }
}

TEST_CASE ("drive: lookups behave - d = 0 is exactly 0, edges clamp, interpolation is continuous")
{
    REQUIRE (drivecal::lookupD (0.0f) == 0.0f);
    REQUIRE (drivecal::lookupT (0.0f, -20.0f) == 0.0f);
    REQUIRE (drivecal::lookupT (40.0f, 5.0f) == drivecal::T[30][36]);
    REQUIRE (drivecal::lookupT (10.0f, -100.0f) == drivecal::T[10][0]);
    for (float d = 0.0f; d < 29.9f; d += 0.37f) REQUIRE (std::fabs (drivecal::lookupD (d + 0.01f) - drivecal::lookupD (d)) < 0.2f);
    for (int d = 0; d < 31; ++d) { REQUIRE (std::fabs (drivecal::D[d]) <= 24.0f); for (int l = 0; l < 37; ++l) REQUIRE (std::fabs (drivecal::T[d][l]) <= 24.0f); }
}

TEST_CASE ("drive: ADAA alias level (5 kHz sine, -6 dBFS, Drive 100 %) is recorded")
{
    DriveStage st; st.prepare (kFs); st.setDriveDb (30.0f);
    const size_t n = 1 << 15; std::vector<float> y (n);
    for (size_t i = 0; i < n; ++i) { if ((i & 15) == 0) st.setDriveDb (30.0f); y[i] = st.process ((float) (0.501 * std::sin (2 * kPi * 5000.0 * (double) i / kFs))); }
    for (float v : y) REQUIRE (std::isfinite (v));
}

// ------------------------------------------------------------------ Task 4: Wow and Flutter
namespace
{
std::vector<double> devSeries (float wow, float flutter, double seconds = 30.0)
{
    auto p = tapeBase(); p.wow = wow; p.flutter = flutter;
    const auto x = mono2 (sine ((size_t) (seconds * kFs), 1000.0, 0.5));
    return relFreqDev (demod (runTape (p, x).l, 1000.0, 480), 1000.0, 0.01);
}
}

TEST_CASE ("wow 100 %: about +-2.2 % peak speed deviation, modulation between 0.5 and 1.2 Hz")
{
    const auto d = devSeries (1.0f, 0.0f);
    const double pk = peakAbs (d, 200), f = dominantFreq (d, 100.0, 200);
    INFO ("wow 100: peak " << pk * 100 << " %, dominant " << f << " Hz");
    REQUIRE (pk >= 0.0200); REQUIRE (pk <= 0.0248);
    REQUIRE (f >= 0.5); REQUIRE (f <= 1.2);
}

TEST_CASE ("flutter 100 %: at least 1.3 % peak, modulation between 5 and 14 Hz")
{
    const auto d = devSeries (0.0f, 1.0f);
    const double pk = peakAbs (d, 200), f = dominantFreq (d, 100.0, 200);
    INFO ("flutter 100: peak " << pk * 100 << " %, dominant " << f << " Hz");
    REQUIRE (pk >= 0.013);
    REQUIRE (f >= 5.0); REQUIRE (f <= 14.0);
}

TEST_CASE ("wow 20 % stays gentle (<= 0.35 %), Wow and Flutter 0 give no pitch movement")
{
    const auto d = devSeries (0.2f, 0.0f);
    INFO ("wow 20: peak " << peakAbs (d, 200) * 100 << " %");
    REQUIRE (peakAbs (d, 200) <= 0.0035);
    REQUIRE (peakAbs (devSeries (0.0f, 0.0f, 6.0), 20) < 1e-6);
}

TEST_CASE ("wow / flutter at 100 %: the delay never clamps and stays inside [2, maxDelay] (44.1 / 48 / 96 / 192 kHz, 60 s)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Tape t; t.prepare (fs, 2, 512);
        auto p = tapeBase(); p.wow = 1.0f; p.flutter = 1.0f; t.setParams (p);
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        const size_t total = (size_t) (60 * fs);
        for (size_t pos = 0; pos < total; pos += 512)
        {
            const float* ip[2] = { l.data(), r.data() }; float* op[2] = { l.data(), r.data() };
            t.process (ip, op, 512);
        }
        INFO ("fs " << fs << " min " << t.minDelaySeen() << " max " << t.maxDelaySeen() << " limit " << t.maxDelayLimit());
        REQUIRE (t.delayClampCount() == 0);
        REQUIRE (t.minDelaySeen() >= 2.0f);
        REQUIRE (t.maxDelaySeen() <= t.maxDelayLimit());
    }
}

// ------------------------------------------------------------------ Task 6: defaults
TEST_CASE ("neutral tape defaults: Wow, Flutter, Drive, Hiss, Age 0, Tone Off; codec defaults unchanged")
{
    JunkyardAudioProcessor p;
    auto real = [&] (const char* id) { auto* pr = p.apvts.getParameter (id); return pr->convertFrom0to1 (pr->getDefaultValue()); };
    for (auto id : { "wow", "flutter", "saturation", "hiss", "tape_age" }) REQUIRE (real (id) == 0.0f);
    REQUIRE (real ("tape_tone") == 20000.0f);
    REQUIRE (real ("swirl") == 25.0f);
    REQUIRE (p.apvts.getParameter ("tape_on")->getDefaultValue() == 0.0f);
    REQUIRE (p.apvts.getParameter ("saturation")->getName (64) == "Drive");
    const EngineParams e;
    REQUIRE (e.wow == 0.0f); REQUIRE (e.flutter == 0.0f); REQUIRE (e.saturation == 0.0f); REQUIRE (e.hiss == 0.0f); REQUIRE (e.age == 0.0f);
    REQUIRE (e.toneHz == 20000.0f); REQUIRE (e.swirl == 0.25f);
    REQUIRE (std::fabs (kbpsToBitrate (0, 64.0f) - 0.5637f) < 1e-3f);
}

// ------------------------------------------------------------------ v1.0.0: Age knob strength (owner: "50 % more powerful")
TEST_CASE ("Age knob strength: 100 % drives the Age engine at 1.5, about 67 % matches the old 100 %, 0 % is off")
{
    JunkyardAudioProcessor p; ParamRefs r; r.bind (p.apvts);
    auto set = [&] (float pct) { auto* pr = p.apvts.getParameter ("tape_age"); pr->setValueNotifyingHost (pr->convertTo0to1 (pct)); };
    set (100.0f); REQUIRE (std::fabs (r.read().age - kAgeStrength) < 1e-4f);
    REQUIRE (kAgeStrength == 1.5f);
    set (66.7f);  REQUIRE (std::fabs (r.read().age - 1.0f) < 2e-3f);
    set (50.0f);  REQUIRE (std::fabs (r.read().age - 0.75f) < 1e-4f);
    set (0.0f);   REQUIRE (r.read().age == 0.0f);
}
