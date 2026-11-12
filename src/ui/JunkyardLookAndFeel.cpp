#include "JunkyardLookAndFeel.h"
#if JUNKYARD_HAVE_FONTS
 #include "BinaryData.h"
#endif

namespace jy::ui
{
JunkyardLookAndFeel::JunkyardLookAndFeel()
{
#if JUNKYARD_HAVE_FONTS
    displayFace = juce::Typeface::createSystemTypefaceFor (BinaryData::BebasNeueRegular_ttf, BinaryData::BebasNeueRegular_ttfSize);
    monoFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
#endif
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, hazard);
    setColour (juce::PopupMenu::highlightedTextColourId, bg);
    setColour (juce::TooltipWindow::backgroundColourId, bg);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, hazardDim);
    setColour (juce::TextEditor::backgroundColourId, lcdBg);
    setColour (juce::TextEditor::textColourId, lcd);
    setColour (juce::TextEditor::outlineColourId, hazard);
    setColour (juce::TextEditor::focusedOutlineColourId, hazard);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, hazard);
    setColour (juce::TextButton::buttonColourId, panelHi);
    setColour (juce::TextButton::textColourOffId, text);
}

juce::Font JunkyardLookAndFeel::display (float h) const
{
    return displayFace ? juce::Font (juce::FontOptions (displayFace).withHeight (h)) : juce::Font (juce::FontOptions (h, juce::Font::bold));
}
juce::Font JunkyardLookAndFeel::mono (float h) const
{
    return monoFace ? juce::Font (juce::FontOptions (monoFace).withHeight (h)) : juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

void JunkyardLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    g.setGradientFill (juce::ColourGradient (screw.brighter (0.3f), c.x - r * 0.4f, c.y - r * 0.4f, screw.darker (0.6f), c.x + r, c.y + r, true));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 0.8f);
    g.drawLine (c.x - r * 0.7f * std::cos (angle), c.y - r * 0.7f * std::sin (angle), c.x + r * 0.7f * std::cos (angle), c.y + r * 0.7f * std::sin (angle), 1.3f);
}

void JunkyardLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const bool on = s.isEnabled();

    juce::Path track; track.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, a0, a1, true);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.strokePath (track, juce::PathStrokeType (r * 0.11f + 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, a0, ang, true);
    g.setColour (on ? hazard : hazardDim);
    g.strokePath (val, juce::PathStrokeType (r * 0.11f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = r * 0.74f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - br - 1, c.y - br + 2, 2 * br + 2, 2 * br + 2);
    g.setGradientFill (juce::ColourGradient (panelHi.brighter (0.25f), c.x - br * 0.5f, c.y - br * 0.7f, panel.darker (0.4f), c.x + br * 0.6f, c.y + br, true));
    g.fillEllipse (c.x - br, c.y - br, 2 * br, 2 * br);
    g.setColour (edge);
    g.drawEllipse (c.x - br, c.y - br, 2 * br, 2 * br, 1.2f);
    // knurled rim
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (int i = 0; i < 28; ++i)
    {
        const float t = juce::MathConstants<float>::twoPi * (float) i / 28.0f;
        g.drawLine (c.x + std::sin (t) * br * 0.9f, c.y - std::cos (t) * br * 0.9f, c.x + std::sin (t) * br, c.y - std::cos (t) * br, 1.0f);
    }
    g.setColour (on ? hazard : hazardDim);
    g.drawLine (c.x + std::sin (ang) * br * 0.25f, c.y - std::cos (ang) * br * 0.25f, c.x + std::sin (ang) * br * 0.88f, c.y - std::cos (ang) * br * 0.88f, std::max (2.0f, r * 0.07f));
}

void JunkyardLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& t, bool over, bool)
{
    auto b = t.getLocalBounds().toFloat();
    const float d = std::min (b.getHeight() - 4.0f, 16.0f);
    auto led = juce::Rectangle<float> (b.getX() + 2, b.getCentreY() - d / 2, d * 1.7f, d);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (led, d / 2);
    const bool on = t.getToggleState();
    const float kx = on ? led.getRight() - d + 2 : led.getX() + 2;
    if (on) { g.setColour (hazard.withAlpha (0.25f)); g.fillRoundedRectangle (led.expanded (2), d / 2 + 2); }
    g.setColour (on ? hazard : juce::Colour (0xff3b4148));
    g.fillEllipse (kx, led.getY() + 2, d - 4, d - 4);
    g.setColour (edge.withAlpha (over ? 1.0f : 0.6f));
    g.drawRoundedRectangle (led, d / 2, 1.0f);
    g.setColour (on ? text : textDim);
    g.setFont (display (b.getHeight() * 0.95f));
    g.drawText (t.getButtonText(), (int) (led.getRight() + 6), 0, (int) (b.getWidth() - led.getRight() - 6), (int) b.getHeight(), juce::Justification::centredLeft, false);
}

void JunkyardLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (panel);
    g.setColour (hazard);
    g.drawRect (0, 0, w, h, 1);
}

juce::Rectangle<int> JunkyardLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const auto f = mono (14.0f);
    juce::GlyphArrangement ga; ga.addJustifiedText (f, tip, 0, 0, 260.0f, juce::Justification::left);
    const auto tb = ga.getBoundingBox (0, -1, true);
    const int w = (int) tb.getWidth() + 20, h = (int) tb.getHeight() + 14;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24, pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 20, w, h).constrainedWithin (parent);
}

void JunkyardLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h)
{
    g.fillAll (bg);
    g.setColour (hazardDim); g.drawRect (0, 0, w, h, 1);
    g.setColour (text);
    juce::AttributedString as; as.setJustification (juce::Justification::centredLeft);
    as.append (tip, mono (14.0f), text);
    juce::TextLayout tl; tl.createLayout (as, 260.0f);
    tl.draw (g, juce::Rectangle<float> (10.0f, 7.0f, (float) w - 20.0f, (float) h - 14.0f));
}
} // namespace jy::ui
