// Drive Learn: fixed curves + learned calibration (FIX_PLAN_3 section 3.4 - 3.6), through the real processor.
#include <catch2/catch_test_macros.hpp>
#include "test_util.h"
#include "PluginProcessor.h"
#include <chrono>
#include <thread>

using namespace jy;
using namespace tu;

namespace
{
void setReal (JunkyardAudioProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
float getReal (JunkyardAudioProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); }

struct Rig
{
    JunkyardAudioProcessor p;
    juce::MidiBuffer midi;
    size_t pos = 0;
    explicit Rig (double fs = kFs, int block = 512)
    {
        setReal (p, "codec_on", 0.0f); setReal (p, "tape_on", 1.0f); setReal (p, "mix", 100.0f);
        p.setRateAndBufferSizeDetails (fs, block); p.prepareToPlay (fs, block);
    }
    // feed `in` (looping) for n frames, returns the output (left, right)
    Stereo feed (const Stereo& in, size_t frames, int block = 512, bool pump = false)
    {
        Stereo out; out.resize (frames);
        for (size_t done = 0; done < frames; done += (size_t) block)
        {
            const int n = (int) std::min<size_t> ((size_t) block, frames - done);
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { const size_t k = (pos + (size_t) i) % in.size(); b.setSample (0, i, in.l[k]); b.setSample (1, i, in.r[k]); }
            p.processBlock (b, midi);
            for (int i = 0; i < n; ++i) { out.l[done + (size_t) i] = b.getSample (0, i); out.r[done + (size_t) i] = b.getSample (1, i); }
            pos += (size_t) n;
            if (pump) p.learnTick();
        }
        return out;
    }
    void silence (size_t frames) { Stereo z; z.resize (512); pos = 0; feed (z, frames); }
    // run a complete Learn on `in`
    bool learn (const Stereo& in)
    {
        p.startLearn();
        for (int i = 0; i < 40 && p.getLearnUiState() == JunkyardAudioProcessor::LearnListening; ++i) feed (in, (size_t) (0.5 * kFs), 512, true);
        for (int i = 0; i < 400 && ! p.isLearned(); ++i) { p.learnTick(); std::this_thread::sleep_for (std::chrono::milliseconds (25)); }
        return p.isLearned();
    }
};

double measure (Rig& r, const Stereo& m, float drivePct, double inputLufs)
{
    setReal (r.p, "saturation", drivePct);
    r.pos = 0;
    r.feed (m, (size_t) (1.0 * kFs));   // settle
    r.pos = 0;
    const auto o = r.feed (m, m.size());
    const size_t lat = (size_t) r.p.getLatencySamples();
    Stereo oo; oo.l.assign (o.l.begin() + (long) lat, o.l.end()); oo.r.assign (o.r.begin() + (long) lat, o.r.end());
    const size_t n = oo.size();
    return jy::loud::lufs (oo.l, oo.r, (size_t) kFs, n, kFs) - inputLufs;
}
}

TEST_CASE ("learn: accuracy - after Learn on the mastered mix, Drive 0 / 30 / 60 / 100 % stay within 0.5 dB of the input")
{
    Rig r; auto m = corpus::make (corpus::Mastered, kFs, 8.0);
    const double L = jy::corpus::lufs (m, kFs);
    REQUIRE (r.learn (m));
    REQUIRE (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnLearned);
    for (float d : { 0.0f, 30.0f, 60.0f, 100.0f })
    {
        const double e = measure (r, m, d, L - 0.0);
        INFO ("mastered mix, Drive " << d << " %: " << e << " dB");
        REQUIRE (std::fabs (e) <= 0.5);
    }
}

