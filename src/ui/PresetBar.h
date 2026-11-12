#pragma once
#include "JunkyardLookAndFeel.h"
#include <functional>

namespace jy::ui
{
// [<] Preset name* [>]  [SAVE] [SCRAP]  -- logic is wired by the editor (M8).
class PresetBar : public juce::Component
{
public:
    PresetBar();
    void resized() override;
    void setPresetName (const juce::String& name, bool modified);

    std::function<void()> onPrev, onNext, onSave, onScrap;
    std::function<void (juce::Component&)> onNameClick;
private:
    juce::TextButton prev { "<" }, next { ">" }, save { "SAVE" }, scrap { "SCRAP" }, nameBtn;
};
} // namespace jy::ui
