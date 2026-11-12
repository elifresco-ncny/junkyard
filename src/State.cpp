#include "State.h"
#include <cmath>

namespace jy::state
{
void migrate (juce::ValueTree& tree)
{
    int v = (int) tree.getProperty (kVersionProp, 0);
    if (v < 2)
    {
        // v1 -> v2: Chaos and Dropouts removed; Age no longer darkens (that moved to the new Tone parameter).
        float oldAge = 20.0f; bool hasTone = false;
        for (int i = tree.getNumChildren(); --i >= 0;)
        {
            auto c = tree.getChild (i);
            const auto id = c.getProperty ("id").toString();
            if (id == "chaos" || id == "tape_dropouts") tree.removeChild (i, nullptr);
            else if (id == "tape_age") oldAge = (float) (double) c.getProperty ("value", 20.0);
            else if (id == "tape_tone") hasTone = true;
        }
        if (! hasTone)
        {
            // The old Age lowpass had its -3 dB point at ~0.7x its nominal cutoff; Tone is specified as the -3 dB point.
            const float hz = oldAge <= 0.0f ? 20000.0f : std::max (4000.0f, 0.7f * 18000.0f * std::pow (5000.0f / 18000.0f, oldAge / 100.0f));
            juce::ValueTree t ("PARAM");
            t.setProperty ("id", "tape_tone", nullptr);
            t.setProperty ("value", hz, nullptr);
            tree.appendChild (t, nullptr);
        }
        v = 2;
    }
    if (v < 3)
    {
        // v2 -> v3: Crunch (crush_on, bits, downsample) removed.
        for (int i = tree.getNumChildren(); --i >= 0;)
        {
            const auto id = tree.getChild (i).getProperty ("id").toString();
            if (id == "crush_on" || id == "bits" || id == "downsample") tree.removeChild (i, nullptr);
        }
        v = 3;
    }
    tree.setProperty (kVersionProp, v, nullptr);
}

std::unique_ptr<juce::XmlElement> toXml (juce::AudioProcessorValueTreeState& apvts)
{
    auto st = apvts.copyState();
    st.setProperty (kVersionProp, kCurrentVersion, nullptr);
    return st.createXml();
}

bool fromXml (juce::AudioProcessorValueTreeState& apvts, const juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType())) return false;
    auto tree = juce::ValueTree::fromXml (xml);
    if (! tree.isValid()) return false;
    migrate (tree);

    // Defaults for anything the saved state doesn't mention.
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());

    apvts.replaceState (tree);
    return true;
}

void saveBinary (juce::AudioProcessorValueTreeState& apvts, juce::MemoryBlock& dest)
{
    if (auto xml = toXml (apvts)) juce::AudioProcessor::copyXmlToBinary (*xml, dest);
}

bool loadBinary (juce::AudioProcessorValueTreeState& apvts, const void* data, int size)
{
    if (data == nullptr || size <= 0) return false;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (data, size)) return fromXml (apvts, *xml);
    return false;
}
} // namespace jy::state
