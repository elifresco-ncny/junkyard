#include "SectionPanel.h"

namespace jy::ui
{
void SectionPanel::setPower (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& tip)
{
    power = std::make_unique<Toggle> (apvts, id, "ON", tip);
    addAndMakeVisible (*power);
    activeWatch = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (id), [this] (float v) { active = v > 0.5f; repaint(); }, nullptr);
    activeWatch->sendInitialUpdate();
    resized();
}

void SectionPanel::resized()
{
    if (power) power->setBounds (getWidth() - 74, 5, 64, 24);
    if (layoutBody) layoutBody (getBody());
}

void SectionPanel::paint (juce::Graphics& g)
{
    auto* l = dynamic_cast<JunkyardLookAndFeel*> (&getLookAndFeel());
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (panelHi, 0, 0, panel, 0, b.getHeight(), false));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawRoundedRectangle (b, 8.0f, 2.0f);
    g.setColour (edge); g.drawRoundedRectangle (b.reduced (1.5f), 7.0f, 1.0f);
    // hazard stripe under the title
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> (10, 30, getWidth() - 20, 3));
    g.setColour (hazard.withAlpha (active ? 0.9f : 0.35f));
    for (int x = -10; x < getWidth(); x += 12) { juce::Path p; p.addQuadrilateral ((float) x, 33.0f, (float) x + 6, 33.0f, (float) x + 12, 30.0f, (float) x + 6, 30.0f); g.fillPath (p); }
    g.restoreState();
    if (l)
    {
        g.setColour (active ? hazard : hazardDim);
        g.setFont (l->display (26.0f));
        g.drawText (titleText, 19, 3, getWidth() - 105, 28, juce::Justification::centredLeft, false);   // v1.0: 5 px further from the screw, glyph centred between the border and the stripe (6 px / 6 px)
    }
    for (auto c : { juce::Point<float> (9, 9), { b.getRight() - 9, 9 }, { 9, b.getBottom() - 9 }, { b.getRight() - 9, b.getBottom() - 9 } })
        JunkyardLookAndFeel::drawScrew (g, c, 3.6f, c.x * 0.37f + c.y * 0.11f);
}

void SectionPanel::paintOverChildren (juce::Graphics& g)
{
    if (! active && power)
    {
        g.setColour (bg.withAlpha (0.45f));
        g.fillRoundedRectangle (getLocalBounds().withTrimmedTop (34).toFloat().reduced (2.0f), 6.0f);
    }
}
} // namespace jy::ui
