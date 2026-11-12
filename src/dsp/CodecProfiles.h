#pragma once
#include "EngineParams.h"
#include "BrokenTrimTable.h"
#include <algorithm>
#include <cmath>

namespace jy
{
struct ModeRange { float minKbps, maxKbps; };
inline constexpr ModeRange kModeRanges[3] = { { 8.0f, 320.0f }, { 6.0f, 256.0f }, { 1.0f, 64.0f } };

// §6.7: kbps = min * (max/min)^bitrate
inline float bitrateToKbps (int mode, float norm01)
{
    const auto r = kModeRanges[mode < 0 ? 0 : (mode > 2 ? 2 : mode)];
    return r.minKbps * std::pow (r.maxKbps / r.minKbps, norm01);
}
inline float kbpsToBitrate (int mode, float kbps)
{
    const auto r = kModeRanges[mode < 0 ? 0 : (mode > 2 ? 2 : mode)];
    const float v = std::log (kbps / r.minKbps) / std::log (r.maxKbps / r.minKbps);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Broken mode level trim (dB): bilinear lookup in the calibrated table brokentrim::T[glitch 0/0.5/1][bitrate 0, 1/8 .. 1].
// A pure function of the two parameters (no signal dependence). See tools/calibrate and docs/DECISIONS.md.
inline float brokenTrimDb (float bitrateEff, float glitchEff)
{
    const float x = std::fmin (1.0f, std::fmax (0.0f, bitrateEff)) * (float) (brokentrim::kNb - 1);
    const float y = std::fmin (1.0f, std::fmax (0.0f, glitchEff)) * (float) (brokentrim::kNg - 1);
    const int i0 = std::min ((int) x, brokentrim::kNb - 2), j0 = std::min ((int) y, brokentrim::kNg - 2);
    const float fx = x - (float) i0, fy = y - (float) j0;
    const auto& T = brokentrim::T;
    const float a = T[j0][i0] + fx * (T[j0][i0 + 1] - T[j0][i0]);
    const float b = T[j0 + 1][i0] + fx * (T[j0 + 1][i0 + 1] - T[j0 + 1][i0]);
    return a + fy * (b - a);
}

// ---------------------------------------------------------------------------------------------
// Tunable constants (M3 calibration). Defaults live here; tools may override through tuning().
struct CodecTuning
{
    float c0 = 0.5f, c1 = 1.5f, c2 = 1.0f, c3 = 5.0f;  // bit-cost model
    float headerBits = 32.0f;
    float tonalOffsetDb = 6.5f, noiseOffsetDb = 1.5f;  // masking offsets (§6.3)
    float athOffsetDb = 0.0f;
    float sfStepDb = 4.0f, sfMaxDb = 24.0f;
    int outerPasses = 6;
    float reservoirMax = 4088.0f, reservoirUse = 0.5f;
    float msBitrateKbps = 128.0f, msCorr = 0.5f;
    float isScale = 1.0f; // multiplies intensity-stereo start frequency
};
inline CodecTuning& tuning() { static CodecTuning t; return t; }

// LAME-like lowpass vs bitrate (§6.4), interpolated on log-kbps
inline float cutoffHz (float kbps)
{
    static const float k[14] = { 8, 16, 24, 32, 48, 64, 80, 96, 112, 128, 160, 192, 256, 320 };
    static const float f[14] = { 2000, 3400, 4300, 5500, 7600, 10900, 14000, 16800, 16900, 16900, 17500, 18900, 19500, 20000 };
    if (kbps <= k[0]) return std::fmax (300.0f, f[0] * std::sqrt (kbps / k[0]));
    if (kbps >= k[13]) return f[13];
    int i = 0; while (kbps > k[i + 1]) ++i;
    const float t = std::log (kbps / k[i]) / std::log (k[i + 1] / k[i]);
    return f[i] + t * (f[i + 1] - f[i]);
}

// Intensity stereo start frequency (Hz); >= Nyquist means off
inline float intensityStartHz (float kbps)
{
    static const float k[5] = { 8, 16, 32, 64, 96 };
    static const float f[5] = { 2500, 3500, 5000, 9000, 22000 };
    if (kbps <= k[0]) return f[0];
    if (kbps >= k[4]) return 1e9f;
    int i = 0; while (kbps > k[i + 1]) ++i;
    const float t = std::log (kbps / k[i]) / std::log (k[i + 1] / k[i]);
    return (f[i] + t * (f[i + 1] - f[i])) * tuning().isScale;
}
} // namespace jy
