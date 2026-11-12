#include "LearnButton.h"

namespace jy::ui
{
namespace
{
// Sizes (px at 100 % scale)
constexpr float kLampD = 7.0f, kButtonD = 18.0f, kTextH = 11.0f, kGap = 8.0f;
constexpr float kGlow = 0.25f;                       // glow alpha when fully lit (light glow)
constexpr float kPulsePeriod = 1.4f;                 // seconds for one full fade up and down
constexpr float kPulseLo = 0.08f, kPulseHi = 0.85f;  // listening lamp range (learned = 1)
const juce::Colour kCollar { 0xff101215 }, kCapTop { 0xff41474e }, kCapBot { 0xff272b30 }, kLampOff { 0xff3a3106 };
const juce::Colour kLampOn = hazard;                 // lit lens matches the panel yellow
}

LearnButton::LearnButton()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LearnButton::setState (int s)
{
    s = juce::jlimit (0, 3, s);
    if (s == state) return;
    state = s;
    const bool pulsing = state == Listening || state == Analysing;
    if (pulsing && ! isTimerRunning()) startTimerHz (60);
    else if (! pulsing) stopTimer();
    repaint();
}

float LearnButton::lampLevel() const
{
    switch (state)
    {
        case Listening:
        case Analysing:
        {
            const double t = (double) juce::Time::getMillisecondCounter() * 0.001;
            const float s = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * t / kPulsePeriod);   // smooth sine fade
            return kPulseLo + (kPulseHi - kPulseLo) * s;
        }
        case Learned: return 1.0f;
        default:      return 0.0f;
    }
}

void LearnButton::paint (juce::Graphics& g)
{
    const float cx = (float) getWidth() * 0.5f, cy = (float) getHeight() * 0.5f;

    // ---- push button: a flat dark collar and a concentric cap with a soft top-to-bottom shade
    const float r = kButtonD * 0.5f;
    const juce::Rectangle<float> collar (cx - r, cy - r, kButtonD, kButtonD);
    g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawEllipse (collar.translated (0.0f, 0.5f), 1.0f);   // faint lower lip
    g.setColour (kCollar); g.fillEllipse (collar);
    const float capD = std::round (kButtonD * 0.70f);
    auto cap = collar.withSizeKeepingCentre (capD, capD);
    if (down) cap = cap.translated (0.0f, 1.0f);
    else { g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillEllipse (cap.translated (0.0f, 1.0f)); }
    const juce::Colour top = down ? kCapTop.darker (0.25f) : (over ? kCapTop.brighter (0.10f) : kCapTop);
    g.setGradientFill (juce::ColourGradient (top, cap.getCentreX(), cap.getY(), down ? kCapBot.darker (0.15f) : kCapBot, cap.getCentreX(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.10f)); g.drawEllipse (cap.reduced (0.5f), 0.8f);   // soft edge, no sheen

    // ---- lamp, left of the button: flat lens in a dark rim, lens and glow fade together
    const float lx = cx - r - kGap - kLampD * 0.5f;
    const juce::Rectangle<float> lens (lx - kLampD * 0.5f, cy - kLampD * 0.5f, kLampD, kLampD);
    const float lit = lampLevel();
    if (lit > 0.0f)
    {
        const auto glow = lens.expanded (kLampD * 0.7f);
        g.setGradientFill (juce::ColourGradient (hazard.withAlpha (kGlow * lit), glow.getCentreX(), glow.getCentreY(),
                                                 hazard.withAlpha (0.0f), glow.getRight(), glow.getCentreY(), true));
        g.fillEllipse (glow);
    }
    g.setColour (bg); g.fillEllipse (lens.expanded (1.5f));
    g.setColour (kLampOff.interpolatedWith (kLampOn, lit)); g.fillEllipse (lens);

    // ---- text, right of the button
    if (auto* l = dynamic_cast<JunkyardLookAndFeel*> (&getLookAndFeel()))
    {
        g.setColour (state == Learned ? text : (over ? text.withAlpha (0.85f) : textDim));
        g.setFont (l->display (kTextH).withExtraKerningFactor (0.08f));
        const float tx = cx + r + kGap;
        g.drawText ("LEARN", juce::Rectangle<float> (tx, 0.0f, (float) getWidth() - tx, (float) getHeight()).translated (0.0f, 1.0f),
                    juce::Justification::centredLeft, false);
    }
}

void LearnButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isRightButtonDown())
    {
        juce::PopupMenu m;
        m.addItem (1, "Learn from input");
        m.addItem (2, "Use default curve", canUseDefault ? canUseDefault() : state == Learned);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<LearnButton> (this)] (int r)
                         {
                             if (safe == nullptr) return;
                             if (r == 1 && safe->onLearn) safe->onLearn();
                             else if (r == 2 && safe->onUseDefault) safe->onUseDefault();
                         });
        return;
    }
    down = true; repaint();
}

void LearnButton::mouseUp (const juce::MouseEvent& e)
{
    if (! down) return;
    down = false; repaint();
    if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();   // click fires on release, like a real button
}
} // namespace jy::ui
