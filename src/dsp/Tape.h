#pragma once
#include "EngineParams.h"
#include "DelayLine.h"
#include "Crossover.h"
#include "Smoothers.h"
#include "Random.h"
#include "ValueNoise.h"
#include "DriveStage.h"
#include "DriveCal.h"
#include "CalPublisher.h"
#include "TapeAge.h"
#include <cmath>

namespace jy
{
inline int tapeBaseDelay (double sr) { return (int) std::lround (0.010 * sr); }

// Tape section (v0.4). Per channel, in this order:
//   wow/flutter modulated delay (10 ms centre = fixed latency)
//   -> Drive (DriveStage, fixed calibrated level curve, no followers)
//   -> Age [Mistracking -> Halo -> + Crackle -> + Side-B bleed]   (TapeAge, signal-driven, stereo-linked)
//   -> + Hiss (follows the music through a noise-reduction-style envelope)
//   -> Tone (playback-head HF loss: softens everything above, including halo, crackle, bleed and hiss).
// There is no moving gain anywhere: every gain is a pure function of the parameters (plus the stored Learn calibration).
class Tape
{
public:
    int getLatency() const { return base; }
    void prepare (double sampleRate, int numChannels, int maxBlock);
    void reset();
    void setParams (const EngineParams& p) { P = p; }
    void process (const float* const* in, float* const* out, int n);
    void setCalPublisher (const drivecal::CalPublisher* p) { calPub = p; }

    // test / diagnostics
    float currentDelay (int ch) const { return lastDelay[ch]; }
    float currentMakeupGain() const { return smMakeup.getCurrent(); }
    long delayClampCount() const { return clampCount; }
    float minDelaySeen() const { return minDelay; }
    float maxDelaySeen() const { return maxDelaySeenV; }
    float maxDelayLimit() const { return (float) base * 1.95f; }
    void setAgeSolo (int mask) { age.setSolo (mask); }
    TapeAge& ageModule() { return age; }
    bool useAdaa = true;

private:
    void updateTone (float hz);
    double sr = 48000.0;
    int base = 480, nch = 2;
    EngineParams P;
    DelayLine line[2];
    ValueNoise vnWow, vnFl, vnFlFast, vnScrape;
    Random hissRng;
    float wowPhase = 0, flPhase = 0, lastDelay[2] {};
    long clampCount = 0; float minDelay = 1e9f, maxDelaySeenV = 0.0f;
    bool firstBlock = true;

    // drive
    LinearSmoother smSat, smTone, smMakeup;   // smTone works on ln(Hz)
    long ctr = 0;                              // per-sample counter: coefficient updates happen on a fixed 16-sample cadence (block-size independent)
    DriveStage stage[2];
    drivecal::LearnedCal cal; uint32_t calVer = 0; const drivecal::CalPublisher* calPub = nullptr;
    int ramp20 = 960, ramp200 = 9600;
    // age
    TapeAge age;
    // tone
    Biquad toneLp[2]; float tonePole[2] {}, toneCoef = 0.5f, toneApplied = -1.0f; bool toneOn = false;
    // hiss: pink, tilted, decorrelated; level follows an envelope of the tape signal (sidechain = after the delay read, before Drive)
    float pk[2][7] {}; float hissNorm = 1.0f; Biquad hissTilt[2]; LinearSmoother smHiss;
    float hEnv = 0.0f, hAttCoef = 0.0f, hRelCoef = 0.0f, gHiss = 0.0f, gHissStep = 0.0f, gHissTarget = 0.0f; int gHissLeft = 0;
};
} // namespace jy
