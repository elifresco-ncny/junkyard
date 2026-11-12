#include "Knob.h"

namespace jy::ui
{
static JunkyardLookAndFeel* laf (juce::Component& c) { return dynamic_cast<JunkyardLookAndFeel*> (&c.getLookAndFeel()); }

Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& lbl, const juce::String& tip)
    : attachment (*apvts.getParameter (paramId), slider, nullptr), label (lbl)
{
    slider.setTooltip (tip);
    slider.onTypeRequest = [this] { beginTextEntry(); };
    // The readout under the knob must follow the value while dragging (the hover state alone doesn't change then).
    slider.onValueChange = [this] { repaint (getLocalBounds().removeFromBottom (labelH)); };
    addAndMakeVisible (slider);
}

void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (labelH)); }

void Knob::updateLive()
{
    const bool h = slider.isMouseOverOrDragging() || slider.isMouseButtonDown();
    if (h != hot) { hot = h; repaint(); }
}

void Knob::paint (juce::Graphics& g)
{
    auto* l = laf (*this);
    auto r = getLocalBounds().removeFromBottom (labelH);
    g.setColour (hot ? juce::Colour (0xfff2c200) : textDim);
    if (hot && l)
    {
        g.setFont (l->mono ((float) labelH - 3.0f));
        g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::centred, false);
    }
    else if (l)
    {
        g.setFont (l->display ((float) labelH + 1.0f));
        g.drawText (label, r, juce::Justification::centred, false);
    }
}

void Knob::beginTextEntry()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setJustification (juce::Justification::centred);
    if (auto* l = laf (*this)) editor->setFont (l->mono (14.0f));
    editor->setText (slider.getTextFromValue (slider.getValue()), false);
    editor->setBounds (getLocalBounds().withSizeKeepingCentre (std::max (70, getWidth()), 22));
    addAndMakeVisible (*editor);
    editor->selectAll();
    editor->grabKeyboardFocus();
    auto close = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)] { if (safe) safe->editor.reset(); }); };
    editor->onReturnKey = [this, close]
    {
        const double v = slider.getValueFromText (editor->getText());
        slider.setValue (v, juce::sendNotificationSync);
        close();
    };
    editor->onEscapeKey = close;
    editor->onFocusLost = close;
}

void LcdReadout::paint (juce::Graphics& g)
{
    auto* l = laf (*this);
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black); g.fillRoundedRectangle (b, 4.0f);
    g.setColour (lcdBg); g.fillRoundedRectangle (b.reduced (2.0f), 3.0f);
    if (! l) return;
    g.setColour (lcd);
    auto main = getLocalBounds().reduced (6, 2);
    if (subText.isNotEmpty()) { g.setFont (l->mono (13.0f)); g.drawText (subText, main.removeFromBottom (16), juce::Justification::centredRight, false); }
    g.setFont (l->mono (b.getHeight() * (subText.isEmpty() ? 0.55f : 0.42f)));
    g.drawText (mainText, main, juce::Justification::centred, false);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    for (int y = 3; y < getHeight() - 2; y += 3) g.drawHorizontalLine (y, 3.0f, b.getWidth() - 3.0f);
}
} // namespace jy::ui
