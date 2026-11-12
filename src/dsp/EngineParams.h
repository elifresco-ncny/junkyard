#pragma once

namespace jy
{
enum CodecMode { ModeMP3 = 0, ModeStream = 1, ModeBroken = 2 };
enum ChainOrder { DigitalThenTape = 0, TapeThenDigital = 1 };

// Plain snapshot of every parameter, taken once per host block. Percentages are 0..1.
struct EngineParams
{
    float inputGainDb = 0.0f, outputGainDb = 0.0f;

    bool codecOn = true;
    int codecMode = ModeMP3;
    float bitrate = 0.5637f; // normalized 0..1
    float swirl = 0.25f, hfLoss = 0.0f, glitch = 0.0f;

    bool tapeOn = false;
    float wow = 0.0f, flutter = 0.0f, saturation = 0.0f /* Drive */, hiss = 0.0f, age = 0.0f;
    float toneHz = 20000.0f; // tape high-frequency rolloff, 20 kHz = off

    int order = DigitalThenTape;
    float width = 1.0f; // 0..1.5
    bool lowKeep = false;
    float lowKeepFreq = 150.0f;
    float mix = 1.0f;
};
} // namespace jy
