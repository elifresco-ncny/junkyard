#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "dsp/CodecProfiles.h"

TEST_CASE ("parameters have frozen ids and nice display strings")
{
    JunkyardAudioProcessor p;
    REQUIRE (jy::allParameterIds().size() == 20);
    for (auto& id : jy::allParameterIds()) REQUIRE (p.apvts.getParameter (id) != nullptr);
    REQUIRE (p.apvts.getParameter ("bitrate")->getText (p.apvts.getParameter ("bitrate")->getDefaultValue(), 0).endsWith ("kbps"));
    REQUIRE (p.apvts.getParameter ("mix")->getText (1.0f, 0) == "100 %");
    REQUIRE (p.apvts.getParameter ("output_gain")->getText (p.apvts.getParameter ("output_gain")->convertTo0to1 (-6.0f), 0) == "-6.0 dB");
    REQUIRE (p.apvts.getParameter ("codec_mode")->getText (0.0f, 0) == "MP3");
    auto* param = p.apvts.getParameter ("bitrate");
    REQUIRE (std::abs (param->getValueForText ("128 kbps") - jy::kbpsToBitrate (0, 128.0f)) < 1e-4f);
}

TEST_CASE ("save -> load round trips every parameter")
{
    JunkyardAudioProcessor a, b;
    int k = 0;
    for (auto& id : jy::allParameterIds())
    {
        auto* rp = a.apvts.getParameter (id);
        const float v = 0.1f + 0.03f * (float) (k++ % 25);
        rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (v))); // legal (snapped) value
    }
    a.setUiScale (1.25f); a.setReseedOnPlay (false);
    juce::MemoryBlock mb; a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    for (auto& id : jy::allParameterIds())
        REQUIRE (std::abs (a.apvts.getParameter (id)->getValue() - b.apvts.getParameter (id)->getValue()) < 1e-5f);
    REQUIRE (b.getUiScale() == 1.25f);
    REQUIRE_FALSE (b.getReseedOnPlay());
}

