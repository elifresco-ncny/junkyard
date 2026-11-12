#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/JunkyardEngine.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <random>

// ---- allocation counter: armed only around audio-thread calls
static thread_local bool gArmed = false;   // per thread: only the calling (audio) thread is counted, not a Learn worker
static std::atomic<long> gAllocs { 0 };
void* operator new (std::size_t n) { if (gArmed) gAllocs.fetch_add (1); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
struct Arm { Arm() { gAllocs = 0; gArmed = true; } ~Arm() { gArmed = false; } long count() const { return gAllocs.load(); } };

void fillSignal (juce::AudioBuffer<float>& b, int kind, int pos)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int t = pos + i;
            float v = 0;
            switch (kind)
            {
                case 0: v = 0.0f; break;                                          // silence
                case 1: v = 1.0f; break;                                          // DC
                case 2: v = ((t / 48) % 2) ? 1.0f : -1.0f; break;                 // full-scale square
                case 3: v = (t % 2) ? 1.0f : -1.0f; break;                        // Nyquist square
                default: v = std::sin (0.05f * (float) t) * 0.8f; break;
            }
            b.setSample (c, i, v);
        }
}
juce::AudioProcessor::BusesLayout layout (int nch)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (nch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (nch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    return l;
}
void allOn (JunkyardAudioProcessor& p)
{
    for (auto id : { "codec_on", "tape_on", "low_keep" }) p.apvts.getParameter (id)->setValueNotifyingHost (1.0f);
    p.apvts.getParameter ("glitch")->setValueNotifyingHost (0.5f);
    p.apvts.getParameter ("tape_age")->setValueNotifyingHost (0.6f);
    p.apvts.getParameter ("tape_tone")->setValueNotifyingHost (0.4f);
}
}

TEST_CASE ("no heap allocation on the audio thread (all sections, all modes)")
{
    for (int nch : { 1, 2 })
    {
        JunkyardAudioProcessor p;
        REQUIRE (p.setBusesLayout (layout (nch)));
        allOn (p);
        p.setRateAndBufferSizeDetails (48000, 128);
        p.prepareToPlay (48000, 128);
        juce::AudioBuffer<float> b (nch, 128); juce::MidiBuffer m;
        for (int mode = 0; mode < 3; ++mode)
        {
            p.apvts.getParameter ("codec_mode")->setValueNotifyingHost (mode / 2.0f);
            for (int i = 0; i < 40; ++i) { fillSignal (b, 4, i * 128); p.processBlock (b, m); } // warm up
            long allocs;
            {
                Arm a;
                for (int i = 0; i < 600; ++i) { fillSignal (b, i % 5 == 0 ? 2 : 4, i * 128); p.processBlock (b, m); }
                allocs = a.count();
            }
            INFO ("channels " << nch << " mode " << mode);
            REQUIRE (allocs == 0);
        }
        // bypass path too
        Arm a; for (int i = 0; i < 50; ++i) p.processBlockBypassed (b, m);
        REQUIRE (a.count() == 0);
    }
}

TEST_CASE ("edge-case signals: finite and below the safety ceiling in every mode, mono and stereo, 1-sample blocks")
{
    for (int nch : { 1, 2 })
        for (int kind = 0; kind < 4; ++kind)
            for (int mode = 0; mode < 3; ++mode)
                for (int bs : { 1, 64, 512 })
                {
                    JunkyardAudioProcessor p;
                    REQUIRE (p.setBusesLayout (layout (nch)));
                    allOn (p);
                    p.apvts.getParameter ("codec_mode")->setValueNotifyingHost (mode / 2.0f);
                    p.apvts.getParameter ("mix")->setValueNotifyingHost (1.0f);
                    p.setRateAndBufferSizeDetails (48000, 512);
                    p.prepareToPlay (48000, 512);
                    juce::AudioBuffer<float> b (nch, bs); juce::MidiBuffer m;
                    const int total = bs == 1 ? 6000 : 30000; float peak = 0;
                    for (int pos = 0; pos < total; pos += bs)
                    {
                        fillSignal (b, kind, pos); p.processBlock (b, m);
                        for (int c = 0; c < nch; ++c) for (int i = 0; i < bs; ++i) { const float v = b.getSample (c, i); REQUIRE (std::isfinite (v)); peak = std::max (peak, std::fabs (v)); }
                    }
                    INFO ("nch " << nch << " kind " << kind << " mode " << mode << " bs " << bs << " peak " << peak);
                    REQUIRE (peak <= jy::SafetyLimiter::ceiling);
                    if (kind == 0) REQUIRE (peak < 0.1f); // silence in -> at most hiss/noise-fill out
                }
}

