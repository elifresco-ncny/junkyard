#include "Presets.h"
#include "Parameters.h"
#include "State.h"
#include "ModeMemory.h"
#include "dsp/CodecProfiles.h"
#if JUNKYARD_HAVE_FONTS || JUNKYARD_HAVE_PRESETS
 #include "BinaryData.h"
#endif

namespace jy
{
namespace
{
juce::String sanitize (juce::String n) { return n.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._()-+#&").trim(); }

struct FactoryRes { juce::String file; const char* data; int size; };
std::vector<FactoryRes> factoryResources()
{
    std::vector<FactoryRes> r;
#if JUNKYARD_HAVE_PRESETS
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        juce::String orig (BinaryData::originalFilenames[i]);
        if (! orig.endsWithIgnoreCase (".xml")) continue;
        int size = 0;
        const char* d = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        r.push_back ({ orig, d, size });
    }
    std::sort (r.begin(), r.end(), [] (const FactoryRes& a, const FactoryRes& b) { return a.file < b.file; });
#endif
    return r;
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& a) : apvts (a)
{
    refresh();
    stateRestored();
}

juce::File PresetManager::userDir()
{
#if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Junkyard Audio/Junkyard/Presets");
#else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Junkyard Audio/Junkyard");
#endif
}

std::unique_ptr<juce::XmlElement> PresetManager::factoryXml (int i) const
{
    auto res = factoryResources();
    if (i < 0 || i >= (int) res.size()) return nullptr;
    return juce::parseXML (juce::String::fromUTF8 (res[(size_t) i].data, res[(size_t) i].size));
}

void PresetManager::refresh()
{
    entries.clear();
    auto res = factoryResources();
    int fi = 0;
    for (auto& r : res)
        if (auto xml = juce::parseXML (juce::String::fromUTF8 (r.data, r.size)))
            entries.push_back ({ xml->getStringAttribute ("presetName", r.file.fromFirstOccurrenceOf ("_", false, false)), true, fi++, {} });
    nFactory = (int) entries.size();
    auto dir = userDir();
    if (dir.isDirectory())
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.jyd"))
            entries.push_back ({ f.getFileNameWithoutExtension(), false, -1, f });
    std::sort (entries.begin() + nFactory, entries.end(), [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
}

PresetManager::Vec PresetManager::currentNorm() const
{
    Vec v;
    for (auto& id : allParameterIds()) v.push_back (apvts.getParameter (id)->getValue());
    return v;
}

PresetManager::Vec PresetManager::normFromXml (const juce::XmlElement& xml) const
{
    auto tree = juce::ValueTree::fromXml (xml);
    state::migrate (tree); // older preset files get the same upgrade as saved states
    Vec v;
    for (auto& id : allParameterIds())
    {
        auto* p = apvts.getParameter (id);
        float n = p->getDefaultValue();
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto c = tree.getChild (i);
            if (c.getProperty ("id").toString() == id) { n = p->convertTo0to1 ((float) (double) c.getProperty ("value")); break; }
        }
        v.push_back (juce::jlimit (0.0f, 1.0f, n));
    }
    return v;
}

void PresetManager::applyNorm (const Vec& v)
{
    modemem::clear (apvts.state); // presets / undo never inherit stale per-mode knob memory
    int i = 0;
    for (auto& id : allParameterIds())
    {
        auto* p = apvts.getParameter (id);
        p->beginChangeGesture(); p->setValueNotifyingHost (v[(size_t) i++]); p->endChangeGesture();
    }
}

void PresetManager::pushUndo()
{
    undoStack.push_back (currentNorm());
    if (undoStack.size() > 30) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PresetManager::setName (const juce::String& n) { apvts.state.setProperty (kPresetProp, n, nullptr); }
juce::String PresetManager::currentName() const { return apvts.state.getProperty (kPresetProp, "Init").toString(); }

int PresetManager::currentIndex() const
{
    const auto n = currentName();
    for (size_t i = 0; i < entries.size(); ++i) if (entries[i].name == n) return (int) i;
    return -1;
}

bool PresetManager::loadXml (const juce::XmlElement& xml, const juce::String& fallbackName)
{
    if (! xml.hasTagName (apvts.state.getType())) return false;
    pushUndo();
    const auto v = normFromXml (xml);
    applyNorm (v);
    setName (xml.getStringAttribute ("presetName", fallbackName));
    refNorm = v;
    return true;
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= (int) entries.size()) return false;
    const auto& e = entries[(size_t) index];
    std::unique_ptr<juce::XmlElement> xml = e.factory ? factoryXml (e.factoryIndex) : juce::parseXML (e.file);
    return xml != nullptr && loadXml (*xml, e.name);
}

bool PresetManager::loadByName (const juce::String& name)
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name.equalsIgnoreCase (name)) return load ((int) i);
    return false;
}

int PresetManager::step (int delta)
{
    if (entries.empty()) return -1;
    const int n = (int) entries.size();
    int i = currentIndex();
    i = i < 0 ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
    load (i);
    return i;
}

std::unique_ptr<juce::XmlElement> PresetManager::userPresetXml (const juce::String& name) const
{
    auto xml = state::toXml (apvts);
    if (xml == nullptr) return nullptr;
    for (auto* att : { state::kUiScaleProp, state::kReseedProp, state::kShowStampProp, state::kDriveCalProp, kPresetProp }) xml->removeAttribute (att);   // the calibration belongs to the audio, not the preset
    xml->deleteAllChildElementsWithTagName (modemem::kType); // per-session UI memory, not part of a preset
    xml->setAttribute ("presetName", name);
    return xml;
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = sanitize (rawName);
    if (name.isEmpty()) return false;
    auto dir = userDir();
    if (! dir.createDirectory().wasOk()) return false;
    auto xml = userPresetXml (name);
    if (xml == nullptr) return false;
    auto file = dir.getChildFile (name + ".jyd");
    if (! xml->writeTo (file)) return false;
    setName (name);
    refNorm = currentNorm();
    refresh();
    return true;
}

bool PresetManager::isModified() const
{
    if (refNorm.empty()) return false;
    const auto cur = currentNorm();
    for (size_t i = 0; i < cur.size(); ++i) if (std::fabs (cur[i] - refNorm[i]) > 2e-3f) return true;
    return false;
}

void PresetManager::stateRestored()
{
    const auto n = currentName();
    refNorm.clear();
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == n)
        {
            std::unique_ptr<juce::XmlElement> xml = entries[i].factory ? factoryXml (entries[i].factoryIndex) : juce::parseXML (entries[i].file);
            if (xml) refNorm = normFromXml (*xml);
            break;
        }
    if (refNorm.empty()) refNorm = currentNorm(); // unknown name: treat the restored state as the reference
    undoStack.clear(); redoStack.clear();
}

