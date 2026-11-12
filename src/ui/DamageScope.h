#pragma once
#include "JunkyardLookAndFeel.h"
#include "dsp/ScopeRing.h"

namespace jy::ui
{
// Live spectrogram of what the codec keeps: blue = kept, black holes = starved bands, red = glitch frames.
class DamageScope : public juce::Component, public juce::SettableTooltipClient
{
public:
    DamageScope();
    void pull (ScopeRing& ring);   // call from a UI timer
    void paint (juce::Graphics&) override;
    void clear();
private:
    static constexpr int kCols = 480;
    juce::Image img { juce::Image::ARGB, kCols, kScopeBins, true };
    int col = 0;
};
} // namespace jy::ui