TEST_CASE ("sample-rate / block-size changes while running re-report latency and stay stable")
{
    JunkyardAudioProcessor p; allOn (p);
    juce::MidiBuffer m;
    for (double sr : { 44100.0, 96000.0, 48000.0, 192000.0, 22050.0, 48000.0 })
        for (int bs : { 32, 480, 2048 })
        {
            p.setRateAndBufferSizeDetails (sr, bs);
            p.prepareToPlay (sr, bs);
            const int hop = sr <= 50000 ? 512 : (sr <= 100000 ? 1024 : 2048);
            REQUIRE (p.getLatencySamples() == 2 * hop + (int) std::lround (0.010 * sr));
            juce::AudioBuffer<float> b (2, bs);
            for (int i = 0; i < 20; ++i) { fillSignal (b, 4, i * bs); p.processBlock (b, m); for (int c = 0; c < 2; ++c) REQUIRE (std::isfinite (b.getSample (c, bs - 1))); }
        }
}

TEST_CASE ("automating every parameter every block never breaks the processor")
{
    JunkyardAudioProcessor p; juce::MidiBuffer m; std::mt19937 g (5);
    p.setRateAndBufferSizeDetails (48000, 128); p.prepareToPlay (48000, 128);
    juce::AudioBuffer<float> b (2, 128);
    float peak = 0;
    for (int blk = 0; blk < 48000 * 20 / 128; ++blk)
    {
        for (auto& id : jy::allParameterIds()) if ((g() & 3) == 0) p.apvts.getParameter (id)->setValueNotifyingHost ((float) (g() % 10001) / 10000.0f);
        fillSignal (b, 4, blk * 128); p.processBlock (b, m);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) { REQUIRE (std::isfinite (b.getSample (c, i))); peak = std::max (peak, std::fabs (b.getSample (c, i))); }
    }
    REQUIRE (peak <= 8.0f); // wet is limited to +6 dBFS, then Output can add up to +12 dB (x4)
}

TEST_CASE ("an M1-era saved state loads in the current build")
{
    auto xml = juce::parseXML (juce::File (JUNKYARD_FIXTURES).getChildFile ("m1_state.xml"));
    REQUIRE (xml != nullptr);
    juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (*xml, mb);
    JunkyardAudioProcessor p;
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    auto real = [&] (const char* id) { auto* pr = p.apvts.getParameter (id); return pr->convertFrom0to1 (pr->getValue()); };
    REQUIRE (std::abs (real ("input_gain") - 3.5f) < 0.11f);
    REQUIRE (std::abs (real ("swirl") - 55.0f) < 0.2f);
    REQUIRE (p.apvts.getParameter ("downsample") == nullptr); // Crunch was removed in v0.3
    REQUIRE (std::abs (real ("output_gain") + 2.5f) < 0.11f);
    REQUIRE (p.apvts.getParameter ("codec_mode")->getText (p.apvts.getParameter ("codec_mode")->getValue(), 0) == "Stream");
    // and it processes
    p.setRateAndBufferSizeDetails (48000, 256); p.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
    for (int i = 0; i < 100; ++i) { fillSignal (b, 4, i * 256); p.processBlock (b, m); }
    REQUIRE (std::isfinite (b.getSample (0, 0)));
}

