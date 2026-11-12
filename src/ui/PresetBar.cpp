#include "PresetBar.h"

namespace jy::ui
{
PresetBar::PresetBar()
{
    for (auto* b : { &prev, &next, &save, &scrap, &nameBtn }) { addAndMakeVisible (*b); b->setColour (juce::TextButton::buttonColourId, panelHi); }
    nameBtn.setColour (juce::TextButton::buttonColourId, lcdBg);
    nameBtn.setColour (juce::TextButton::textColourOffId, lcd);
    nameBtn.setButtonText ("Init");
    prev.setTooltip ("Previous preset."); next.setTooltip ("Next preset.");
    save.setTooltip ("Saves the current settings as a preset.");
    scrap.setTooltip ("Randomizes the settings. Input, Mix, Output and Low Keep stay as they are. Undo is in the preset menu.");
    nameBtn.setTooltip ("Opens the factory and user preset list.");
    prev.onClick = [this] { if (onPrev) onPrev(); };
    next.onClick = [this] { if (onNext) onNext(); };
    save.onClick = [this] { if (onSave) onSave(); };
    scrap.onClick = [this] { if (onScrap) onScrap(); };
    nameBtn.onClick = [this] { if (onNameClick) onNameClick (nameBtn); };
}

void PresetBar::setPresetName (const juce::String& name, bool modified) { nameBtn.setButtonText (name + (modified ? " *" : "")); }

void PresetBar::resized()
{
    auto b = getLocalBounds();
    prev.setBounds (b.removeFromLeft (30)); b.removeFromLeft (4);
    scrap.setBounds (b.removeFromRight (70)); b.removeFromRight (4);
    save.setBounds (b.removeFromRight (60)); b.removeFromRight (4);
    next.setBounds (b.removeFromRight (30)); b.removeFromRight (4);
    nameBtn.setBounds (b);
}
} // namespace jy::ui
