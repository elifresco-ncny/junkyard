#pragma once
#include "EngineParams.h"
#include "Mdct.h"
#include "BandLayout.h"
#include "Psychoacoustic.h"
#include "BitAllocator.h"
#include "CodecProfiles.h"
#include "Random.h"
#include "ScopeRing.h"
#include <vector>
#include <array>

namespace jy
{
inline int codecHop (double sr) { return sr <= 50000.0 ? 512 : (sr <= 100000.0 ? 1024 : 2048); }

// Streaming MDCT codec simulator. Latency is exactly 2N samples (N = codecHop(sr)).
// Modes: MP3 (holes), Stream (noise fill, SBR-lite, parametric-stereo-lite, PLC/buffering), Broken (corruption, freezes, history glitches).
class CodecEngine
{
public:
    struct Diagnostics
    {
        double frames = 0, codedBins = 0, nonzeroBins = 0, msFrames = 0, bitsUsed = 0, badFrames = 0, bufferingFrames = 0;
        void clear() { *this = {}; }
    };

    void prepare (double sampleRate, int numChannels, int maxBlock);
    int getLatency() const { return 2 * hop; }
    void reset();
    void setParams (const EngineParams& p) { params = p; }
    void process (const float* const* in, float* const* out, int n);
    void seed (uint64_t s) { rng.setSeed (s); }
    void setScope (ScopeRing* r) { scope = r; }

    Diagnostics diag;
    bool passThrough = false; // test hook: MDCT -> IMDCT without quantisation
    bool disableBrokenTrim = false; // test / calibration hook: Broken's fixed level trim forced to 0 dB

private:
    void processFrame();
    void applyGlitch (float glitch, int mode, int kmax);
    void sbrPatch (int c, int kc, int kTop, int mode);
    void pushScope (bool glitched);

    double sr = 48000.0;
    int hop = 512, nch = 2, pos = 0, histFrames = 94;
    EngineParams params;
    Mdct mdct;
    BandLayout bands;
    Psychoacoustic psy;
    std::array<BitAllocator, 2> alloc;
    PsyResult psyRes[2];
    Random rng;
    float reservoir = 0.0f;

    std::array<std::vector<float>, 2> inbuf, outbuf, ola, X, Xorig, Y, Xq, tmp, lastGood;
    std::array<std::vector<float>, 2> hist; // history ring: histFrames * hop per channel
    int histPos = 0, histCount = 0;
    bool isBand[kMaxBands] {};
    float isRatio[kMaxBands] {};

    // glitch / mode state
    // Broken mode level trim: a fixed function of (Bitrate, Glitch) (brokenTrimDb), smoothed over ~4 frames. No level memory.
    float brkTrimSm = 0.0f; bool brkTrimInit = false;
    bool bad = false; int badRun = 0, bufferFrames = 0, bufferCooldown = 0, holdFrames = 0, stereoEvt = 0, stereoEvtLeft = 0;
    bool rectNext = false, haveLastGood = false, glitchMute = false;
    float sbrEnv[2][5] {}; int sbrCtr = 0;
    ScopeRing* scope = nullptr;
    int scopeLo[kScopeBins + 1] {};
};
} // namespace jy