TEST_CASE ("bypass delays by the reported latency and Mix 0 matches it")
{
    JunkyardAudioProcessor p, q; allOn (p);
    p.apvts.getParameter ("mix")->setValueNotifyingHost (0.0f);
    p.setRateAndBufferSizeDetails (48000, 256); p.prepareToPlay (48000, 256);
    q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
    const int L = p.getLatencySamples();
    juce::MidiBuffer m; std::vector<float> in, outMix, outByp;
    std::mt19937 g (2); std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    for (int blk = 0; blk < 60; ++blk)
    {
        juce::AudioBuffer<float> a (2, 256), c (2, 256);
        for (int i = 0; i < 256; ++i) { const float v = d (g); a.setSample (0, i, v); a.setSample (1, i, v); in.push_back (v); }
        c.makeCopyOf (a);
        p.processBlock (a, m); q.processBlockBypassed (c, m);
        for (int i = 0; i < 256; ++i) { outMix.push_back (a.getSample (0, i)); outByp.push_back (c.getSample (0, i)); }
    }
    // after bypass is used the engine prewarms: only compare the mix-0 path against the delayed input
    for (size_t i = (size_t) L; i < in.size(); ++i) REQUIRE (std::fabs (outMix[i] - in[i - (size_t) L]) < 1e-5f);
    for (size_t i = (size_t) L; i < in.size(); ++i) REQUIRE (std::fabs (outByp[i] - in[i - (size_t) L]) < 1e-6f);
}

// ------------------------------------------------------------------ v0.2 regressions
#include "dsp/Tape.h"
#include "ui/Knob.h"

namespace
{
double rmsDb (const std::vector<float>& v, size_t from = 0)
{
    double e = 0; for (size_t i = from; i < v.size(); ++i) e += (double) v[i] * v[i];
    return 10.0 * std::log10 (e / (double) (v.size() - from) + 1e-40);
}
std::vector<float> runEngine (jy::JunkyardEngine& e, const std::vector<float>& in, int bs = 256)
{
    std::vector<float> l = in, r = in, out;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) bs)
    {
        const int n = (int) std::min<size_t> ((size_t) bs, in.size() - pos);
        float* p[2] = { l.data() + pos, r.data() + pos }; e.process (p, n);
        out.insert (out.end(), l.begin() + (long) pos, l.begin() + (long) pos + n);
    }
    return out;
}
std::vector<float> noiseVec (size_t n, float amp, unsigned seed) { std::mt19937 g (seed); std::uniform_real_distribution<float> d (-amp, amp); std::vector<float> v (n); for (auto& s : v) s = d (g); return v; }
}

TEST_CASE ("silent in -> silent out: every mode, sections on, saturation 0/0.3/1, hiss 0")
{
    for (int mode = 0; mode < 3; ++mode)
        for (float sat : { 0.0f, 0.3f, 1.0f })
            for (int order = 0; order < 2; ++order)
            {
                jy::EngineParams p; p.codecMode = mode; p.tapeOn = true; p.lowKeep = true; p.hiss = 0.0f; p.saturation = sat;
                p.age = 0.5f; p.toneHz = 8000.0f; p.glitch = 0.4f; p.swirl = 0.5f; p.order = order;
                jy::JunkyardEngine e; e.setParams (p); e.prepare (48000, 256, 2);
                auto burst = noiseVec (24000, 0.3f, 3);
                burst.resize (24000 + 48000 * 3, 0.0f);           // noise, then 3 s of digital silence
                auto out = runEngine (e, burst);
                std::vector<float> tail (out.end() - 48000, out.end());
                float peak = 0; for (float v : tail) peak = std::max (peak, std::fabs (v));
                INFO ("mode " << mode << " sat " << sat << " order " << order << " tail peak " << peak);
                REQUIRE (peak < 1e-7f);
            }
}

