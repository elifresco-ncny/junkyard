// v0.3 tests that are still valid in v0.4: codec level neutrality, mode memory (the tape tests moved to test_v04.cpp / test_age.cpp)
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "dsp/JunkyardEngine.h"
#include "dsp/Tape.h"
#include <juce_dsp/juce_dsp.h>
#include <random>
#include <numeric>

using namespace jy;

namespace
{
constexpr double kFs = 48000.0;
constexpr float kPi = 3.14159265358979f;

double rmsDb (const std::vector<float>& v, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) v[i] * v[i];
    return 10.0 * std::log10 (e / (double) (b - a) + 1e-30);
}

// Pink-ish noise (Voss-style filter bank) scaled to a given RMS in dBFS.
std::vector<float> pinkNoise (size_t n, double rmsDbfs, unsigned seed = 5)
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
    const double cur = rmsDb (v, 0, n);
    const float g2 = (float) std::pow (10.0, (rmsDbfs - cur) / 20.0);
    for (auto& s : v) s *= g2;
    return v;
}

// Run mono x through an engine (both channels identical) in blocks of 256, returns left.
std::vector<float> runEngine (JunkyardEngine& e, const std::vector<float>& x, int block = 256)
{
    std::vector<float> l = x, r = x;
    for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, x.size() - pos);
        float* p[2] = { l.data() + pos, r.data() + pos };
        e.process (p, n);
    }
    return l;
}

std::unique_ptr<JunkyardEngine> makeEngine (const EngineParams& p)
{
    auto e = std::make_unique<JunkyardEngine>();
    e->setParams (p); e->prepare (kFs, 512, 2); e->setParams (p);
    return e;
}
} // namespace

TEST_CASE ("codec is level neutral: MP3 320 within 0.25 dB, every mode at 64 kbps within 1 dB")
{
    auto in = pinkNoise ((size_t) (6 * kFs), -10.0, 11);
    for (size_t k = 0; k < in.size(); k += (size_t) (0.5 * kFs)) // kick-like bursts
        for (size_t i = 0; i < 4800 && k + i < in.size(); ++i) in[k + i] += 0.5f * std::sin (2.0f * kPi * 60.0f * (float) i / (float) kFs) * std::exp (-(float) i / 1500.0f);
    const double inDb = rmsDb (in, (size_t) kFs, (size_t) (5 * kFs));
    auto measure = [&] (int mode, float kbps)
    {
        EngineParams p; p.codecOn = true; p.codecMode = mode; p.bitrate = kbpsToBitrate (mode, kbps); p.swirl = 0; p.mix = 1.0f;
        auto e = makeEngine (p);
        const auto out = runEngine (*e, in);
        return rmsDb (out, (size_t) kFs + (size_t) e->getLatencySamples(), (size_t) (5 * kFs) + (size_t) e->getLatencySamples()) - inDb;
    };
    REQUIRE (std::fabs (measure (ModeMP3, 320.0f)) <= 0.25);
    for (int m : { (int) ModeMP3, (int) ModeStream, (int) ModeBroken })
    {
        const double d = measure (m, 64.0f);
        INFO ("mode " << m << " diff " << d);
        REQUIRE (std::fabs (d) <= 1.0);
    }
}

// ------------------------------------------------------------------ Task 6: per-mode knob memory
#include "dsp/CodecProfiles.h"
namespace
{
float realOf (JunkyardAudioProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); }
void setRealP (JunkyardAudioProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
float kbpsOf (JunkyardAudioProcessor& p) { return bitrateToKbps ((int) std::lround (realOf (p, "codec_mode")), realOf (p, "bitrate")); }
}

