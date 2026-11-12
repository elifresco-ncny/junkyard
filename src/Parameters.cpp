#include "Parameters.h"
#include "dsp/CodecProfiles.h"

namespace jy
{
int ModeProbe::mode() const
{
    if (auto* p = raw.load()) return juce::jlimit (0, 2, (int) std::lround (p->load()));
    return 0;
}

namespace
{
using FloatAttr = juce::AudioParameterFloatAttributes;

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float p) { return s * std::pow (e / s, p); },
             [] (float s, float e, float v) { return std::log (juce::jlimit (s, e, v) / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

float parseNumber (const juce::String& t, float fallback)
{
    auto s = t.trim();
    if (s.isEmpty()) return fallback;
    const float v = (float) s.retainCharacters ("0123456789.-+").getDoubleValue();
    return v;
}

juce::String fmtDb (float v) { return juce::String (v, 1) + " dB"; }
juce::String fmtPct (float v) { return juce::String (juce::roundToInt (v)) + " %"; }

std::unique_ptr<juce::AudioParameterFloat> pct (const char* id, const char* name, float def, float maxV = 100.0f)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, maxV, 0.1f), def,
        FloatAttr().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return fmtPct (v); })
            .withValueFromStringFunction ([maxV] (const juce::String& t) { return juce::jlimit (0.0f, maxV, parseNumber (t, 0.0f)); }));
}

std::unique_ptr<juce::AudioParameterFloat> dbParam (const char* id, const char* name, float lo, float hi)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 0.1f), 0.0f,
        FloatAttr().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return fmtDb (v); })
            .withValueFromStringFunction ([lo, hi] (const juce::String& t) { return juce::jlimit (lo, hi, parseNumber (t, 0.0f)); }));
}

juce::String fmtHz (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, 1) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}
float parseHz (const juce::String& t, float lo, float hi)
{
    float v = parseNumber (t, lo);
    if (t.containsIgnoreCase ("k")) v *= 1000.0f;
    return juce::jlimit (lo, hi, v);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (std::shared_ptr<ModeProbe> probe)
{
    using namespace pid;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };

    l.add (dbParam (inputGain, "Input", -24.0f, 24.0f));
    l.add (std::make_unique<juce::AudioParameterBool> (id (codecOn), "Codec", true));
    l.add (std::make_unique<juce::AudioParameterChoice> (id (codecMode), "Mode", juce::StringArray { "MP3", "Stream", "Broken" }, 0));
    l.add (std::make_unique<juce::AudioParameterFloat> (
        id (bitrate), "Bitrate", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5637f,
        FloatAttr()
            .withStringFromValueFunction ([probe] (float v, int)
            {
                const float k = bitrateToKbps (probe->mode(), v);
                return (k < 10.0f ? juce::String (k, 1) : juce::String (juce::roundToInt (k))) + " kbps";
            })
            .withValueFromStringFunction ([probe] (const juce::String& t)
            {
                return kbpsToBitrate (probe->mode(), juce::jmax (0.5f, parseNumber (t, 64.0f)));
            })));
    l.add (pct (swirl, "Swirl", 25.0f));
    l.add (pct (hfLoss, "HF Loss", 0.0f));
    l.add (pct (glitch, "Glitch", 0.0f));
    l.add (std::make_unique<juce::AudioParameterBool> (id (tapeOn), "Tape", false));
    l.add (pct (wow, "Wow", 0.0f));
    l.add (pct (flutter, "Flutter", 0.0f));
    l.add (pct (saturation, "Drive", 0.0f));   // ID "saturation" is frozen; the display name is Drive
    l.add (pct (hiss, "Hiss", 0.0f));
    l.add (pct (tapeAge, "Age", 0.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (
        id (tapeTone), "Tone", logRange (4000.0f, 20000.0f), 20000.0f,
        FloatAttr().withStringFromValueFunction ([] (float v, int) { return v >= 19990.0f ? juce::String ("Off") : fmtHz (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return t.containsIgnoreCase ("off") ? 20000.0f : parseHz (t, 4000.0f, 20000.0f); })));
    l.add (std::make_unique<juce::AudioParameterChoice> (id (order), "Order", juce::StringArray { "Codec > Tape", "Tape > Codec" }, 0));
    l.add (pct (width, "Width", 100.0f, 150.0f));
    l.add (std::make_unique<juce::AudioParameterBool> (id (lowKeep), "Low Keep", false));
    l.add (std::make_unique<juce::AudioParameterFloat> (
        id (lowKeepFreq), "Low Keep Freq", logRange (40.0f, 400.0f), 150.0f,
        FloatAttr().withStringFromValueFunction ([] (float v, int) { return fmtHz (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return parseHz (t, 40.0f, 400.0f); })));
    l.add (pct (mix, "Mix", 100.0f));
    l.add (dbParam (outputGain, "Output", -24.0f, 12.0f));
    return l;
}

void ParamRefs::bind (juce::AudioProcessorValueTreeState& a)
{
    using namespace pid;
    auto r = [&a] (const char* i) { auto* p = a.getRawParameterValue (i); jassert (p != nullptr); return p; };
    inputGain = r (pid::inputGain); codecOn = r (pid::codecOn); codecMode = r (pid::codecMode); bitrate = r (pid::bitrate);
    swirl = r (pid::swirl); hfLoss = r (pid::hfLoss); glitch = r (pid::glitch); 
    tapeOn = r (pid::tapeOn); wow = r (pid::wow);
    flutter = r (pid::flutter); saturation = r (pid::saturation); hiss = r (pid::hiss);
    tone = r (tapeTone); age = r (tapeAge); order = r (pid::order); width = r (pid::width); lowKeep = r (pid::lowKeep);
    lowKeepFreq = r (pid::lowKeepFreq); mix = r (pid::mix); outputGain = r (pid::outputGain);
}

EngineParams ParamRefs::read() const
{
    EngineParams p;
    p.inputGainDb = inputGain->load(); p.outputGainDb = outputGain->load();
    p.codecOn = codecOn->load() > 0.5f; p.codecMode = juce::jlimit (0, 2, (int) std::lround (codecMode->load()));
    p.bitrate = bitrate->load(); p.swirl = swirl->load() * 0.01f; p.hfLoss = hfLoss->load() * 0.01f; p.glitch = glitch->load() * 0.01f;
    p.tapeOn = tapeOn->load() > 0.5f; p.wow = wow->load() * 0.01f; p.flutter = flutter->load() * 0.01f;
    p.saturation = saturation->load() * 0.01f; p.hiss = hiss->load() * 0.01f; p.toneHz = tone->load();
    p.age = age->load() * 0.01f; p.order = juce::jlimit (0, 1, (int) std::lround (order->load()));
    p.width = width->load() * 0.01f; p.lowKeep = lowKeep->load() > 0.5f; p.lowKeepFreq = lowKeepFreq->load();
    p.mix = mix->load() * 0.01f;
    return p;
}

const juce::StringArray& allParameterIds()
{
    using namespace pid;
    static const juce::StringArray ids { inputGain, codecOn, codecMode, bitrate, swirl, hfLoss, glitch,
        tapeOn, wow, flutter, saturation, hiss, tapeAge, tapeTone, order, width, lowKeep, lowKeepFreq,
        mix, outputGain };
    return ids;
}
} // namespace jy
