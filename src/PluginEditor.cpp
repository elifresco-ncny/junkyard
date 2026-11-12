#include "PluginEditor.h"
#include "BuildInfo.h"
#include "Parameters.h"
#include "dsp/CodecProfiles.h"
#include "ui/Knob.h"
#include "ui/Toggle.h"
#include "ui/SectionPanel.h"
#include "ui/DamageScope.h"
#include "ui/PresetBar.h"
#include "ui/LearnButton.h"

namespace jy::ui
{
class GearButton : public juce::Button
{
public:
    GearButton() : juce::Button ("settings") { setTooltip ("Settings."); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat().reduced (5.0f);
        juce::Path p; p.addStar (b.getCentre(), 8, b.getWidth() * 0.34f, b.getWidth() * 0.5f, 0.0f);
        p.addEllipse (b.withSizeKeepingCentre (b.getWidth() * 0.34f, b.getWidth() * 0.34f));
        p.setUsingNonZeroWinding (false);
        g.setColour (over ? hazard : textDim); g.fillPath (p);
    }
};

class EditorContent : public juce::Component
{
public:
    explicit EditorContent (JunkyardAudioProcessor& p)
        : proc (p), apvts (p.apvts),
          codecPanel ("CODEC"), tapePanel ("TAPE"), signalPanel ("SIGNAL"),
          mode (apvts, pid::codecMode, { "MP3", "STREAM", "BROKEN" },
                "Codec type. MP3: gaps and swirl. Stream: smoother, with noise filling the gaps. Broken: corrupted, stuttering data."),
          order (apvts, pid::order, { "CODEC > TAPE", "TAPE > CODEC" }, "CODEC > TAPE: the tape processes the codec's output. TAPE > CODEC: the codec processes the tape's output."),
          bitrate (apvts, pid::bitrate, "BITRATE", "Data rate. Lower sounds more damaged. MP3 8-320 kbps, Stream 6-256 kbps, Broken 1-64 kbps."),
          swirl (apvts, pid::swirl, "SWIRL", "Makes high frequencies flicker on and off, like low-bitrate streaming audio."),
          hfLoss (apvts, pid::hfLoss, "HF LOSS", "Lowers the highest frequency the codec keeps. 100% cuts above about 2.5 kHz."),
          glitch (apvts, pid::glitch, "GLITCH", "How often data is lost. MP3 mutes or repeats, Stream fades or buffers, Broken fills the gap with corrupted audio."),
          wow (apvts, pid::wow, "WOW", "Slow pitch wobble, about 0.8 Hz, up to 2.25%."),
          flutter (apvts, pid::flutter, "FLUTTER", "Fast pitch wobble, about 5-11 Hz, up to 1.5%, with a little scrape."),
          sat (apvts, pid::saturation, "DRIVE", "Drives the tape into distortion. The level is matched automatically. Use Learn to match it to your audio."),
          hiss (apvts, pid::hiss, "HISS", "Tape noise that rises and falls with the music. 0% is off."),
          age (apvts, pid::tapeAge, "AGE", "Worn tape: a fuzzy haze around the sound, soft crackle, a faint reversed ghost, and duller highs on quiet parts."),
          tone (apvts, pid::tapeTone, "TONE", "Rolls off high frequencies on everything the tape section outputs. Fully right is off."),
          width (apvts, pid::width, "WIDTH", "Stereo width of the effect. 0% is mono, 100% is unchanged, 150% is wider."),
          lowKey (apvts, pid::lowKeep, "LOW KEEP", "Keeps everything below Freq clean."),
          lowFreq (apvts, pid::lowKeepFreq, "FREQ", "Low Keep crossover, 40-400 Hz."),
          mix (apvts, pid::mix, "MIX", "Blend between the dry and the processed signal."),
          out (apvts, pid::outputGain, "OUTPUT", "Output level, -24 to +12 dB."),
          in (apvts, pid::inputGain, "INPUT", "Level into the effect, +/-24 dB. Hotter input saturates the tape more.")
    {
        codecPanel.setPower (apvts, pid::codecOn, "Turns the Codec section on or off.");
        tapePanel.setPower (apvts, pid::tapeOn, "Turns the Tape section on or off.");
        for (auto* c : { &codecPanel, &tapePanel, &signalPanel }) addAndMakeVisible (*c);

        codecPanel.addAndMakeVisible (mode); codecPanel.addAndMakeVisible (lcdBox);
        for (auto* k : { &bitrate, &swirl, &hfLoss, &glitch }) codecPanel.addAndMakeVisible (*k);
        for (auto* k : { &wow, &flutter, &sat, &hiss, &age, &tone }) tapePanel.addAndMakeVisible (*k);
        tapePanel.addAndMakeVisible (learn);
        learn.setTooltip ("Play audio and click: listens to 5 seconds of your audio and matches Drive's level to it. Right-click to go back to the default.");
        learn.onClick = [this] { proc.toggleLearn(); };
        learn.onLearn = [this] { if (proc.getLearnUiState() != LearnButton::Listening) proc.startLearn(); };
        learn.onUseDefault = [this] { proc.useDefaultCurve(); };
        learn.canUseDefault = [this] { return proc.isLearned(); };
        signalPanel.addAndMakeVisible (width); signalPanel.addAndMakeVisible (lowKey); signalPanel.addAndMakeVisible (lowFreq);
        for (auto* c : { (juce::Component*) &scope, (juce::Component*) &presetBar, (juce::Component*) &gear, (juce::Component*) &order,
                         (juce::Component*) &mix, (juce::Component*) &out, (juce::Component*) &in })
            addAndMakeVisible (*c);

        codecPanel.layoutBody = [this] (juce::Rectangle<int>)
        {
            mode.setBounds (14, 42, 300, 30);
            bitrate.setBounds (12, 80, 132, 150);
            lcdBox.setBounds (164, 80, 284, 62);
            swirl.setBounds (160, 150, 88, 92); hfLoss.setBounds (256, 150, 88, 92); glitch.setBounds (352, 150, 88, 92);
        };
        tapePanel.layoutBody = [this] (juce::Rectangle<int>)
        {
            int x = 14; for (auto* k : { &wow, &flutter, &sat, &hiss, &age, &tone }) { k->setBounds (x, 46, 90, 100); x += 96; }
            learn.setBounds (sat.getBounds().getCentreX() - LearnButton::kWidth / 2, 16 - LearnButton::kHeight / 2, LearnButton::kWidth, LearnButton::kHeight);   // header row, above the stripe; the push button's centre = the Drive knob's centre
        };
        signalPanel.layoutBody = [this] (juce::Rectangle<int>)
        {
            width.setBounds (28, 46, 100, 100); lowKey.setBounds (160, 52, 130, 24); lowFreq.setBounds (172, 80, 100, 66);
        };

        mode.onSelect = [this] (int i) { proc.switchModeFromUi (i); };
        gear.onClick = [this] { showSettings(); };
        presetBar.onPrev = [this] { proc.presets.step (-1); };
        presetBar.onNext = [this] { proc.presets.step (+1); };
        presetBar.onScrap = [this] { proc.presets.scrap(); };
        presetBar.onSave = [this] { promptSave(); };
        presetBar.onNameClick = [this] (juce::Component& c) { showPresetMenu (c); };
        setOpaque (false);
    }

