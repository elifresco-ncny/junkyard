#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/CodecProfiles.h"
#include "dsp/Loudness.h"

JunkyardAudioProcessor::JunkyardAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      modeProbe (std::make_shared<jy::ModeProbe>()),
      apvts (*this, nullptr, "JUNKYARD", jy::createParameterLayout (modeProbe))
{
    modeProbe->raw.store (apvts.getRawParameterValue (jy::pid::codecMode));
    refs.bind (apvts);
    engine.setScope (&scopeRing);
    apvts.state.setProperty (jy::state::kVersionProp, jy::state::kCurrentVersion, nullptr);
    apvts.state.addListener (this);
    refreshSettings();
}

JunkyardAudioProcessor::~JunkyardAudioProcessor()
{
    learnTimer.stopTimer();
    shutdownLearnJob();
    apvts.state.removeListener (this);
    modeProbe->raw.store (nullptr);
}

void JunkyardAudioProcessor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) { refreshSettings(); }

void JunkyardAudioProcessor::refreshSettings()
{
    reseedOnPlay.store ((bool) apvts.state.getProperty (jy::state::kReseedProp, true));
    uiScale.store (juce::jlimit (0.75f, 1.5f, (float) apvts.state.getProperty (jy::state::kUiScaleProp, 1.0f)));
}

void JunkyardAudioProcessor::releaseResources()
{
    shutdownLearnJob();
    engine.learn().cancel();
    if (engine.learn().state() == jy::LearnCapture::Analyzing) engine.learn().finish();
}

void JunkyardAudioProcessor::prepareToPlay (double sr, int blockSize)
{
    shutdownLearnJob();   // the capture buffer is reallocated below: no job may be reading it
    const int nch = juce::jlimit (1, 2, getTotalNumOutputChannels());
    engine.setParams (refs.read());
    engine.prepare (sr, juce::jmax (blockSize, 64), nch);
    setLatencySamples (engine.getLatencySamples());
    applyDriveCalFromState();
}

bool JunkyardAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return l.getMainInputChannelSet() == out
           && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void JunkyardAudioProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < buf.getNumChannels(); ++c) buf.clear (c, 0, n);
    if (buf.getNumChannels() != engine.getNumChannels() || n == 0) return; // layout mismatch: pass through
    auto params = refs.read();
    // Transport start -> full reset with fixed seeds, so renders/exports repeat exactly.
    // (The transport is used for nothing else: hiss follows the audio, not Live's transport.)
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool running = pos->getIsPlaying();
            if (running && ! wasPlaying && reseedOnPlay.load()) { engine.setParams (params); engine.reset(); }
            wasPlaying = running;
        }
    engine.setParams (params);
    engine.process (buf.getArrayOfWritePointers(), n);
}

void JunkyardAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    if (buf.getNumChannels() != engine.getNumChannels() || buf.getNumSamples() == 0) return;
    engine.processBypassed (buf.getArrayOfWritePointers(), buf.getNumSamples());
}

juce::AudioProcessorEditor* JunkyardAudioProcessor::createEditor() { return new JunkyardEditor (*this); }

void JunkyardAudioProcessor::getStateInformation (juce::MemoryBlock& d) { jy::state::saveBinary (apvts, d); }

void JunkyardAudioProcessor::setStateInformation (const void* data, int size)
{
    if (jy::state::loadBinary (apvts, data, size)) { presets.stateRestored(); stateRestoredAt = juce::Time::getMillisecondCounter(); applyDriveCalFromState(); }
    refreshSettings();
}