TEST_CASE ("garbage and empty state do not crash or change params")
{
    JunkyardAudioProcessor p;
    p.apvts.getParameter ("mix")->setValueNotifyingHost (0.3f);
    const float before = p.apvts.getParameter ("mix")->getValue();
    p.setStateInformation (nullptr, 0);
    const char junk[] = "this is not a state";
    p.setStateInformation (junk, sizeof (junk));
    juce::XmlElement wrong ("SOMETHING"); juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (wrong, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (p.apvts.getParameter ("mix")->getValue() == before);
}

TEST_CASE ("state with missing params falls back to defaults; M1 state loads")
{
    JunkyardAudioProcessor p;
    p.apvts.getParameter ("mix")->setValueNotifyingHost (0.2f);
    p.apvts.getParameter ("swirl")->setValueNotifyingHost (0.9f);
    // A minimal "old" state: only one param, no version attribute.
    juce::XmlElement root ("JUNKYARD");
    auto* e = root.createNewChildElement ("PARAM"); e->setAttribute ("id", "swirl"); e->setAttribute ("value", 50.0);
    juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (root, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (std::abs (p.apvts.getParameter ("swirl")->convertFrom0to1 (p.apvts.getParameter ("swirl")->getValue()) - 50.0f) < 0.2f);
    REQUIRE (p.apvts.getParameter ("mix")->getValue() == p.apvts.getParameter ("mix")->getDefaultValue());
}

TEST_CASE ("processor mono and stereo layouts process audio with reported latency")
{
    for (int nch : { 1, 2 })
    {
        JunkyardAudioProcessor p;
        juce::AudioProcessor::BusesLayout lay;
        lay.inputBuses.add (nch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        lay.outputBuses.add (nch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        REQUIRE (p.setBusesLayout (lay));
        p.setRateAndBufferSizeDetails (48000, 128);
        p.prepareToPlay (48000, 128);
        REQUIRE (p.getLatencySamples() == 1504);
        juce::AudioBuffer<float> b (nch, 128); juce::MidiBuffer m;
        for (int i = 0; i < 40; ++i) { b.clear(); p.processBlock (b, m); }
        REQUIRE (std::isfinite (b.getSample (0, 0)));
    }
}

// ------------------------------------------------------------------ M8: presets
TEST_CASE ("factory presets load, change parameters, and Init equals the defaults")
{
    JunkyardAudioProcessor p;
    auto& pm = p.presets;
    REQUIRE (pm.numFactory() >= 18);
    REQUIRE (pm.all()[0].name == "Init");
    REQUIRE_FALSE (pm.isModified());
    for (int i = 0; i < pm.numFactory(); ++i)
    {
        REQUIRE (pm.load (i));
        REQUIRE (pm.currentName() == pm.all()[(size_t) i].name);
        REQUIRE_FALSE (pm.isModified());
    }
    REQUIRE (pm.loadByName ("walkman"));
    REQUIRE (p.apvts.getRawParameterValue ("tape_on")->load() > 0.5f);
    REQUIRE (p.apvts.getRawParameterValue ("codec_mode")->load() == 0.0f);
    // modified flag
    p.apvts.getParameter ("swirl")->setValueNotifyingHost (0.77f);
    REQUIRE (pm.isModified());
    // programs are exposed too
    REQUIRE (p.getNumPrograms() == pm.numFactory());
    REQUIRE (p.getProgramName (0) == "Init");
}

TEST_CASE ("Init preset has exactly the default parameter values")
{
    JunkyardAudioProcessor a, b;
    b.presets.load (3); b.presets.load (0);
    for (auto& id : jy::allParameterIds())
        REQUIRE (std::abs (a.apvts.getParameter (id)->getValue() - b.apvts.getParameter (id)->getValue()) < 2e-3f);
}

TEST_CASE ("scrap randomises sensibly, leaves mix/output/input/low keep alone, and can be undone")
{
    JunkyardAudioProcessor p;
    p.presets.load (2);
    p.apvts.getParameter ("mix")->setValueNotifyingHost (0.4f); p.apvts.getParameter ("input_gain")->setValueNotifyingHost (0.6f);
    p.apvts.getParameter ("low_keep")->setValueNotifyingHost (1.0f);
    const float mix0 = p.apvts.getParameter ("mix")->getValue(), in0 = p.apvts.getParameter ("input_gain")->getValue();
    const float sw0 = p.apvts.getParameter ("swirl")->getValue();
    int differs = 0;
    for (int i = 0; i < 40; ++i)
    {
        p.presets.scrap();
        REQUIRE (p.apvts.getParameter ("mix")->getValue() == mix0);
        REQUIRE (p.apvts.getParameter ("input_gain")->getValue() == in0);
        REQUIRE (p.apvts.getParameter ("low_keep")->getValue() == 1.0f);
        REQUIRE (p.apvts.getRawParameterValue ("codec_on")->load() > 0.5f);
        differs += std::abs (p.apvts.getParameter ("swirl")->getValue() - sw0) > 0.01f;
    }
    REQUIRE (differs > 20);
    p.presets.undo();
    p.presets.undo();
    REQUIRE (p.presets.canRedo());
}

TEST_CASE ("user presets: save, appear in the list, reload")
{
    JunkyardAudioProcessor p;
    p.apvts.getParameter ("swirl")->setValueNotifyingHost (0.63f);
    const auto name = juce::String ("zz_test_preset_") + juce::String (juce::Random::getSystemRandom().nextInt (100000));
    // user dir may be sandbox-protected during tests: only assert when the write works
    if (p.presets.saveUser (name))
    {
        REQUIRE (p.presets.currentName() == name);
        REQUIRE_FALSE (p.presets.isModified());
        p.presets.load (0);
        REQUIRE (p.presets.loadByName (name));
        REQUIRE (std::abs (p.apvts.getParameter ("swirl")->getValue() - 0.63f) < 0.01f);
        jy::PresetManager::userDir().getChildFile (name + ".jyd").deleteFile();
    }
}

TEST_CASE ("preset name survives save/load of plugin state")
{
    JunkyardAudioProcessor a, b;
    a.presets.loadByName ("Walkman");
    juce::MemoryBlock mb; a.getStateInformation (mb);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (b.presets.currentName() == "Walkman");
    REQUIRE_FALSE (b.presets.isModified());
}

TEST_CASE ("v1 states and user presets migrate: chaos/dropouts dropped, tone derived from the old age")
{
    // v1 user preset (no tone, with removed params)
    juce::XmlElement x ("JUNKYARD"); x.setAttribute ("stateVersion", 1); x.setAttribute ("presetName", "Old");
    for (auto [id, v] : { std::pair<const char*, double> { "tape_age", 40.0 }, { "chaos", 50.0 }, { "tape_dropouts", 30.0 }, { "tape_on", 1.0 } })
    { auto* c = x.createNewChildElement ("PARAM"); c->setAttribute ("id", id); c->setAttribute ("value", v); }
    JunkyardAudioProcessor p;
    REQUIRE (p.presets.loadXml (x, "Old"));
    auto* tone = p.apvts.getParameter ("tape_tone");
    const float hz = tone->convertFrom0to1 (tone->getValue());
    const float expect = 0.7f * 18000.0f * std::pow (5000.0f / 18000.0f, 0.4f);
    REQUIRE (std::abs (hz - expect) < 0.02f * expect);
    REQUIRE (p.apvts.getParameter ("chaos") == nullptr);
    // age 0 -> tone off
    juce::XmlElement y ("JUNKYARD"); auto* c = y.createNewChildElement ("PARAM"); c->setAttribute ("id", "tape_age"); c->setAttribute ("value", 0.0);
    REQUIRE (p.presets.loadXml (y, "Old2"));
    REQUIRE (tone->convertFrom0to1 (tone->getValue()) > 19990.0f);
    REQUIRE (tone->getText (tone->getValue(), 0) == "Off");
}

TEST_CASE ("M1 fixture gets a tone derived from its age and saves as version 3")
{
    auto xml = juce::parseXML (juce::File (JUNKYARD_FIXTURES).getChildFile ("m1_state.xml"));
    juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (*xml, mb);
    JunkyardAudioProcessor p; p.setStateInformation (mb.getData(), (int) mb.getSize());
    auto* tone = p.apvts.getParameter ("tape_tone");
    const float expect = 0.7f * 18000.0f * std::pow (5000.0f / 18000.0f, 0.6f);
    REQUIRE (std::abs (tone->convertFrom0to1 (tone->getValue()) - expect) < 0.02f * expect);
    juce::MemoryBlock out; p.getStateInformation (out);
    auto saved = juce::AudioProcessor::getXmlFromBinary (out.getData(), (int) out.getSize());
    REQUIRE (saved->getIntAttribute ("stateVersion") == 3);
}

TEST_CASE ("v2 state with Crunch params loads, drops them and saves as v3")
{
    juce::XmlElement x ("JUNKYARD"); x.setAttribute ("stateVersion", 2);
    for (auto [id, v] : std::vector<std::pair<const char*, double>> { { "crush_on", 1 }, { "bits", 6 }, { "downsample", 8000 }, { "swirl", 44 }, { "tape_tone", 9000 } })
    { auto* c = x.createNewChildElement ("PARAM"); c->setAttribute ("id", id); c->setAttribute ("value", v); }
    juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (x, mb);
    JunkyardAudioProcessor p; p.setStateInformation (mb.getData(), (int) mb.getSize());
    auto* sw = p.apvts.getParameter ("swirl");
    REQUIRE (std::abs (sw->convertFrom0to1 (sw->getValue()) - 44.0f) < 0.2f);
    juce::MemoryBlock out; p.getStateInformation (out);
    auto saved = juce::AudioProcessor::getXmlFromBinary (out.getData(), (int) out.getSize());
    REQUIRE (saved->getIntAttribute ("stateVersion") == 3);
    for (auto* c : saved->getChildIterator()) { const auto id = c->getStringAttribute ("id"); REQUIRE (id != "crush_on"); REQUIRE (id != "bits"); REQUIRE (id != "downsample"); }
}