    ~EditorContent() override { proc.scopeRing.active.store (false); }

    void startScope() { proc.scopeRing.active.store (true); }

    void refresh()
    {
        scope.pull (proc.scopeRing);
        lcdBox.setText (apvts.getParameter (pid::bitrate)->getCurrentValueAsText());
        presetBar.setPresetName (proc.presets.currentName(), proc.presets.isModified());
        learn.setState (proc.getLearnUiState());
        for (auto* k : { &bitrate, &swirl, &hfLoss, &glitch, &wow, &flutter, &sat, &hiss, &age, &tone, &width, &lowFreq, &mix, &out, &in })
            k->updateLive();
    }

    void resized() override
    {
        codecPanel.setBounds (12, 52, 468, 262);
        scope.setBounds (490, 52, 458, 262);
        tapePanel.setBounds (12, 322, 600, 158);
        signalPanel.setBounds (620, 322, 328, 158);
        presetBar.setBounds (260, 10, 560, 32);
        gear.setBounds (918, 8, 34, 34);
        order.setBounds (84, 514, 250, 32);
        in.setBounds (640, 482, 80, 78); mix.setBounds (750, 482, 80, 78); out.setBounds (860, 482, 80, 78);
    }

    void paint (juce::Graphics& g) override
    {
        auto* l = dynamic_cast<JunkyardLookAndFeel*> (&getLookAndFeel());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2024), 0, 0, bg, 0, (float) getHeight(), false));
        g.fillAll();
        g.setColour (juce::Colours::white.withAlpha (0.015f));
        for (int i = -getHeight(); i < getWidth(); i += 9) g.drawLine ((float) i, 0, (float) i + getHeight(), (float) getHeight(), 1.0f);
        if (l)
        {
            g.setColour (hazard); g.setFont (l->display (40.0f));
            g.drawText ("JUNKYARD", 18, 6, 230, 44, juce::Justification::centredLeft, false);   // v1.0: no subtitle; centred on the preset bar row
            g.setColour (textDim); g.setFont (l->display (20.0f));
            g.drawText ("ORDER", 18, 514, 60, 32, juce::Justification::centredLeft, false);
            if (JUNKYARD_DEV_BUILD || proc.getShowStamp())
            {
                g.setColour (textDim.withAlpha (0.8f)); g.setFont (l->mono (10.0f));
                g.drawText (juce::String ("v") + JUNKYARD_VERSION_STRING + (JUNKYARD_DEV_BUILD ? juce::String (" dev  build #") + juce::String (JUNKYARD_BUILD_NUMBER) + " - " + JUNKYARD_BUILD_TIME : juce::String()),
                            getWidth() - 390, 40, 340, 12, juce::Justification::centredRight, false);
            }
        }
    }

    PresetBar presetBar;

