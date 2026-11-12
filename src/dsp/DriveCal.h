#pragma once
// Fixed Drive level-match curves (generated table + lookups) and the run-time makeup rule. See FIX_PLAN_3 §3.3 and docs/DRIVE_CAL.md.
#include "DriveCalc.h"
#include "DriveCalTable.h"
#include <algorithm>
#include <cmath>

namespace jy::drivecal
{
// Bilinear, clamped at the edges. d in dB (0..30), L in LUFS (-36..0).
inline float lookupT (float d, float L)
{
    const float x = std::fmin ((float) (kNd - 1), std::fmax (0.0f, d));
    const float y = std::fmin ((float) (kNl - 1), std::fmax (0.0f, L - kLmin));
    const int i0 = std::min ((int) x, kNd - 2), j0 = std::min ((int) y, kNl - 2);
    const float fx = x - (float) i0, fy = y - (float) j0;
    const float a = T[i0][j0] + fy * (T[i0][j0 + 1] - T[i0][j0]);
    const float b = T[i0 + 1][j0] + fy * (T[i0 + 1][j0 + 1] - T[i0 + 1][j0]);
    return a + fx * (b - a);
}
inline float lookupD (float d)
{
    const float x = std::fmin ((float) (kNd - 1), std::fmax (0.0f, d));
    const int i0 = std::min ((int) x, kNd - 2);
    return D[i0] + (x - (float) i0) * (D[i0 + 1] - D[i0]);
}

// A learned curve: C[d] (dB, linear in d), the loudness it was measured at and the Input value (dB) at that moment.
struct LearnedCal
{
    bool learned = false;
    float Lcal = -20.0f, inputDbAtLearn = 0.0f;
    float C[kNd] {};
};
inline float lookupC (const float (&C)[kNd], float d)
{
    const float x = std::fmin ((float) (kNd - 1), std::fmax (0.0f, d));
    const int i0 = std::min ((int) x, kNd - 2);
    return C[i0] + (x - (float) i0) * (C[i0 + 1] - C[i0]);
}

// makeupDb = curveDb + T(d, Lbase + delta) - T(d, Lbase), delta = Input now - Input at learn (0 in default mode)
inline float makeupDbFor (const LearnedCal& cal, float driveDb, float inputDbNow)
{
    if (driveDb <= 0.0f) return 0.0f;
    const float Lbase = cal.learned ? cal.Lcal : kDefaultL;
    const float ref = cal.learned ? cal.inputDbAtLearn : 0.0f;
    const float delta = inputDbNow - ref;
    const float curve = cal.learned ? lookupC (cal.C, driveDb) : lookupD (driveDb);
    return std::fmax (-24.0f, std::fmin (24.0f, curve + lookupT (driveDb, Lbase + delta) - lookupT (driveDb, Lbase)));
}
} // namespace jy::drivecal