TEST_CASE ("mode memory: each codec mode remembers its own Bitrate / Swirl / HF Loss / Glitch (UI switches)")
{
    JunkyardAudioProcessor p;
    setRealP (p, "codec_mode", 0); setRealP (p, "bitrate", kbpsToBitrate (0, 128.0f)); setRealP (p, "swirl", 10.0f);
    p.switchModeFromUi (1);
    REQUIRE ((int) realOf (p, "codec_mode") == 1);
    // first visit: kbps kept (128 is inside Stream's 6-256 range)
    REQUIRE (std::fabs (kbpsOf (p) - 128.0f) < 1.0f);
    REQUIRE (std::fabs (realOf (p, "swirl") - 10.0f) < 0.2f);   // Swirl carries over on a first visit
    setRealP (p, "bitrate", kbpsToBitrate (1, 32.0f)); setRealP (p, "swirl", 60.0f);
    p.switchModeFromUi (2);
    // first visit to Broken from Stream 32 kbps: clamped to Broken's 64 kbps max? (32 < 64 so it is kept)
    REQUIRE (std::fabs (kbpsOf (p) - 32.0f) < 1.0f);
    p.switchModeFromUi (0);
    REQUIRE (std::fabs (kbpsOf (p) - 128.0f) < 1.0f); REQUIRE (std::fabs (realOf (p, "swirl") - 10.0f) < 0.2f);
    p.switchModeFromUi (1);
    REQUIRE (std::fabs (kbpsOf (p) - 32.0f) < 1.0f); REQUIRE (std::fabs (realOf (p, "swirl") - 60.0f) < 0.2f);
}

TEST_CASE ("mode memory: first visit to Broken from MP3 128 kbps is clamped to 64 kbps")
{
    JunkyardAudioProcessor p;
    setRealP (p, "bitrate", kbpsToBitrate (0, 128.0f)); setRealP (p, "glitch", 33.0f);
    p.switchModeFromUi (2);
    REQUIRE (std::fabs (kbpsOf (p) - 64.0f) < 0.5f);
    REQUIRE (std::fabs (realOf (p, "glitch") - 33.0f) < 0.2f);
}

TEST_CASE ("mode memory: a host-side mode change leaves the other four parameters alone")
{
    JunkyardAudioProcessor p;
    setRealP (p, "bitrate", 0.7f); setRealP (p, "swirl", 41.0f); setRealP (p, "hf_loss", 22.0f); setRealP (p, "glitch", 17.0f);
    p.apvts.getParameter ("codec_mode")->setValueNotifyingHost (1.0f); // host / automation path
    REQUIRE (std::fabs (realOf (p, "bitrate") - 0.7f) < 1e-3f); REQUIRE (std::fabs (realOf (p, "swirl") - 41.0f) < 0.2f);
    REQUIRE (std::fabs (realOf (p, "hf_loss") - 22.0f) < 0.2f); REQUIRE (std::fabs (realOf (p, "glitch") - 17.0f) < 0.2f);
    REQUIRE (! p.modeMemoryValid (0));
}

TEST_CASE ("mode memory survives get/setStateInformation; presets and Scrap clear it; saved user presets never contain it")
{
    JunkyardAudioProcessor a, b;
    setRealP (a, "bitrate", kbpsToBitrate (0, 96.0f)); setRealP (a, "swirl", 33.0f);
    a.switchModeFromUi (1); setRealP (a, "swirl", 70.0f);
    REQUIRE (a.modeMemoryValid (0));
    juce::MemoryBlock mb; a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (b.modeMemoryValid (0));
    b.switchModeFromUi (0);
    REQUIRE (std::fabs (kbpsOf (b) - 96.0f) < 1.0f); REQUIRE (std::fabs (realOf (b, "swirl") - 33.0f) < 0.2f);
    // factory preset load clears
    REQUIRE (b.presets.numFactory() > 0);
    b.switchModeFromUi (2); REQUIRE (b.modeMemoryValid (0));
    b.presets.load (0);
    for (int m = 0; m < 3; ++m) REQUIRE (! b.modeMemoryValid (m));
    // Scrap clears
    b.switchModeFromUi (1); REQUIRE ((b.modeMemoryValid (0) || b.modeMemoryValid (2)));
    b.presets.scrap();
    for (int m = 0; m < 3; ++m) REQUIRE (! b.modeMemoryValid (m));
    // user preset files are free of it
    b.switchModeFromUi (((int) std::lround (realOf (b, "codec_mode")) + 1) % 3); // Scrap picks a random mode: always switch to a different one
    auto xml = jy::state::toXml (b.apvts);
    REQUIRE (xml->getChildByName (jy::modemem::kType) != nullptr); // session state has it ...
    auto presetXml = b.presets.userPresetXml ("Test");
    REQUIRE (presetXml != nullptr);
    REQUIRE (presetXml->getChildByName (jy::modemem::kType) == nullptr);   // ... presets don't
}