TEST_CASE ("two chained tape instances do not add noise to a -90 dBFS floor")
{
    jy::EngineParams p; p.codecOn = false; p.tapeOn = true; p.hiss = 0.0f; p.saturation = 0.3f; p.wow = p.flutter = 0.0f; p.age = 0.0f;
    jy::JunkyardEngine a, b; a.setParams (p); b.setParams (p); a.prepare (48000, 256, 2); b.prepare (48000, 256, 2);
    const float amp = 3.16e-5f * 1.7320508f;                 // uniform noise with RMS = -90 dBFS
    auto in = noiseVec (96000, amp, 9);
    auto ao = runEngine (a, in), bo = runEngine (b, ao);
    const double da = rmsDb (ao, 9600), db = rmsDb (bo, 9600);
    INFO ("A out " << da << " dB, B out " << db << " dB");
    REQUIRE (db <= da + 3.0);
}

TEST_CASE ("adaa saturator is double-precision clean on tiny signals")
{
    jy::AdaaTanh s; double e = 0, ei = 0; auto in = noiseVec (50000, 5e-5f, 4);
    for (float x : in) { const float y = s.process (x); e += (double) y * y; ei += (double) x * x; }
    REQUIRE (10 * std::log10 (e / ei) < 1.0); // tanh of a tiny signal is ~unity gain, not +18 dB
}

TEST_CASE ("tone: off is transparent, -3 dB point tracks the setting")
{
    auto run1 = [] (float tone, float freq)
    {
        jy::Tape t; t.prepare (48000, 1, 512);
        jy::EngineParams p; p.saturation = 0; p.wow = p.flutter = p.age = p.hiss = 0; p.toneHz = tone; t.setParams (p);
        std::vector<float> in (48000), out (48000);
        for (size_t i = 0; i < in.size(); ++i) in[i] = std::sin (6.2831853f * freq * (float) i / 48000.0f);
        const float* ip[1] = { in.data() }; float* op[1] = { out.data() };
        for (size_t pos = 0; pos < in.size(); pos += 64) { const float* a[1] = { ip[0] + pos }; float* o[1] = { op[0] + pos }; t.process (a, o, 64); }
        std::vector<float> tail (out.begin() + 24000, out.end()), ref (in.begin() + 24000, in.end());
        return rmsDb (tail) - rmsDb (ref);
    };
    REQUIRE (std::fabs (run1 (20000.0f, 8000.0f)) < 1e-4);
    for (float tone : { 5000.0f, 8000.0f })
    {
        float f3 = 0;
        for (float f = 1000.0f; f < 15000.0f; f += 100.0f) if (run1 (tone, f) < -3.0f) { f3 = f; break; }
        INFO ("tone " << tone << " -3 dB at " << f3);
        REQUIRE (std::fabs (f3 - tone) < 0.2f * tone);
    }
}

TEST_CASE ("limiter is transparent up to 0 dBFS and bounded at +6 dBFS; Broken bursts stay bounded")
{
    jy::SafetyLimiter l; float x = 0.99f; l.process (x); REQUIRE (x == 0.99f);
    x = 1.0f; l.process (x); REQUIRE (x == 1.0f);
    x = 50.0f; l.process (x); REQUIRE (x <= jy::SafetyLimiter::ceiling); REQUIRE (x > 1.9f);
    jy::EngineParams p; p.codecMode = 2; p.glitch = 1.0f; p.bitrate = 0.6f; p.swirl = 1.0f;
    jy::JunkyardEngine e; e.setParams (p); e.prepare (48000, 256, 2);
    auto out = runEngine (e, noiseVec (48000 * 20, 0.3f, 8));
    float peak = 0; for (float v : out) peak = std::max (peak, std::fabs (v));
    REQUIRE (peak <= jy::SafetyLimiter::ceiling);
}

