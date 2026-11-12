// junkyard_render in.wav out.wav [--set id=value]... [--bitrate kbps] [--mode mp3|stream|broken] [--keep-latency]
// Values for --set are in the parameter's real units (e.g. swirl=40, mix=100, tape_age=50).
// bitrate is entered in kbps. By default the plugin latency is compensated so in/out line up.
#include "PluginProcessor.h"
#include "dsp/CodecProfiles.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::cerr << "usage: junkyard_render in.wav out.wav [--set id=value] [--bitrate kbps] [--mode mp3|stream|broken] [--preset name]\n"; return 1; }
    JunkyardAudioProcessor proc;
    bool keepLatency = false;
    std::vector<std::pair<juce::String, double>> sets;
    juce::String presetName;
    for (int i = 3; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a == "--keep-latency") keepLatency = true;
        else if (a == "--preset" && i + 1 < argc) presetName = argv[++i];
        else if (a == "--preset-file" && i + 1 < argc)
        {
            auto xml = juce::parseXML (juce::File (juce::String (argv[++i])));
            if (! xml || ! proc.presets.loadXml (*xml, "file")) { std::cerr << "cannot load preset file\n"; return 2; }
        }
        else if (a == "--mode" && i + 1 < argc)
        {
            juce::String m (argv[++i]);
            sets.push_back ({ "codec_mode", m.equalsIgnoreCase ("stream") ? 1 : (m.equalsIgnoreCase ("broken") ? 2 : 0) });
        }
        else if (a == "--tune" && i + 1 < argc)   // codec tuning override, e.g. --tune c0=0.4
        {
            juce::String kv (argv[++i]); auto k = kv.upToFirstOccurrenceOf ("=", false, false); const float v = (float) kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            auto& t = jy::tuning();
            if (k == "c0") t.c0 = v; else if (k == "c1") t.c1 = v; else if (k == "c2") t.c2 = v; else if (k == "c3") t.c3 = v;
            else if (k == "tonalOffsetDb") t.tonalOffsetDb = v; else if (k == "noiseOffsetDb") t.noiseOffsetDb = v;
            else if (k == "athOffsetDb") t.athOffsetDb = v; else if (k == "reservoirUse") t.reservoirUse = v;
            else if (k == "sfStepDb") t.sfStepDb = v; else if (k == "outerPasses") t.outerPasses = (int) v;
            else if (k == "msBitrateKbps") t.msBitrateKbps = v; else if (k == "isScale") t.isScale = v;
            else if (k == "headerBits") t.headerBits = v;
            else { std::cerr << "unknown tune key " << k << "\n"; return 2; }
        }
        else if (a == "--bitrate" && i + 1 < argc) sets.push_back ({ "@kbps", juce::String (argv[++i]).getDoubleValue() });
        else if (a == "--set" && i + 1 < argc)
        {
            juce::String kv (argv[++i]);
            sets.push_back ({ kv.upToFirstOccurrenceOf ("=", false, false), kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue() });
        }
    }
    auto setReal = [&] (const juce::String& id, double v)
    {
        auto* p = proc.apvts.getParameter (id);
        if (p == nullptr) { std::cerr << "unknown parameter " << id << "\n"; std::exit (2); }
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    };
    if (presetName.isNotEmpty() && ! proc.loadPresetByName (presetName)) { std::cerr << "unknown preset " << presetName << "\n"; return 2; }
    for (auto& [id, v] : sets)
    {
        if (id == "@kbps") continue;
        setReal (id, v);
    }
    for (auto& [id, v] : sets)
        if (id == "@kbps")
        {
            const int mode = juce::jlimit (0, 2, (int) std::lround (proc.apvts.getRawParameterValue ("codec_mode")->load()));
            proc.apvts.getParameter ("bitrate")->setValueNotifyingHost (jy::kbpsToBitrate (mode, (float) v));
        }

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (juce::String (argv[1]))));
    if (! rd) { std::cerr << "cannot read " << argv[1] << "\n"; return 1; }
    const int nch = rd->numChannels >= 2 ? 2 : 1;
    const int64_t len = rd->lengthInSamples;
    juce::AudioBuffer<float> in ((int) nch, (int) len);
    rd->read (&in, 0, (int) len, 0, true, nch > 1);

    juce::AudioProcessor::BusesLayout lay;
    lay.inputBuses.add (nch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    lay.outputBuses.add (nch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    proc.setBusesLayout (lay);
    proc.setRateAndBufferSizeDetails (rd->sampleRate, 512);
    proc.prepareToPlay (rd->sampleRate, 512);
    const int L = proc.getLatencySamples();
    const int total = (int) len + L;
    juce::AudioBuffer<float> out (nch, total);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = std::min (512, total - pos);
        juce::AudioBuffer<float> blk (nch, n);
        blk.clear();
        for (int c = 0; c < nch; ++c)
            if (pos < len) blk.copyFrom (c, 0, in, c, pos, std::min (n, (int) len - pos));
        proc.processBlock (blk, midi);
        for (int c = 0; c < nch; ++c) out.copyFrom (c, pos, blk, c, 0, n);
    }
    juce::File of { juce::String (argv[2]) }; of.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> wr (wav.createWriterFor (new juce::FileOutputStream (of), rd->sampleRate, (unsigned) nch, 24, {}, 0));
    if (! wr) { std::cerr << "cannot write " << argv[2] << "\n"; return 1; }
    const int off = keepLatency ? 0 : L;
    wr->writeFromAudioSampleBuffer (out, off, total - off);
    std::cout << "latency " << L << " samples; wrote " << argv[2] << "\n";
    return 0;
}