TEST_CASE ("learn: accuracy - the drum loop at -24 LUFS")
{
    Rig r; auto m = corpus::make (corpus::Drums, kFs, 8.0); corpus::scaleToLufs (m, kFs, -24.0);
    REQUIRE (r.learn (m));
    for (float d : { 0.0f, 30.0f, 60.0f, 100.0f })
    {
        const double e = measure (r, m, d, -24.0);
        INFO ("drums, Drive " << d << " %: " << e << " dB");
        REQUIRE (std::fabs (e) <= 0.5);
    }
}

TEST_CASE ("learn: Input correction - moving Input to -6 dB after learning gives output = input - 6 dB (+-1 dB) at Drive 100 %")
{
    Rig r; auto m = corpus::make (corpus::Mastered, kFs, 8.0);
    const double L = jy::corpus::lufs (m, kFs);
    REQUIRE (r.learn (m));
    setReal (r.p, "input_gain", -6.0f);
    const double e = measure (r, m, 100.0f, L);
    INFO ("error vs input - 6 dB: " << e + 6.0 << " dB");
    REQUIRE (std::fabs (e + 6.0) <= 1.0);
}

TEST_CASE ("learn: silent input stays listening and times out at 30 s, keeping the previous curve")
{
    Rig r; juce::uint32 now = 1000; r.p.learnClock = [&now] { return now; };
    r.p.startLearn();
    REQUIRE (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnListening);
    r.silence ((size_t) (2 * kFs)); now += 20000; r.p.learnTick();
    REQUIRE (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnListening);
    REQUIRE (r.p.getEngine().learn().capturedFrames() == 0);
    now += 10500; r.p.learnTick();
    REQUIRE (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnDefault);
    REQUIRE (! r.p.isLearned());
}

TEST_CASE ("learn: audio with gaps - only the non-silent chunks are captured")
{
    Rig r; auto m = corpus::make (corpus::FullMix, kFs, 4.0); corpus::scaleToLufs (m, kFs, -18.0);
    Stereo gapped; gapped.resize ((size_t) (4.0 * kFs));
    for (size_t i = 0; i < (size_t) (1.0 * kFs); ++i) { gapped.l[i] = m.l[i]; gapped.r[i] = m.r[i]; }                 // music, silence, music, silence (1 s each)
    for (size_t i = (size_t) (2.0 * kFs); i < (size_t) (3.0 * kFs); ++i) { gapped.l[i] = m.l[i]; gapped.r[i] = m.r[i]; }
    r.p.startLearn();
    r.feed (gapped, gapped.size());
    const auto& cap = r.p.getEngine().learn();
    // after 4 s fed: the two music seconds were kept, both silent seconds were rewound
    const double secs = (double) cap.capturedFrames() / kFs;
    INFO ("captured " << secs << " s of 4 s fed");
    REQUIRE (secs > 1.8); REQUIRE (secs < 2.6);   // chunk granularity: a 100 ms chunk that holds the edge of the music is kept
    r.p.cancelLearn();
}

TEST_CASE ("learn: a click while listening cancels and keeps the previous curve; Use default curve restores the default")
{
    Rig r; auto m = corpus::make (corpus::Mastered, kFs, 8.0);
    r.p.toggleLearn(); r.feed (m, (size_t) kFs);
    REQUIRE (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnListening);
    r.p.toggleLearn();
    REQUIRE (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnDefault);
    REQUIRE (! r.p.isLearned());
    REQUIRE (r.learn (m));
    REQUIRE (r.p.apvts.state.hasProperty (jy::state::kDriveCalProp));
    r.p.useDefaultCurve();
    REQUIRE (! r.p.isLearned()); REQUIRE (! r.p.apvts.state.hasProperty (jy::state::kDriveCalProp));
    REQUIRE (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnDefault);
}

