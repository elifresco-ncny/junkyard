#pragma once
#include "Toggle.h"
#include <functional>

namespace jy::ui
{
// Riveted metal panel with a stencil title and an optional power switch. Dims its controls (still editable) when off.
class SectionPanel : public juce::Component
{
public:
    SectionPanel (const juce::String& title) : titleText (title) {}
    void setPower (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& tooltip);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    juce::Rectangle<int> getBody() const { return getLocalBounds().withTrimmedTop (34).reduced (10, 4); }
    int headerHeight() const { return 34; }
    std::function<void (juce::Rectangle<int> body)> layoutBody; // children are laid out in panel-local coordinates
private:
    juce::String titleText;
    std::unique_ptr<Toggle> power;
    std::unique_ptr<juce::ParameterAttachment> activeWatch;
    bool active = true;
};
} // namespace jy::ui