void PresetManager::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (currentNorm());
    const auto v = undoStack.back(); undoStack.pop_back();
    applyNorm (v);
}

void PresetManager::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (currentNorm());
    const auto v = redoStack.back(); redoStack.pop_back();
    applyNorm (v);
}

// ------------------------------------------------------------------ Scrap
void PresetManager::scrap()
{
    pushUndo();
    modemem::clear (apvts.state);
    auto& r = rng;
    auto setReal = [this] (const char* id, double v) { auto* p = apvts.getParameter (id); p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) v)); p->endChangeGesture(); };
    auto uni = [&r] (float a, float b) { return a + (b - a) * r.nextFloat(); };
    auto rare = [&r] (float p) { return r.nextFloat() < p; };

    const float m = r.nextFloat();
    const int mode = m < 0.4f ? 0 : (m < 0.75f ? 1 : 2);
    setReal (pid::codecOn, 1); setReal (pid::codecMode, mode);
    // sensible bitrates: log-uniform between ~20 % and ~85 % of the knob; extreme ends are rare
    float bn = uni (0.2f, 0.85f);
    if (rare (0.07f)) bn = uni (0.0f, 0.12f);
    if (rare (0.05f)) bn = uni (0.9f, 1.0f);
    setReal (pid::bitrate, bn); // bitrate is stored normalised, so real == normalised; goes through the change gesture
    setReal (pid::swirl, rare (0.08f) ? uni (80, 100) : uni (0, 65));
    setReal (pid::hfLoss, rare (0.2f) ? uni (30, 70) : uni (0, 25));
    setReal (pid::glitch, rare (0.06f) ? uni (60, 90) : (rare (0.4f) ? uni (8, 40) : uni (0, 6)));
    const bool tape = rare (0.4f);
    setReal (pid::tapeOn, tape);
    setReal (pid::wow, uni (3, 25)); setReal (pid::flutter, uni (3, 20)); setReal (pid::saturation, uni (6, 40));
    setReal (pid::hiss, uni (0, 40)); setReal (pid::tapeAge, uni (5, 65));
    setReal (pid::order, rare (0.4f) ? 1 : 0);
    setReal (pid::width, rare (0.2f) ? uni (0, 60) : (rare (0.15f) ? uni (110, 140) : uni (85, 105)));
    // Mix, Output, Input and Low Keep are deliberately left alone.
    setName ("Scrap");
    refNorm = currentNorm();
}
} // namespace jy
