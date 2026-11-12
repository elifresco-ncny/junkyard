#pragma once
#include "Palette.h"

namespace jy::ui
{
class JunkyardLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JunkyardLookAndFeel();
    juce::Font display (float height) const;  // Bebas Neue (condensed, stencil-ish caps)
    juce::Font mono (float height) const;     // Share Tech Mono

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    juce::Font getPopupMenuFont() override { return mono (15.0f); }
    juce::Font getLabelFont (juce::Label&) override { return mono (14.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;

    static void drawScrew (juce::Graphics&, juce::Point<float> c, float r, float angle);
private:
    juce::Typeface::Ptr displayFace, monoFace;
};
} // namespace jy::ui
