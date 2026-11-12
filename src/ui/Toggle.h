#pragma once
#include "JunkyardLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace jy::ui
{
class Toggle : public juce::ToggleButton
{
public:
    Toggle (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& text, const juce::String& tooltip)
        : juce::ToggleButton (text), attachment (*apvts.getParameter (paramId), *this, nullptr)
    {
        setTooltip (tooltip);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
private:
    juce::ButtonParameterAttachment attachment;
};

// Segmented radio selector bound to a choice parameter (MP3 | STREAM | BROKEN, Digital > Tape | ...).
class SegmentedChoice : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedChoice (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::StringArray labels, const juce::String& tooltip);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int getSelected() const { return selected; }
    std::function<void (int)> onSelect; // if set, a click calls this instead of writing the parameter directly
private:
    int indexAt (juce::Point<int>) const;
    juce::StringArray names;
    juce::ParameterAttachment attachment;
    int selected = 0, hover = -1;
};
} // namespace jy::ui