TEST_CASE ("learn: the calibration round-trips through the state, survives preset loads, and never enters a user preset")
{
    Rig a; auto m = corpus::make (corpus::Mastered, kFs, 8.0);
    REQUIRE (a.learn (m));
    juce::MemoryBlock mb; a.p.getStateInformation (mb);
    Rig b; REQUIRE (! b.p.isLearned());
    b.p.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (b.p.isLearned());
    REQUIRE (b.p.apvts.state.getProperty (jy::state::kDriveCalProp).toString() == a.p.apvts.state.getProperty (jy::state::kDriveCalProp).toString());
    // preset load / Init / Scrap leave it alone
    const auto before = b.p.apvts.state.getProperty (jy::state::kDriveCalProp).toString();
    b.p.presets.load (3); REQUIRE (b.p.apvts.state.getProperty (jy::state::kDriveCalProp).toString() == before);
    b.p.presets.load (0); REQUIRE (b.p.apvts.state.getProperty (jy::state::kDriveCalProp).toString() == before);
    b.p.presets.scrap(); REQUIRE (b.p.apvts.state.getProperty (jy::state::kDriveCalProp).toString() == before);
    REQUIRE (b.p.isLearned());
    // user preset file content
    auto xml = b.p.presets.userPresetXml ("Test");
    REQUIRE (xml != nullptr);
    REQUIRE (! xml->hasAttribute (jy::state::kDriveCalProp));
    REQUIRE (jy::state::toXml (b.p.apvts)->hasAttribute (jy::state::kDriveCalProp));
    // malformed values are ignored
    for (const char* bad : { "garbage", "1;x;y;1,2,3", "2;-14;0;0,0", "1;-14;0;" })
    {
        JunkyardAudioProcessor c; c.apvts.state.setProperty (jy::state::kDriveCalProp, bad, nullptr);
        juce::MemoryBlock mb2; c.getStateInformation (mb2);
        JunkyardAudioProcessor d; d.setStateInformation (mb2.getData(), (int) mb2.getSize());
        REQUIRE (! d.isLearned());
    }
}

TEST_CASE ("learn: finishing while the audio runs in 1-sample blocks never steps the level")
{
    Rig r (kFs, 1);
    auto m = corpus::make (corpus::Mastered, kFs, 8.0);
    setReal (r.p, "saturation", 50.0f);
    const auto tone = mono2 (sine ((size_t) (60 * kFs), 220.0, 0.3));
    r.feed (tone, (size_t) kFs, 1);
    r.p.startLearn();
    std::vector<float> out; out.reserve ((size_t) (12 * kFs));
    size_t sinceDone = 0; bool done = false;
    juce::AudioBuffer<float> b (2, 1);
    for (size_t i = 0; i < (size_t) (12 * kFs) && sinceDone < (size_t) (0.6 * kFs); ++i)
    {
        const float v = (float) (0.5 * std::sin (2 * kPi * 220.0 * (double) (r.pos + i) / kFs) + 0.2 * std::sin (2 * kPi * 3100.0 * (double) (r.pos + i) / kFs));
        b.setSample (0, 0, v); b.setSample (1, 0, v);
        r.p.processBlock (b, r.midi);
        out.push_back (b.getSample (0, 0));
        if ((i & 255) == 0)
        {
            r.p.learnTick(); if (r.p.isLearned()) done = true;
            if (r.p.getLearnUiState() == JunkyardAudioProcessor::LearnAnalysing) std::this_thread::sleep_for (std::chrono::milliseconds (1));   // let the worker run (real time)
        }
        if (done) ++sinceDone;
    }
    REQUIRE (done);
    // largest sample step in the 600 ms after the swap vs in the second before the Learn finished: the 200 ms ramp must not add a step
    float stepBefore = 0, stepAfter = 0; const size_t n = out.size(), cut = n - sinceDone;
    for (size_t i = cut - (size_t) kFs; i < cut; ++i) stepBefore = std::max (stepBefore, std::fabs (out[i] - out[i - 1]));
    for (size_t i = cut; i < n; ++i) stepAfter = std::max (stepAfter, std::fabs (out[i] - out[i - 1]));
    INFO ("max step before " << stepBefore << ", after " << stepAfter);
    REQUIRE (stepAfter <= 1.6f * stepBefore);
}
