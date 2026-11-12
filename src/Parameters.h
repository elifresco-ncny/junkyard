#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/EngineParams.h"
#include <atomic>
#include <memory>

namespace jy
{
// Frozen parameter IDs (never rename!).
namespace pid
{
inline constexpr const char* inputGain = "input_gain";
inline constexpr const char* codecOn = "codec_on";
inline constexpr const char* codecMode = "codec_mode";
inline constexpr const char* bitrate = "bitrate";
inline constexpr const char* swirl = "swirl";
inline constexpr const char* hfLoss = "hf_loss";
inline constexpr const char* glitch = "glitch";
inline constexpr const char* tapeOn = "tape_on";
inline constexpr const char* wow = "wow";
inline constexpr const char* flutter = "flutter";
inline constexpr const char* saturation = "saturation";
inline constexpr const char* hiss = "hiss";
inline constexpr const char* tapeTone = "tape_tone";
inline constexpr const char* tapeAge = "tape_age";
inline constexpr const char* order = "order";
inline constexpr const char* width = "width";
inline constexpr const char* lowKeep = "low_keep";
inline constexpr const char* lowKeepFreq = "low_keep_freq";
inline constexpr const char* mix = "mix";
inline constexpr const char* outputGain = "output_gain";
} // namespace pid

// Lets the bitrate parameter format itself for the *current* codec mode.
struct ModeProbe { std::atomic<const std::atomic<float>*> raw { nullptr }; int mode() const; };

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (std::shared_ptr<ModeProbe> probe);

// Cached raw-value pointers; read() builds the per-block snapshot (audio-thread safe).
struct ParamRefs
{
    void bind (juce::AudioProcessorValueTreeState& apvts);
    EngineParams read() const;
    std::atomic<float> *inputGain {}, *codecOn {}, *codecMode {}, *bitrate {}, *swirl {}, *hfLoss {}, *glitch {},
        *tapeOn {}, *wow {}, *flutter {}, *saturation {}, *hiss {},
        *tone {}, *age {}, *order {}, *width {}, *lowKeep {}, *lowKeepFreq {}, *mix {}, *outputGain {};
};

// Every automatable parameter ID, in a stable order (used by tests / presets).
const juce::StringArray& allParameterIds();
} // namespace jy