void JunkyardAudioProcessor::switchModeFromUi (int newMode)
{
    using namespace jy;
    newMode = juce::jlimit (0, 2, newMode);
    auto* modeP = apvts.getParameter (pid::codecMode);
    const int oldMode = juce::jlimit (0, 2, (int) std::lround (modeP->convertFrom0to1 (modeP->getValue())));
    auto realOf = [this] (const char* id) { auto* p = apvts.getParameter (id); return p->convertFrom0to1 (p->getValue()); };
    auto setReal = [this] (const char* id, float v)
    {
        auto* p = apvts.getParameter (id);
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
    };
    if (newMode == oldMode) return;

    // 1. remember the knobs of the mode being left
    const float oldBitrate = realOf (pid::bitrate);
    auto s = modemem::slot (apvts.state, oldMode);
    s.setProperty (modemem::kValid, true, nullptr);
    s.setProperty (modemem::kBitrate, oldBitrate, nullptr);
    s.setProperty (modemem::kSwirl, realOf (pid::swirl), nullptr);
    s.setProperty (modemem::kHfLoss, realOf (pid::hfLoss), nullptr);
    s.setProperty (modemem::kGlitch, realOf (pid::glitch), nullptr);

    // 2. switch the mode
    setReal (pid::codecMode, (float) newMode);

    // 3. restore the entered mode's knobs; first visit keeps the current kbps (clamped to the new range)
    auto n = modemem::slot (apvts.state, newMode);
    if ((bool) n.getProperty (modemem::kValid, false))
    {
        setReal (pid::bitrate, (float) (double) n.getProperty (modemem::kBitrate));
        setReal (pid::swirl, (float) (double) n.getProperty (modemem::kSwirl));
        setReal (pid::hfLoss, (float) (double) n.getProperty (modemem::kHfLoss));
        setReal (pid::glitch, (float) (double) n.getProperty (modemem::kGlitch));
    }
    else
    {
        setReal (pid::bitrate, kbpsToBitrate (newMode, bitrateToKbps (oldMode, oldBitrate)));
    }
}

void JunkyardAudioProcessor::setCurrentProgram (int i)
{
    // Some hosts call setCurrentProgram(0) right after restoring state: don't let that clobber the saved settings.
    if (stateRestoredAt != 0 && juce::Time::getMillisecondCounter() - stateRestoredAt < 3000) return;
    if (i >= 0 && i < presets.numFactory()) presets.load (i);
}

// ------------------------------------------------------------------ Drive Learn
juce::String JunkyardAudioProcessor::formatDriveCal (const jy::drivecal::LearnedCal& c)
{
    juce::StringArray cs;
    for (int i = 0; i < jy::drivecal::kNd; ++i) cs.add (juce::String (c.C[i], 3));
    return "1;" + juce::String (c.Lcal, 3) + ";" + juce::String (c.inputDbAtLearn, 3) + ";" + cs.joinIntoString (",");
}

bool JunkyardAudioProcessor::parseDriveCal (const juce::String& text, jy::drivecal::LearnedCal& out)
{
    auto parts = juce::StringArray::fromTokens (text, ";", "");
    if (parts.size() != 4 || parts[0].trim() != "1") return false;
    auto cs = juce::StringArray::fromTokens (parts[3], ",", "");
    if (cs.size() != jy::drivecal::kNd) return false;
    jy::drivecal::LearnedCal c;
    c.Lcal = parts[1].getFloatValue(); c.inputDbAtLearn = parts[2].getFloatValue();
    if (! std::isfinite (c.Lcal) || ! std::isfinite (c.inputDbAtLearn) || std::fabs (c.Lcal) > 200.0f || std::fabs (c.inputDbAtLearn) > 100.0f) return false;
    for (int i = 0; i < jy::drivecal::kNd; ++i)
    {
        c.C[i] = cs[i].getFloatValue();
        if (! std::isfinite (c.C[i]) || std::fabs (c.C[i]) > 24.0f) return false;
    }
    c.C[0] = 0.0f; c.learned = true; out = c;
    return true;
}

void JunkyardAudioProcessor::applyDriveCalFromState()
{
    jy::drivecal::LearnedCal c;
    const auto v = apvts.state.getProperty (jy::state::kDriveCalProp);
    if (v.isString() && parseDriveCal (v.toString(), c)) { engine.calPublisher().publish (c); learned = true; }
    else
    {
        if (v.isString() || v.isVoid() == false) apvts.state.removeProperty (jy::state::kDriveCalProp, nullptr);   // malformed: ignore it
        if (learned) { jy::drivecal::LearnedCal d; engine.calPublisher().publish (d); }
        learned = false;
    }
}

