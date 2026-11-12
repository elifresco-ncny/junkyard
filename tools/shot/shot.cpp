// junkyard_shot out.png [scale=1.0] [--preset "Name"] [--set id=value]... [--audio in.wav]
// Renders the editor offscreen (no window) after pushing audio through the plugin so the Damage Scope has content.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/CodecProfiles.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <random>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cerr << "usage: junkyard_shot out.png [scale] [--set id=value] [--audio in.wav]\n"; return 1; }
    float scale = 1.0f; juce::String audioPath; bool doLearn = false, midLearn = false;
    JunkyardAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000, 512);
    for (int i = 2; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a == "--set" && i + 1 < argc)
        {
            juce::String kv (argv[++i]); auto id = kv.upToFirstOccurrenceOf ("=", false, false); const double v = kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            if (id == "bitrate" ) { const int m = (int) proc.apvts.getRawParameterValue ("codec_mode")->load(); proc.apvts.getParameter ("bitrate")->setValueNotifyingHost (jy::kbpsToBitrate (m, (float) v)); }
            else if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
        }
        else if (a == "--audio" && i + 1 < argc) audioPath = argv[++i];
        else if (a == "--preset" && i + 1 < argc) { if (! proc.presets.loadByName (argv[++i])) { std::cerr << "no preset " << argv[i] << "\n"; return 1; } }   // factory/user preset by name
        else if (a == "--learn") doLearn = true;             // run a Drive Learn first: the button shows the learned state
        else if (a == "--listening") midLearn = true;        // snapshot while Learn is still listening
        else scale = a.getFloatValue();
    }
    proc.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (juce::roundToInt (960 * scale), juce::roundToInt (560 * scale));

    juce::AudioBuffer<float> src;
    if (audioPath.isNotEmpty())
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (audioPath)));
        if (rd) { src.setSize (2, (int) rd->lengthInSamples); rd->read (&src, 0, (int) rd->lengthInSamples, 0, true, true); }
    }
    if (doLearn || midLearn) proc.startLearn();
    juce::MidiBuffer midi; std::mt19937 g (3); std::normal_distribution<float> d (0.0f, 0.1f);
    const int blocks = midLearn ? 200 : std::min (900, src.getNumSamples() > 0 ? src.getNumSamples() / 512 : 900);
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> blk (2, 512);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                blk.setSample (c, i, src.getNumSamples() > 0 ? src.getSample (c, b * 512 + i)
                                                          : 0.3f * std::sin (0.04f * (float) (b * 512 + i) * (1.0f + 0.5f * std::sin (b * 0.01f))) + d (g));
        proc.processBlock (blk, midi);
        if (doLearn && b % 8 == 0) proc.learnTick();
        if (b % 4 == 0) if (auto* e = dynamic_cast<JunkyardEditor*> (ed.get())) e->refreshNow();
    }
    for (int k = 0; doLearn && k < 200 && ! proc.isLearned(); ++k) { proc.learnTick(); juce::Thread::sleep (20); }
    if (auto* e = dynamic_cast<JunkyardEditor*> (ed.get())) e->refreshNow();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f { juce::String (argv[1]) }; f.deleteFile();
    juce::PNGImageFormat png; juce::FileOutputStream os (f);
    if (os.openedOk()) png.writeImageToStream (img, os);
    std::cout << "wrote " << argv[1] << " " << img.getWidth() << "x" << img.getHeight() << "\n";
    ed.reset();
    return 0;
}
