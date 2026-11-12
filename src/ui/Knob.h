#pragma once
#include "JunkyardLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace jy::ui
{
// Slider that supports: Shift at mouse-down = fine drag, Cmd/Ctrl-click = type a value, double-click = reset.
class KnobSlider : public juce::Slider
{
public:
    KnobSlider() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        setMouseDragSensitivity (220);
        setScrollWheelEnabled (true);
        setPopupDisplayEnabled (false, false, nullptr);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { if (onTypeRequest) onTypeRequest(); return; }
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1800 : 220);
        juce::Slider::mouseDown (e);
    }
    std::function<void()> onTypeRequest;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& label, const juce::String& tooltip);
    void resized() override;
    void paint (juce::Graphics&) override;

    void updateLive();
    KnobSlider& getSlider() { return slider; }
    void setLabelHeight (int h) { labelH = h; resized(); }

private:
    void beginTextEntry();
    KnobSlider slider;
    juce::SliderParameterAttachment attachment;
    juce::String label;
    bool hot = false;
    int labelH = 16;
    std::unique_ptr<juce::TextEditor> editor;
};

// Small green LCD text readout.
class LcdReadout : public juce::Component
{
public:
    void setText (const juce::String& main, const juce::String& sub = {})
    {
        if (main != mainText || sub != subText) { mainText = main; subText = sub; repaint(); }
    }
    void paint (juce::Graphics&) override;
private:
    juce::String mainText, subText;
};
} // namespace jy::ui