struct JunkyardAudioProcessor::LearnJob : juce::Thread
{
    LearnJob (jy::JunkyardEngine& e) : juce::Thread ("Junkyard Learn"), engine (e) {}
    void run() override
    {
        using namespace jy;
        auto& cap = engine.learn();
        const int nch = cap.numChannels(); const double fs = cap.sampleRate(); const size_t n = cap.totalFrames();
        const std::vector<float>* in[2] = { cap.channel (0), nch > 1 ? cap.channel (1) : cap.channel (0) };
        result.Lcal = (float) loud::lufs (in, nch, 0, n, fs);
        std::vector<float> scratch[2];
        result.C[0] = 0.0f;
        for (int d = 1; d < drivecal::kNd; ++d)
        {
            if (threadShouldExit()) return;
            result.C[d] = (float) drivecal::makeupDb (in, nch, fs, (double) d, (double) result.Lcal, scratch);
        }
        result.learned = true;
        done.store (true, std::memory_order_release);
    }
    jy::JunkyardEngine& engine;
    jy::drivecal::LearnedCal result;
    std::atomic<bool> done { false };
};

void JunkyardAudioProcessor::shutdownLearnJob()
{
    if (learnJob) { learnJob->stopThread (10000); learnJob.reset(); }
}

int JunkyardAudioProcessor::getLearnUiState() const
{
    switch (const_cast<jy::JunkyardEngine&> (engine).learn().state())
    {
        case jy::LearnCapture::Waiting: case jy::LearnCapture::Recording: return LearnListening;
        case jy::LearnCapture::Analyzing: return LearnAnalysing;
        default: return learned ? LearnLearned : LearnDefault;
    }
}

void JunkyardAudioProcessor::startLearn()
{
    if (engine.learn().state() != jy::LearnCapture::Idle) return;
    learnInputDb = refs.inputGain != nullptr ? refs.inputGain->load() : 0.0f;
    learnStartedMs = learnClock ? learnClock() : juce::Time::getMillisecondCounter();
    if (engine.learn().start() && ! learnTimer.isTimerRunning()) learnTimer.startTimerHz (20);
}

void JunkyardAudioProcessor::cancelLearn() { engine.learn().cancel(); }

void JunkyardAudioProcessor::toggleLearn()
{
    switch (engine.learn().state())
    {
        case jy::LearnCapture::Idle: startLearn(); break;
        case jy::LearnCapture::Waiting: case jy::LearnCapture::Recording: cancelLearn(); break;
        default: break;   // analysing: ignore clicks (it lasts about a second)
    }
}

void JunkyardAudioProcessor::useDefaultCurve()
{
    jy::drivecal::LearnedCal d;   // learned = false -> the default row D[]
    engine.calPublisher().publish (d);
    learned = false;
    apvts.state.removeProperty (jy::state::kDriveCalProp, nullptr);
}

void JunkyardAudioProcessor::learnTick()
{
    using LS = jy::LearnCapture;
    auto& cap = engine.learn();
    const auto st = cap.state();
    if (st == LS::Waiting || st == LS::Recording)
    {
        const juce::uint32 now = learnClock ? learnClock() : juce::Time::getMillisecondCounter();
        if (now - learnStartedMs > 30000u) cap.cancel();   // nothing playable arrived: keep the previous curve
    }
    else if (st == LS::Analyzing)
    {
        if (! learnJob) { learnJob = std::make_unique<LearnJob> (engine); learnJob->startThread(); }
        else if (learnJob->done.load (std::memory_order_acquire))
        {
            learnJob->stopThread (5000);
            auto cal = learnJob->result;
            learnJob.reset();
            cal.inputDbAtLearn = learnInputDb;
            engine.calPublisher().publish (cal);
            learned = true;
            apvts.state.setProperty (jy::state::kDriveCalProp, formatDriveCal (cal), nullptr);
            cap.finish();
        }
    }
    if (cap.state() == LS::Idle && ! learnJob) learnTimer.stopTimer();
}
