#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace jy
{
// Factory presets (compiled-in XML), user presets (.jyd in the user's Audio/Presets folder), Scrap randomiser,
// a small undo/redo for preset loads and Scrap, and the "modified" flag. Message-thread only.
class PresetManager
{
public:
    struct Entry { juce::String name; bool factory = true; int factoryIndex = -1; juce::File file; };
    static constexpr const char* kPresetProp = "presetName";

    explicit PresetManager (juce::AudioProcessorValueTreeState& apvts);

    void refresh();                                   // rescan the user folder
    const std::vector<Entry>& all() const { return entries; }
    int numFactory() const { return nFactory; }

    bool load (int index);
    bool loadByName (const juce::String& name);
    bool loadXml (const juce::XmlElement& xml, const juce::String& fallbackName);
    bool saveUser (const juce::String& name);
    std::unique_ptr<juce::XmlElement> userPresetXml (const juce::String& name) const; // the file content saveUser writes
    void scrap();

    juce::String currentName() const;
    int currentIndex() const;                         // index into all(), -1 if unknown
    bool isModified() const;
    void stateRestored();                             // call after setStateInformation
    int step (int delta);                             // load next/previous preset, wraps. Returns new index.

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();

    static juce::File userDir();
    // Factory preset i parsed (used by tests/tools)
    std::unique_ptr<juce::XmlElement> factoryXml (int i) const;

private:
    using Vec = std::vector<float>;
    Vec currentNorm() const;
    Vec normFromXml (const juce::XmlElement&) const;
    void applyNorm (const Vec&);
    void pushUndo();
    void setName (const juce::String& n);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> entries;
    int nFactory = 0;
    Vec refNorm;
    std::vector<Vec> undoStack, redoStack;
    juce::Random rng { (juce::int64) juce::Time::currentTimeMillis() };
};
} // namespace jy
