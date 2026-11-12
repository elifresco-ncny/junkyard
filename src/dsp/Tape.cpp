#include "Tape.h"
#include <algorithm>

namespace jy
{
namespace
{
// The 0.75 sine + 0.25 value-noise shape peaks at about 0.8 of its nominal range when measured as speed deviation, so the depth is scaled up
// to make Wow 100 % read its nominal +-2.25 %. The scaled worst case (base + 430 + 22 samples at 48 kHz) still fits the 1.95 * base limit.
constexpr float kWowShapeScale = 1.2f;
// Hiss envelope knee (dBFS on the detector). The plan says -30; this detector (3 ms attack / 100 ms release on |x|) reads about 5 dB above
// the RMS of noise-like music, so the knee is -24.5 to give the specified 'about 10 dB less hiss for -40 dBFS pink than for -10 dBFS pink'.
constexpr float kHissKneeDb = -24.5f;
inline float pinkStep (float* b, float w)
{
    b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
    b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
    const float p = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f; b[6] = w * 0.115926f;
    return p;
}
}

void Tape::prepare (double sampleRate, int numChannels, int)
{
    sr = sampleRate; nch = numChannels; base = tapeBaseDelay (sr);
    for (auto& l : line) l.prepare (2 * base + 64);
    smTone.prepare (sr, 30.0);
    smSat.prepare (sr, 30.0); smHiss.prepare (sr, 30.0); smMakeup.prepare (sr, 20.0);
    ramp20 = std::max (1, (int) std::lround (0.020 * sr)); ramp200 = std::max (1, (int) std::lround (0.200 * sr));
    hAttCoef = 1.0f - (float) std::exp (-1.0 / (0.003 * sr)); hRelCoef = 1.0f - (float) std::exp (-1.0 / (0.100 * sr));
    {   // normalise the pink generator to unit RMS
        Random r (99); float b[7] {}; double acc = 0; const int n = 200000; Biquad tilt; tilt.setHighShelf (sr, 4000.0, 4.0);
        for (int i = 0; i < n; ++i) { const float p = tilt.process (pinkStep (b, r.bipolar())); acc += (double) p * p; }
        hissNorm = (float) (1.0 / std::sqrt (acc / n));
    }
    for (auto& s : stage) s.prepare (sr);
    age.prepare (sr, nch);
    reset();
}

void Tape::reset()
{
    for (auto& l : line) l.reset();
    ctr = 0; smTone.setImmediate (std::log (20000.0f));
    hissRng.setSeed (0x4849535353ull);
    vnWow.prepare (sr, 11); vnFl.prepare (sr, 12); vnFlFast.prepare (sr, 13); vnScrape.prepare (sr, 15);
    wowPhase = 0; flPhase = 0; lastDelay[0] = lastDelay[1] = (float) base;
    clampCount = 0; minDelay = 1e9f; maxDelaySeenV = 0.0f;
    smSat.setImmediate (0.0f); smHiss.setImmediate (0.0f); smMakeup.setImmediate (1.0f);
    hEnv = 0.0f; gHiss = gHissStep = gHissTarget = 0.0f; gHissLeft = 0;
    for (int c = 0; c < 2; ++c)
    {
        stage[c].adaa = useAdaa; stage[c].reset();
        hissTilt[c].setHighShelf (sr, 4000.0, 4.0); hissTilt[c].reset();
        tonePole[c] = 0; toneLp[c].reset();
        for (auto& v : pk[c]) v = 0;
    }
    age.reset();
    toneApplied = -1.0f; toneOn = false;
    cal = {}; calVer = 0; firstBlock = true;
}

void Tape::updateTone (float hz)
{
    toneApplied = hz;
    // Tone is the -3 dB point: the 2-pole (Q 0.6) + 1-pole (at 1.4x) combination sits at about 0.7 of its nominal cutoff.
    const double fc = std::min ((double) hz * 1.43, sr * 0.45);
    for (auto& f : toneLp) { const auto k = f; f.setLowpass (sr, fc, 0.6); f.z1 = k.z1; f.z2 = k.z2; }
    toneCoef = 1.0f - std::exp (-2.0f * 3.14159265f * std::min ((float) fc * 1.4f, (float) sr * 0.45f) / (float) sr);
}

void Tape::process (const float* const* in, float* const* out, int n)
{
    const float pi2 = 6.2831853f, fs = (float) sr;

    // ---- block-level targets
    const bool toneTargetOff = P.toneHz >= 19990.0f;
    smTone.setTarget (std::log (toneTargetOff ? 20000.0f : std::max (4000.0f, P.toneHz)));
    if (firstBlock)
    {   // start at the parameter values (renders and transport restarts begin in the right place; nothing to fade in)
        smSat.setImmediate (P.saturation); age.snapAge (P.age);
        smHiss.setImmediate (P.hiss <= 0.0f ? 0.0f : std::pow (10.0f, (-80.0f + 50.0f * P.hiss) / 20.0f));
        firstBlock = false;
        if (calPub != nullptr) { calVer = calPub->versionNow(); calPub->read (cal); }
        smMakeup.setImmediate (std::pow (10.0f, drivecal::makeupDbFor (cal, 30.0f * P.saturation, P.inputGainDb) / 20.0f));
    }
    smSat.setTarget (P.saturation);
    smHiss.setTarget (P.hiss <= 0.0f ? 0.0f : std::pow (10.0f, (-80.0f + 50.0f * P.hiss) / 20.0f));
    age.setAge (P.age);
    // Wow: +-2.25 % peak speed at 100 % (^1.5; was +-3 % until v0.4.4), 0.8 Hz. Flutter: +-1.5 % at 100 % (^1.5), 8 Hz centre, plus a small fast "scrape" (40-80 Hz).
    const float wowDepthEff = P.wow > 0.0f ? kWowShapeScale * 0.0225f * std::pow (P.wow, 1.5f) : 0.0f;
    const float flDepthEff = P.flutter > 0.0f ? 0.015f * std::pow (P.flutter, 1.5f) : 0.0f;
    const float scrapeDepth = 0.0008f * P.flutter;
    const float maxDelay = (float) base * 1.95f;
    const float mainBase = (float) base;

    for (int i = 0; i < n; ++i)
    {
        // ---- modulators
        const float fw = 0.8f * (1.0f + 0.2f * vnWow.next (0.1f));
        wowPhase += pi2 * fw / fs; if (wowPhase > pi2) wowPhase -= pi2;
        const float mw = 0.75f * std::sin (wowPhase) + 0.25f * vnFl.next (0.7f);
        const float ff = 8.0f * (1.0f + 0.35f * vnFlFast.next (0.3f));
        flPhase += pi2 * ff / fs; if (flPhase > pi2) flPhase -= pi2;
        const float mf = 0.70f * std::sin (flPhase) + 0.25f * std::sin (2.0f * flPhase + 1.1f) + 0.05f * vnFlFast.next (20.0f);
        const float scr = scrapeDepth > 0.0f ? vnScrape.next (60.0f) : 0.0f;
        const float Aw = wowDepthEff * fs / (pi2 * fw), Af = flDepthEff * fs / (pi2 * ff), As = scrapeDepth * fs / (pi2 * 60.0f);

        const float sat = smSat.next(), hissLvl = smHiss.next(), tl = smTone.next();
        const bool satOn = sat > 0.0f || P.saturation > 0.0f;
        const float driveDb = 30.0f * sat;
        if ((ctr++ & 15) == 0)
        {
            const float hz = std::exp (tl);
            toneOn = ! (toneTargetOff && smTone.getCurrent() >= std::log (19990.0f) && ! smTone.isSmoothing());
            if (toneOn && std::fabs (hz - toneApplied) > 0.003f * hz) updateTone (hz);
            if (satOn)
            {
                for (int c = 0; c < nch; ++c) stage[c].setDriveDb (driveDb);
                bool longRamp = false;
                if (calPub != nullptr && calPub->versionNow() != calVer) { calVer = calPub->versionNow(); calPub->read (cal); longRamp = true; }
                const float mk = std::pow (10.0f, drivecal::makeupDbFor (cal, driveDb, P.inputGainDb) / 20.0f);
                smMakeup.rampSamples = longRamp ? ramp200 : ramp20;
                smMakeup.setTarget (mk);
            }
            else if (calPub != nullptr && calPub->versionNow() != calVer) { calVer = calPub->versionNow(); calPub->read (cal); }
            // hiss: noise-reduction-style breathing. Full hiss while the music is above -30 dBFS, -1 dB per dB below, gone below -80 dBFS.
            {
                const float envDb = 20.0f * std::log10 (std::max (hEnv, 1e-9f));
                const float relDb = std::min (0.0f, envDb - kHissKneeDb);   // full hiss above the knee, -1 dB per dB below
                const float fade = std::min (1.0f, std::max (0.0f, (envDb + 80.0f) / 10.0f));
                gHissTarget = fade > 0.0f ? std::pow (10.0f, relDb / 20.0f) * fade : 0.0f;
                gHissLeft = 16; gHissStep = (gHissTarget - gHiss) / 16.0f;
            }
        }
        if (gHissLeft > 0) { gHiss += gHissStep; if (--gHissLeft == 0) gHiss = gHissTarget; }
        const float wetAmt = satOn ? std::min (1.0f, sat * 20.0f) : 0.0f;   // fades the stage in over the first 5 %, so 0 % is exactly dry
        const float gM = smMakeup.next();

        // ---- delay line read (wow / flutter)
        float yp[2], yv[2]; float hs = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            line[c].push (in[c][i]);
            float d = mainBase + Aw * mw + Af * mf + As * scr;
            if (d < 2.0f || d > maxDelay) ++clampCount;
            minDelay = std::min (minDelay, d); maxDelaySeenV = std::max (maxDelaySeenV, d);
            d = std::min (maxDelay, std::max (2.0f, d));
            lastDelay[c] = d;
            yp[c] = (Aw == 0.0f && Af == 0.0f && As == 0.0f) ? line[c].read (base) : line[c].readCubic (d);
            hs = std::max (hs, std::fabs (yp[c]));
        }
        hEnv += (hs > hEnv ? hAttCoef : hRelCoef) * (hs - hEnv);   // hiss sidechain: time-aligned with the output (post-delay), pre-Drive

        // ---- drive (fixed calibrated makeup, linearly smoothed)
        for (int c = 0; c < nch; ++c)
        {
            yv[c] = yp[c];
            if (satOn) { const float o = stage[c].process (yp[c]); yv[c] += wetAmt * (o * gM - yp[c]); }
        }
        // ---- age (stereo-linked, signal-driven)
        age.process (yv);
        // ---- hiss, tone
        const float hiss = hissLvl * gHiss;
        for (int c = 0; c < nch; ++c)
        {
            float y = yv[c];
            if (hiss > 0.0f) y += hissTilt[c].process (pinkStep (pk[c], hissRng.bipolar()) * hissNorm) * hiss;
            if (toneOn)
            {
                tonePole[c] += toneCoef * (y - tonePole[c]);
                y = toneLp[c].process (tonePole[c]);
            }
            out[c][i] = y;
        }
    }
}
} // namespace jy
