#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace jy::modemem
{
// Per-codec-mode memory of Bitrate / Swirl / HF Loss / Glitch, stored as a MODE_MEMORY child of the APVTS state
// (so it saves with the host session). Presets never contain it.
inline constexpr const char* kType = "MODE_MEMORY";
inline constexpr const char* kSlot = "SLOT";
inline constexpr const char* kValid = "valid";
inline constexpr const char* kBitrate = "bitrate";
inline constexpr const char* kSwirl = "swirl";
inline constexpr const char* kHfLoss = "hf_loss";
inline constexpr const char* kGlitch = "glitch";

inline void clear (juce::ValueTree state)
{
    auto m = state.getChildWithName (kType);
    if (m.isValid()) state.removeChild (m, nullptr);
}

// Slot for a mode (0..2), created on demand.
inline juce::ValueTree slot (juce::ValueTree state, int mode)
{
    auto m = state.getChildWithName (kType);
    if (! m.isValid())
    {
        m = juce::ValueTree (kType);
        for (int i = 0; i < 3; ++i)
        {
            juce::ValueTree s (kSlot);
            s.setProperty ("mode", i, nullptr); s.setProperty (kValid, false, nullptr);
            m.appendChild (s, nullptr);
        }
        state.appendChild (m, nullptr);
    }
    return m.getChild (juce::jlimit (0, 2, mode));
}
inline bool isValid (juce::ValueTree state, int mode)
{
    auto m = state.getChildWithName (kType);
    return m.isValid() && (bool) m.getChild (juce::jlimit (0, 2, mode)).getProperty (kValid, false);
}
} // namespace jy::modemem
