#pragma once
#include "PluginProcessor.h"
#include "ui/JunkyardLookAndFeel.h"

namespace jy::ui { class EditorContent; }

class JunkyardEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit JunkyardEditor (JunkyardAudioProcessor&);
    ~JunkyardEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow();   // pump the live displays once (also used for screenshots)
    static constexpr int kDesignW = 960, kDesignH = 560;
private:
    void timerCallback() override { refreshNow(); }
    JunkyardAudioProcessor& proc;
    jy::ui::JunkyardLookAndFeel laf;
    std::unique_ptr<jy::ui::EditorContent> content;
    juce::ComponentBoundsConstrainer constrainer;
    // v1.0: no hover tooltips (no TooltipWindow). The setTooltip() strings stay as accessibility descriptions for screen readers.
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JunkyardEditor)
};
