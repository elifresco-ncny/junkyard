#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace jy::state
{
inline constexpr int kCurrentVersion = 3;
inline constexpr const char* kVersionProp = "stateVersion";
inline constexpr const char* kReseedProp = "reseedOnPlay";
inline constexpr const char* kUiScaleProp = "uiScale";
inline constexpr const char* kShowStampProp = "showBuildStamp";
// Learned Drive calibration: "1;<Lcal>;<inputDbAtLearn>;<C0>,...,<C30>". An additive property (no state version bump); absent = default curve.
inline constexpr const char* kDriveCalProp = "driveCal";

// Upgrades an older state tree in place to kCurrentVersion.
void migrate (juce::ValueTree& tree);

std::unique_ptr<juce::XmlElement> toXml (juce::AudioProcessorValueTreeState& apvts);
// Returns false (and leaves state untouched) for empty/garbage input. Missing params fall back to defaults.
bool fromXml (juce::AudioProcessorValueTreeState& apvts, const juce::XmlElement& xml);

void saveBinary (juce::AudioProcessorValueTreeState& apvts, juce::MemoryBlock& dest);
bool loadBinary (juce::AudioProcessorValueTreeState& apvts, const void* data, int size);
} // namespace jy::state
