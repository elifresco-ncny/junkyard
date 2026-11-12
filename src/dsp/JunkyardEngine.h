#pragma once
#include "EngineParams.h"
#include "CodecEngine.h"
#include "Tape.h"
#include "Crossover.h"
#include "DelayLine.h"
#include "Smoothers.h"
#include "SafetyLimiter.h"
#include "CalPublisher.h"
#include "LearnCapture.h"
#include <array>
#include <atomic>
#include <vector>

namespace jy
{
// One damage section with a constant-latency bypass path and a click-free enable/disable.
template <class T>
struct Section
{
    T proc;
    std::array<DelayLine, 2> dly;
    int lat = 0, nch = 2;
    bool wantOn = false, running = false;
    float g = 0.0f, gStep = 0.01f;
    int hold = 0;
    std::vector<float> gArr;

    void prepare (double sr, int numChannels, int maxBlock)
    {
        nch = numChannels;
        proc.prepare (sr, numChannels, maxBlock);
        lat = proc.getLatency();
        for (auto& d : dly) d.prepare (lat + 4);
        gStep = (float) (1.0 / (0.020 * sr));
        gArr.assign ((size_t) maxBlock, 0.0f);
        reset();
    }
    void reset()
    {
        proc.reset();
        for (auto& d : dly) d.reset();
        running = false; g = 0.0f; hold = 0;
    }
    // Restart the internal state (used after NaN guard / bypass): keep enabled but re-prime.
    void reprime()
    {
        proc.reset();
        if (wantOn) { running = true; g = 0.0f; hold = lat; } else { running = false; g = 0.0f; hold = 0; }
    }
    void process (const float* const* in, float* const* out, float* const* wet, int n)
    {
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i)
                out[c][i] = dly[(size_t) c].process (in[c][i], lat);

        if (wantOn && !running) { proc.reset(); running = true; g = 0.0f; hold = lat; }
        if (! running) return;

        proc.process (in, wet, n);
        const float target = wantOn ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (hold > 0) --hold;
            else if (g < target) g = std::fmin (target, g + gStep);
            else if (g > target) g = std::fmax (target, g - gStep);
            gArr[(size_t) i] = g;
        }
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i)
                out[c][i] += gArr[(size_t) i] * (wet[c][i] - out[c][i]);
        if (! wantOn && g <= 0.0f) running = false;
    }
};

class JunkyardEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    int getLatencySamples() const { return latency; }
    int getNumChannels() const { return nch; }
    void setParams (const EngineParams& p) { params = p; }
    // In-place. Any n (internally chunked to the prepared max block size).
    void process (float* const* io, int n);
    // Host bypass: input delayed by the plugin latency.
    void processBypassed (float* const* io, int n);

    double getSampleRate() const { return sr; }
    LearnCapture& learn() { return learnCap; }
    drivecal::CalPublisher& calPublisher() { return calPub; }
    Tape& tapeForTest() { return tape.proc; }
    void setScope (ScopeRing* r) { codec.proc.setScope (r); }

private:
    void processChunk (float* const* io, int n);
    void prewarm();

    using Buf = std::array<std::vector<float>, 2>;
    static void ptrs (Buf& b, float* (&p)[2]) { p[0] = b[0].data(); p[1] = b[1].data(); }

    static constexpr int kTick = 64;
    double sr = 48000.0;
    int maxBlock = 512, nch = 2, latency = 0, tickPos = 0;
    EngineParams params;

    Section<CodecEngine> codec;
    Section<Tape> tape;

    std::array<DelayLine, 2> dryLine, lowLine;
    std::array<LR4, 2> xo;
    float xoFreq = 150.0f;
    SafetyLimiter limiter;
    LearnCapture learnCap;
    drivecal::CalPublisher calPub;

    Buf dry, x, low, high, a, b, c, wetScratch;
    std::vector<float> gIn, gOut, gMix, gWidth, gLow, gWet, gDuck;
    LinearSmoother smIn, smOut, smMix, smWidth, smLow;


    // wet enable (prewarm after bypass / reset)
    float wetG = 1.0f; int wetHold = 0; float wetStep = 0.01f;
    bool needPrewarm = false;
    // order change duck
    int orderCur = 0; float duckG = 1.0f; float duckStep = 0.01f;
};
} // namespace jy
