#include "Toggle.h"

namespace jy::ui
{
SegmentedChoice::SegmentedChoice (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::StringArray labels, const juce::String& tip)
    : names (std::move (labels)),
      attachment (*apvts.getParameter (paramId), [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
{
    setTooltip (tip);
    attachment.sendInitialUpdate();
}

int SegmentedChoice::indexAt (juce::Point<int> p) const
{
    const int n = names.size();
    return juce::jlimit (0, n - 1, p.x * n / std::max (1, getWidth()));
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (onSelect) onSelect (i); else attachment.setValueAsCompleteGesture ((float) i);
}
void SegmentedChoice::mouseMove (const juce::MouseEvent& e) { const int h = indexAt (e.getPosition()); if (h != hover) { hover = h; repaint(); } }

void SegmentedChoice::paint (juce::Graphics& g)
{
    auto* l = dynamic_cast<JunkyardLookAndFeel*> (&getLookAndFeel());
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRoundedRectangle (b, 5.0f);
    const int n = names.size();
    const float w = b.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto seg = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (2.0f);
        const bool sel = i == selected;
        if (sel) { g.setColour (hazard); g.fillRoundedRectangle (seg, 4.0f); }
        else if (i == hover) { g.setColour (panelHi); g.fillRoundedRectangle (seg, 4.0f); }
        g.setColour (sel ? bg : textDim);
        if (l) g.setFont (l->display (b.getHeight() * 0.62f));
        g.drawText (names[i], seg, juce::Justification::centred, false);
    }
    g.setColour (edge); g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
}
} // namespace jy::ui
