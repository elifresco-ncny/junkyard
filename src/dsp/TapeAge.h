#pragma once
// Tape Age (FIX_PLAN_3 §5): four signal-driven parts - Mistracking, Halo, Crackle, Side-B bleed. No echo, no level wobble, no
// random LFO movement, no stereo drift, no pitch movement, no dropouts. Every part outputs exactly 0 when the music is silent.
#include "Crossover.h"
#include "DelayLine.h"
#include "Random.h"
#include "Smoothers.h"
#include "ValueNoise.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace jy
{
// Calibrated overall gain constants (halo / bleed level targets are measured on the full-mix corpus at -14 LUFS and asserted in
// tests/test_age.cpp). Regenerate with `junkyard_calibrate age`.
inline constexpr float kHaloCal = 1.07f;    // empirical: halo-only RMS re signal = -31 / -25 / -15.5 dB at Age 20 / 50 / 100 (plan: -34 / -25 / -14, +-3)
inline constexpr float kAgeTopMakeupDbPerUnit = 1.0f;   // static makeup above Age amount 1 (see controlTick); 0 at or below 1
inline constexpr float kBleedCal = 1.377f;   // empirical: bleed-only RMS re signal = -51 / -44 / -32 dB at Age 20 / 50 / 100

inline void flushBq (Biquad& b) { if (std::fabs (b.z1) < 1e-12) b.z1 = 0.0; if (std::fabs (b.z2) < 1e-12) b.z2 = 0.0; }

struct OnePoleEnv
{
    float aAtt = 0.1f, aRel = 0.01f, v = 0.0f;
    void set (double fs, double attMs, double relMs)
    {
        aAtt = 1.0f - (float) std::exp (-1.0 / (attMs * 0.001 * fs)); aRel = 1.0f - (float) std::exp (-1.0 / (relMs * 0.001 * fs));
    }
    inline float process (float s) { v += (s > v ? aAtt : aRel) * (s - v); return v; }
    void reset() { v = 0.0f; }
};

// Unit-RMS noise band-limited by a 2-pole high-pass and a 2-pole low-pass (one xorshift, two biquads per sample).
struct BandNoise
{
    Random rng; Biquad hp, lp; float norm = 1.0f;
    void design (double fs, double fHp, double fLp)
    {
        hp.setHighpass (fs, fHp); lp.setLowpass (fs, std::fmin (fLp, fs * 0.45));
        Biquad h = hp, l = lp; h.reset(); l.reset();
        double acc = 0.0; const int n = (int) std::min (fs, 96000.0);
        for (int i = 0; i < n; ++i) { const float y = l.process (h.process (i == 0 ? 1.0f : 0.0f)); acc += (double) y * y; }
        norm = (float) (1.0 / std::sqrt (std::max (1e-30, acc / 3.0)));   // bipolar() has variance 1/3
    }
    void reset (uint64_t seed) { rng.setSeed (seed); hp.reset(); lp.reset(); }
    inline float next() { return norm * lp.process (hp.process (rng.bipolar())); }
};

struct Ramp16
{
    float cur = 0.0f, step = 0.0f;
    void setNow (float v) { cur = v; step = 0.0f; }
    void aim (float target) { step = (target - cur) * (1.0f / 16.0f); }
    inline float next() { cur += step; return cur; }
};

class TapeAge
{
public:
    enum Part { Halo = 1, Crackle = 2, Bleed = 4, Mistrack = 8 };

    void prepare (double sampleRate, int numChannels);
    void reset();
    void setAge (float a) { smAge.setTarget (a); }
    void snapAge (float a) { smAge.setImmediate (a); }
    // test-only: 0 = everything; otherwise only the parts in the mask (the dry signal is muted unless Mistrack is in the mask,
    // which then outputs the mistracked dry signal).
    void setSolo (int mask) { solo = mask; }
    // One frame in place: y[0..nch) is the tape signal right after Drive (the Age sidechain signal xs).
    void process (float* y);

    // diagnostics / test hooks
    std::vector<uint32_t>* eventLog = nullptr;   // crackle event sample indices (test only; push_back allocates)
    long eventCount = 0;
    float currentAge() const { return smAge.getCurrent(); }

private:
    static constexpr int kVoices = 6, kGrains = 3;
    struct Voice
    {
        bool on = false; float env = 0.0f, decay = 0.99f, att = 0.0f, amp = 0.0f; int attLeft = 0; Biquad bp;
    };
    struct Grain { bool on = false; int64_t start = 0; float len = 1.0f, t = 0.0f, step = 1.0f, gain = 0.0f; };
    struct AllpassD { DelayLine line; int len = 1; };

    void controlTick();
    void resetParts();
    void flushStates();
    void startGrain (int c);
    void triggerEvent (int c);

    double sr = 48000.0; int nch = 2; int ctr = 0; long sampleIndex = 0;
    int solo = 0;
    LinearSmoother smAge;
    bool wasActive = false;

    // shared envelopes (stereo-linked)
    OnePoleEnv envFast, envSlow, envSlowB, envHF;
    Biquad hpDet[2];
    int64_t silentCount = 0;
    float stopFade = 1.0f;

    // gains (control rate, 16-sample ramps)
    Ramp16 rDry, rHalo, rBleed;
    float aNow = 0.0f;

    // mistracking
    Biquad shelf[2]; float shelfCut = 0.0f, shelfFc = 6000.0f; bool shelfInit = false;

    // halo
    BandNoise hugN[2], eroN[2], jitN[2];
    Biquad hp150[2], lp8k[2];
    AllpassD ap1[2], ap2[2];
    DelayLine fdn[2][4]; int fdnLen[2][4] {}; float fdnG[2][4] {}, fdnLp[2][4] {}, fdnLpCoef = 0.5f;
    DelayLine jit[2]; float jitBase = 96.0f, jitAmt = 0.0f;
    float haloNorm = 1.0f;
    bool haloWasOn = false;

    // crackle
    Random rngC; ValueNoise vnC; Voice voices[2][kVoices]; Biquad crackLp[2]; int crackIdle = 1 << 20;
    Random rngNoise;
    float cLevel = 0.0f, cOnset = 0.0f, cCluster = 0.0f;

    // side-B bleed
    std::vector<float> hist[2]; int histMask = 0; int64_t wIdx = 0;
    std::vector<float> envRing; int envMask = 0;
    Grain grains[2][kGrains]; int64_t nextGrain[2] {}; Random rngB[2];
    Biquad bHp[2], bLp[2]; DelayLine bJit[2]; BandNoise bJitN[2], bEroN[2];
    float bJitBase = 96.0f, bJitAmt = 0.0f;
    std::vector<float> hannTab;
    bool bleedWasOn = false;
};
} // namespace jy
