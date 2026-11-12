#pragma once
// Shared drive-stage level analysis: the calibration tool (fixed curves), the Learn job (learned curve) and the guard test
// all call this, so they run the identical DriveStage code.
#include "DriveStage.h"
#include "Loudness.h"
#include <vector>

namespace jy::drivecal
{
// Loudness (LUFS) at the OUTPUT of a fresh DriveStage per channel run at `driveDb` over the whole buffer, ignoring the first
// `skip` frames (100 ms in the plan). `in[c]` are the channel buffers. `scratch` is caller-owned work memory (avoids per-call allocation).
inline double stageLufsOut (const std::vector<float>* const* in, int nch, double fs, double driveDb, size_t skip,
                            std::vector<float> (&scratch)[2])
{
    const size_t n = in[0]->size();
    for (int c = 0; c < nch; ++c)
    {
        scratch[c].resize (n);
        DriveStage st; st.prepare (fs); st.setDriveDb ((float) driveDb);
        const float* src = in[c]->data(); float* dst = scratch[c].data();
        for (size_t i = 0; i < n; ++i)
        {
            if ((i & 15) == 0) st.setDriveDb ((float) driveDb);   // same control cadence as Tape (a no-op for a constant drive)
            dst[i] = st.process (src[i]);
        }
    }
    const std::vector<float>* o[2] = { &scratch[0], &scratch[1] };
    return loud::lufs (o, nch, skip, n, fs);
}

// makeup in dB that returns the stage output to its input loudness: m = L_in - L_out, clamped to +-24 dB
inline double makeupDb (const std::vector<float>* const* in, int nch, double fs, double driveDb, double lufsIn, std::vector<float> (&scratch)[2])
{
    if (driveDb <= 0.0) return 0.0;
    const size_t skip = (size_t) std::llround (0.1 * fs);
    const double m = lufsIn - stageLufsOut (in, nch, fs, driveDb, skip, scratch);
    return std::fmax (-24.0, std::fmin (24.0, m));
}
} // namespace jy::drivecal
