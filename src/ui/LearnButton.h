#pragma once
#include "JunkyardLookAndFeel.h"
#include <functional>

namespace jy::ui
{
// Drive's Learn control, in the Tape panel header above the stripe, centred on the Drive knob:
//   [lamp]  (push button)  LEARN
// The push button is a round cap sunk in a dark collar. The lamp is off for the default curve, fades smoothly up and
// down while listening/analysing (peaking a little below full), and stays fully lit once learned.
// The whole control is clickable: click toggles start/cancel, right-click opens a menu.
class LearnButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum State { Default = 0, Listening = 1, Analysing = 2, Learned = 3 };
    LearnButton();
    void setState (int s);                    // 0..3 (see the processor's LearnUi)
    int getState() const { return state; }
    // The control is laid out around its own horizontal centre, which is the push button's centre.
    static constexpr int kWidth = 130, kHeight = 28;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }

    std::function<void()> onClick, onLearn, onUseDefault;
    std::function<bool()> canUseDefault;

private:
    void timerCallback() override { repaint(); }
    float lampLevel() const;                  // 0 = off .. 1 = fully lit
    int state = Default; bool over = false, down = false;
};
} // namespace jy::ui