TEST_CASE ("knob readout text follows the slider value")
{
    JunkyardAudioProcessor p;
    jy::ui::Knob k (p.apvts, "swirl", "SWIRL", "tip");
    k.setBounds (0, 0, 80, 100);
    for (double v : { 0.0, 12.5, 50.0, 100.0 })
    {
        k.getSlider().setValue (v, juce::sendNotificationSync);
        REQUIRE (k.getSlider().getTextFromValue (k.getSlider().getValue()) == juce::String (juce::roundToInt (v)) + " %");
        REQUIRE (std::abs (p.apvts.getRawParameterValue ("swirl")->load() - (float) v) < 0.2f);
    }
    REQUIRE ((bool) k.getSlider().onValueChange);
}

TEST_CASE ("every factory preset: two chained instances are exactly silent on silence when Hiss is 0")
{
    JunkyardAudioProcessor a, b;
    const int nPresets = a.presets.numFactory();
    for (int i = 0; i < nPresets; ++i)
    {
        for (auto* pr : { &a, &b })
        {
            pr->presets.load (i);
            pr->apvts.getParameter ("hiss")->setValueNotifyingHost (0.0f);
            pr->apvts.getParameter ("mix")->setValueNotifyingHost (1.0f);
            pr->setRateAndBufferSizeDetails (48000, 256); pr->prepareToPlay (48000, 256);
        }
        juce::MidiBuffer m; juce::AudioBuffer<float> x (2, 256);
        float tailPeak = 0;
        for (int blk = 0; blk < 48000 * 6 / 256; ++blk)
        {
            const bool noisy = blk < 40;
            for (int c = 0; c < 2; ++c) for (int s = 0; s < 256; ++s) x.setSample (c, s, noisy ? 0.3f * std::sin (0.05f * (float) (blk * 256 + s)) : 0.0f);
            a.processBlock (x, m); b.processBlock (x, m);
            if (blk > 48000 * 5 / 256) for (int c = 0; c < 2; ++c) for (int s = 0; s < 256; ++s) tailPeak = std::max (tailPeak, std::fabs (x.getSample (c, s)));
        }
        INFO ("preset " << a.presets.all()[(size_t) i].name << " tail peak " << tailPeak);
        REQUIRE (tailPeak < 1e-6f);
    }
}

// ------------------------------------------------------------------ v0.4: Learn completing under 1-sample blocks
#include <chrono>
#include <thread>
TEST_CASE ("no heap allocation on the audio thread while a Drive Learn runs and completes (1-sample blocks)")
{
    JunkyardAudioProcessor p; juce::MidiBuffer m;
    p.apvts.getParameter ("codec_on")->setValueNotifyingHost (0.0f); p.apvts.getParameter ("tape_on")->setValueNotifyingHost (1.0f);
    p.apvts.getParameter ("saturation")->setValueNotifyingHost (0.7f);
    p.setRateAndBufferSizeDetails (48000, 1); p.prepareToPlay (48000, 1);
    juce::AudioBuffer<float> b (2, 1);
    for (int i = 0; i < 2000; ++i) { b.setSample (0, 0, 0.3f * std::sin (0.03f * (float) i)); b.setSample (1, 0, b.getSample (0, 0)); p.processBlock (b, m); }
    p.startLearn();
    long allocs = 0; bool done = false; int after = 0;
    for (long i = 0; i < 48000L * 14 && after < 20000; ++i)
    {
        b.setSample (0, 0, 0.4f * std::sin (0.05f * (float) i) + 0.2f * std::sin (0.9f * (float) i)); b.setSample (1, 0, b.getSample (0, 0));
        { Arm a; p.processBlock (b, m); allocs += a.count(); }
        if ((i & 255) == 0) { p.learnTick(); if (p.isLearned()) done = true; if (p.getLearnUiState() == JunkyardAudioProcessor::LearnAnalysing) std::this_thread::sleep_for (std::chrono::milliseconds (2)); }   // wait budget ~5 s of wall time: the worker is slower under CPU load (was 200 us, flaky under ctest -j4)
        if (done) ++after;
    }
    REQUIRE (done);
    REQUIRE (allocs == 0);
}
