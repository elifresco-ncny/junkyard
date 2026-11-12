#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "State.h"
#include "Presets.h"
#include "ModeMemory.h"
#include "dsp/JunkyardEngine.h"
#include <functional>

class JunkyardAudioProcessor : public juce::AudioProcessor,
                               private juce::ValueTree::Listener
{
public:
    JunkyardAudioProcessor();
    ~JunkyardAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Junkyard"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }   // Age halo and side-B ghost ring for about a second
    int getNumPrograms() override { return std::max (1, presets.numFactory()); }
    int getCurrentProgram() override { const int i = presets.currentIndex(); return (i >= 0 && i < presets.numFactory()) ? i : 0; }
    void setCurrentProgram (int i) override;
    const juce::String getProgramName (int i) override { return (i >= 0 && i < presets.numFactory()) ? presets.all()[(size_t) i].name : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Non-automatable settings (stored in plugin state)
    bool getReseedOnPlay() const { return reseedOnPlay.load(); }
    void setReseedOnPlay (bool b) { apvts.state.setProperty (jy::state::kReseedProp, b, nullptr); }
    float getUiScale() const { return uiScale.load(); }
    void setUiScale (float s) { apvts.state.setProperty (jy::state::kUiScaleProp, juce::jlimit (0.75f, 1.5f, s), nullptr); }

    // Message thread only. Called from the Mode switch's click handler (never from a parameter listener, so host or
    // automation mode changes just change the mode): remembers Bitrate/Swirl/HF Loss/Glitch per codec mode.
    void switchModeFromUi (int newMode);
    bool modeMemoryValid (int mode) const { return jy::modemem::isValid (apvts.state, mode); }

    std::shared_ptr<jy::ModeProbe> modeProbe; // must be declared before apvts
    juce::AudioProcessorValueTreeState apvts;
    jy::ScopeRing scopeRing;
    bool getShowStamp() const { return (bool) apvts.state.getProperty (jy::state::kShowStampProp, false); }
    void setShowStamp (bool b) { apvts.state.setProperty (jy::state::kShowStampProp, b, nullptr); }
    jy::PresetManager presets { apvts };
    bool loadPresetByName (const juce::String& n) { return presets.loadByName (n); }
    jy::JunkyardEngine& getEngine() { return engine; }

    // ---- Drive "Learn" (message thread). The calibration is a state property (never a parameter); presets leave it alone.
    enum LearnUi { LearnDefault = 0, LearnListening = 1, LearnAnalysing = 2, LearnLearned = 3 };
    void toggleLearn();                    // click: start, or cancel while listening
    void startLearn();
    void cancelLearn();
    void useDefaultCurve();
    int getLearnUiState() const;           // what the button shows
    bool isLearned() const { return learned; }
    void learnTick();                      // the 20 Hz timer body (public so tests can pump it)
    std::function<juce::uint32()> learnClock;   // test hook: wall clock in ms
    static bool parseDriveCal (const juce::String&, jy::drivecal::LearnedCal&);
    static juce::String formatDriveCal (const jy::drivecal::LearnedCal&);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void refreshSettings();
    void applyDriveCalFromState();
    void shutdownLearnJob();
    struct LearnJob;
    struct LearnTimer : juce::Timer { explicit LearnTimer (JunkyardAudioProcessor& o) : owner (o) {} void timerCallback() override { owner.learnTick(); } JunkyardAudioProcessor& owner; };
    std::unique_ptr<LearnJob> learnJob;
    LearnTimer learnTimer { *this };
    juce::uint32 learnStartedMs = 0; float learnInputDb = 0.0f; bool learned = false;

    jy::ParamRefs refs;
    jy::JunkyardEngine engine;
    std::atomic<bool> reseedOnPlay { true };
    bool wasPlaying = false;
    juce::uint32 stateRestoredAt = 0;
    std::atomic<float> uiScale { 1.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JunkyardAudioProcessor)
};