private:
    void promptSave()
    {
        auto* aw = new juce::AlertWindow ("Save preset", "Name your preset:", juce::MessageBoxIconType::NoIcon, this);
        aw->addTextEditor ("name", proc.presets.currentName() == "Init" ? juce::String() : proc.presets.currentName());
        aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, safe = juce::Component::SafePointer<EditorContent> (this)] (int r)
        {
            if (safe != nullptr && r == 1) proc.presets.saveUser (aw->getTextEditorContents ("name"));
        }), true);
    }

    void showPresetMenu (juce::Component& target)
    {
        auto& pm = proc.presets;
        pm.refresh();
        juce::PopupMenu m, fac, usr;
        const auto cur = pm.currentName();
        for (int i = 0; i < (int) pm.all().size(); ++i)
        {
            const auto& e = pm.all()[(size_t) i];
            (e.factory ? fac : usr).addItem (100 + i, e.name, true, e.name == cur);
        }
        if (pm.all().size() == (size_t) pm.numFactory()) usr.addItem (-1, "(no user presets yet)", false, false);
        m.addSubMenu ("Factory", fac);
        m.addSubMenu ("User", usr);
        m.addSeparator();
        m.addItem (1, "Undo", pm.canUndo()); m.addItem (2, "Redo", pm.canRedo());
        m.addItem (3, "Show user preset folder");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this, safe = juce::Component::SafePointer<EditorContent> (this)] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 1) proc.presets.undo(); else if (r == 2) proc.presets.redo();
            else if (r == 3) { auto d = jy::PresetManager::userDir(); d.createDirectory(); d.revealToUser(); }
            else if (r >= 100) proc.presets.load (r - 100);
        });
    }

    void showSettings()
    {
        juce::PopupMenu m, sc;
        const float cur = proc.getUiScale();
        for (int pct : { 75, 100, 125, 150 }) sc.addItem (1000 + pct, juce::String (pct) + " %", true, std::abs (cur * 100.0f - (float) pct) < 3.0f);
        m.addSubMenu ("UI scale", sc);
        m.addItem (1, "Reseed on play", true, proc.getReseedOnPlay());
        m.addItem (2, "Show build stamp", ! JUNKYARD_DEV_BUILD, proc.getShowStamp() || JUNKYARD_DEV_BUILD);
        m.addSeparator();
        m.addItem (3, "About Junkyard...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gear), [this, safe = juce::Component::SafePointer<EditorContent> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) proc.setReseedOnPlay (! proc.getReseedOnPlay());
            else if (r == 2) { proc.setShowStamp (! proc.getShowStamp()); repaint(); }
            else if (r == 3)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Junkyard",
                    juce::String ("Junkyard ") + JUNKYARD_VERSION_STRING + "\nbuild #" + juce::String (JUNKYARD_BUILD_NUMBER) + "  " + JUNKYARD_BUILD_DATE + " " + JUNKYARD_BUILD_TIME
                    + "\n\nFree software, AGPL-3.0\n" JUNKYARD_REPO_URL
                    + "\n\nFonts: Bebas Neue, Share Tech Mono (SIL OFL).");
            else if (r >= 1000)
            {
                if (auto* ed = findParentComponentOfClass<juce::AudioProcessorEditor>())
                {
                    const float s = (float) (r - 1000) / 100.0f;
                    ed->setSize (juce::roundToInt (JunkyardEditor::kDesignW * s), juce::roundToInt (JunkyardEditor::kDesignH * s));
                }
            }
        });
    }

    JunkyardAudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    SectionPanel codecPanel, tapePanel, signalPanel;
    SegmentedChoice mode, order;
    LcdReadout lcdBox;
    LearnButton learn;
    DamageScope scope;
    GearButton gear;
    Knob bitrate, swirl, hfLoss, glitch, wow, flutter, sat, hiss, age, tone, width;
    Toggle lowKey;
    Knob lowFreq, mix, out, in;
};
} // namespace jy::ui

JunkyardEditor::JunkyardEditor (JunkyardAudioProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&laf);
    content = std::make_unique<jy::ui::EditorContent> (p);
    addAndMakeVisible (*content);
    constrainer.setFixedAspectRatio ((double) kDesignW / kDesignH);
    constrainer.setSizeLimits (kDesignW * 3 / 4, kDesignH * 3 / 4, kDesignW * 3 / 2, kDesignH * 3 / 2);
    setConstrainer (&constrainer);
    setResizable (true, false);
    const float s = juce::jlimit (0.75f, 1.5f, p.getUiScale());
    setSize (juce::roundToInt (kDesignW * s), juce::roundToInt (kDesignH * s));
    content->startScope();
    startTimerHz (30);
}

JunkyardEditor::~JunkyardEditor()
{
    stopTimer();
    content.reset();
    setLookAndFeel (nullptr);
}

void JunkyardEditor::paint (juce::Graphics& g) { g.fillAll (jy::ui::bg); }

void JunkyardEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    content->setTransform (juce::AffineTransform::scale (s));
    content->setBounds (0, 0, kDesignW, kDesignH);
    proc.setUiScale (s);
}

void JunkyardEditor::refreshNow() { if (content) content->refresh(); }
