#include "TapeAge.h"
#include <algorithm>

namespace jy
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int lenSamples (double fs, double ms) { return std::max (2, (int) std::lround (ms * 0.001 * fs)); }
}

void TapeAge::prepare (double fs, int numChannels)
{
    sr = fs; nch = std::min (2, std::max (1, numChannels));
    smAge.prepare (sr, 30.0);
    envFast.set (sr, 1.0, 30.0); envSlow.set (sr, 10.0, 300.0); envSlowB.set (sr, 10.0, 600.0); envHF.set (sr, 2.0, 80.0);
    for (int c = 0; c < 2; ++c)
    {
        hpDet[c].setHighpass (sr, 2000.0);
        hugN[c].design (sr, 30.0, 400.0); eroN[c].design (sr, 40.0, 400.0); jitN[c].design (sr, 100.0, 1500.0);
        bJitN[c].design (sr, 100.0, 1500.0); bEroN[c].design (sr, 40.0, 400.0);
        hp150[c].setHighpass (sr, 150.0); lp8k[c].setLowpass (sr, std::min (8000.0, sr * 0.45));
        ap1[c].len = lenSamples (sr, c == 0 ? 4.7 : 5.1); ap2[c].len = lenSamples (sr, c == 0 ? 7.3 : 7.9);
        ap1[c].line.prepare (ap1[c].len + 8); ap2[c].line.prepare (ap2[c].len + 8);
        static const double ms[4] = { 17.0, 23.0, 31.0, 43.0 };
        for (int i = 0; i < 4; ++i) { fdnLen[c][i] = lenSamples (sr, ms[i] * (c == 0 ? 1.0 : 1.07)); fdn[c][i].prepare (fdnLen[c][i] + 8); }
        jit[c].prepare ((int) std::ceil (0.0035 * sr) + 16);
        bJit[c].prepare ((int) std::ceil (0.0035 * sr) + 16);
        bHp[c].setHighpass (sr, 180.0); bLp[c].setLowpass (sr, std::min (2200.0, sr * 0.45));
        crackLp[c].setLowpass (sr, std::min (9000.0, sr * 0.45));
    }
    fdnLpCoef = 1.0f - (float) std::exp (-2.0 * kPi * std::min (5000.0, sr * 0.45) / sr);
    jitBase = (float) (0.002 * sr); bJitBase = (float) (0.002 * sr); bJitAmt = (float) (0.0002 * sr);
    {
        int size = 16; while ((double) size < 4.0 * sr + 64.0) size <<= 1;
        for (auto& h : hist) h.assign ((size_t) size, 0.0f);
        histMask = size - 1;
        int es = std::max (16, size / 64); envRing.assign ((size_t) es, 0.0f); envMask = es - 1;
        // size is a power of two >= 16*64, so size / 64 is a power of two too
    }
    hannTab.assign (1026, 0.0f);
    for (int i = 0; i < 1026; ++i) hannTab[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * kPi * (double) std::min (i, 1024) / 1024.0);
    reset();
}

void TapeAge::resetParts()
{
    for (int c = 0; c < 2; ++c)
    {
        shelf[c].reset(); shelf[c].setHighShelfSlope (sr, 6000.0, 0.0, 0.7); hpDet[c].reset();
        hp150[c].reset(); lp8k[c].reset(); crackLp[c].reset(); bHp[c].reset(); bLp[c].reset();
        ap1[c].line.reset(); ap2[c].line.reset(); jit[c].reset(); bJit[c].reset();
        for (int i = 0; i < 4; ++i) { fdn[c][i].reset(); fdnLp[c][i] = 0.0f; }
        for (auto& v : voices[c]) { v.on = false; v.bp.reset(); }
        for (auto& g : grains[c]) g.on = false;
        nextGrain[c] = 0;
    }
    shelfCut = 0.0f; shelfFc = 6000.0f; shelfInit = false;
    envHF.reset(); crackIdle = 1 << 20; haloWasOn = bleedWasOn = false;
    rDry.setNow (1.0f); rHalo.setNow (0.0f); rBleed.setNow (0.0f);
}

void TapeAge::reset()
{
    smAge.setImmediate (0.0f);
    envFast.reset(); envSlow.reset(); envSlowB.reset(); envHF.reset();
    silentCount = 0; stopFade = 1.0f; ctr = 0; sampleIndex = 0; wIdx = 0; eventCount = 0;
    for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
    std::fill (envRing.begin(), envRing.end(), 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        hugN[c].reset (0xA6E0000ull + 1 + (uint64_t) c); eroN[c].reset (0xA6E0000ull + 11 + (uint64_t) c); jitN[c].reset (0xA6E0000ull + 21 + (uint64_t) c);
        bJitN[c].reset (0xA6E0000ull + 31 + (uint64_t) c); bEroN[c].reset (0xA6E0000ull + 41 + (uint64_t) c);
        rngB[c].setSeed (0xB1EEDull + 7 * (uint64_t) c);
    }
    rngC.setSeed (0xC4ACCull); rngNoise.setSeed (0x5EED5ull); vnC.prepare (sr, 77);
    resetParts();
    wasActive = false; aNow = 0.0f;
}

void TapeAge::flushStates()
{
    for (int c = 0; c < nch; ++c)
    {
        for (Biquad* b : { &shelf[c], &hpDet[c], &hp150[c], &lp8k[c], &crackLp[c], &bHp[c], &bLp[c] }) flushBq (*b);
        for (int i = 0; i < 4; ++i) if (std::fabs (fdnLp[c][i]) < 1e-12f) fdnLp[c][i] = 0.0f;
    }
}

void TapeAge::triggerEvent (int c)
{
    Voice* v = nullptr;
    for (auto& x : voices[c]) if (! x.on) { v = &x; break; }
    if (v == nullptr) return;
    const float tau = (float) std::exp (rngC.range ((float) std::log (0.0002), (float) std::log (0.0015)));
    v->decay = (float) std::exp (-1.0 / ((double) tau * sr));
    v->attLeft = std::max (1, (int) std::lround (0.0001 * sr)); v->att = 1.0f / (float) v->attLeft; v->env = 0.0f;
    const double fc = std::min (std::exp ((double) rngC.range ((float) std::log (1500.0), (float) std::log (7000.0))), sr * 0.45);
    const double q = 0.7 + 0.7 * (double) rngC.uniform();
    v->bp.setBandpass (sr, fc, q); v->bp.reset();
    const float db = std::min (-10.0f, -22.0f + 7.0f * rngC.gauss());   // (tune) v0.4.1: +10 dB (was -32 / cap -20) so the dust is audible under the music
    // the grain amplitude is the RMS of the coloured burst: undo the band-pass's noise-power loss and the uniform noise's 0.577 RMS
    const double bpGain = std::sqrt (std::min (1.0, kPi * fc / (q * sr))) * 0.57735;
    v->amp = envSlow.v * (float) (std::pow (10.0, db / 20.0) / bpGain) * (rngC.chance (0.5f) ? -1.0f : 1.0f);
    v->on = true;
}

void TapeAge::startGrain (int c)
{
    Grain* g = nullptr;
    for (auto& x : grains[c]) if (! x.on) { g = &x; break; }
    auto& r = rngB[c];
    const float Gs = r.range (0.35f, 0.9f);
    float Ps = r.range (0.6f, 3.2f);
    const float delta = r.range (-0.003f, 0.003f);
    nextGrain[c] = std::max<int64_t> (1, (int64_t) (0.5f * Gs * (float) sr));
    if (g == nullptr) return;
    Ps = std::max (0.3f, std::min (Ps, 3.95f - 2.01f * Gs));   // the grain reads backwards from P behind the head while the head moves on
    g->len = Gs * (float) sr; g->t = 0.0f; g->step = 1.0f + delta;
    g->start = wIdx - (int64_t) (Ps * (float) sr);
    // source level: mean of the slow envelope over the grain's source span (normalises the history so the ghost tracks the current music)
    const int64_t a = std::max<int64_t> (0, (g->start - (int64_t) (g->step * g->len)) >> 6), b = std::max<int64_t> (a, g->start >> 6);
    double acc = 0.0; int64_t cnt = 0;
    for (int64_t i = a; i <= b; ++i) { acc += (double) envRing[(size_t) (i & envMask)]; ++cnt; }
    const float avg = (float) (acc / (double) std::max<int64_t> (1, cnt));
    g->gain = avg > 1e-5f ? 1.0f / avg : 0.0f;
    g->on = true;
}

void TapeAge::controlTick()
{
    const float a = aNow;
    const float rampIn = std::min (1.0f, a * 20.0f);
    // ---- halo level + loop gains
    const float haloDb = -40.0f + 26.0f * std::pow (a, 0.8f);
    const float t60 = 0.15f + 0.60f * a;
    const float gHalo = kHaloCal * std::pow (10.0f, haloDb / 20.0f) * std::sqrt (0.45f / t60) * rampIn;
    const float cDry = std::sqrt (std::max (0.0f, 1.0f - std::pow (10.0f, haloDb / 10.0f) * rampIn * rampIn));
    rDry.aim (cDry); rHalo.aim (gHalo);
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < 4; ++i) fdnG[c][i] = std::pow (10.0f, -3.0f * (float) fdnLen[c][i] / (t60 * (float) sr));
    jitAmt = 0.00012f * (float) sr * a;

    // ---- bleed level (fades out once the input has been silent (< -100 dBFS) for 0.3 s: exactly 0 after 0.8 s of silence)
    stopFade = clampf (1.0f - ((float) silentCount - 0.3f * (float) sr) / (0.5f * (float) sr), 0.0f, 1.0f);
    const float bleedDb = -56.0f + 24.0f * a;
    rBleed.aim (kBleedCal * std::pow (10.0f, bleedDb / 20.0f) * rampIn * stopFade);

    // ---- mistracking: dull on quiet passages, open on loud ones; never boosts
    {
        const float hfDb = 20.0f * std::log10 (std::max (envHF.v, 1e-9f));
        const float quiet = clampf ((-10.0f - hfDb) / 40.0f, 0.0f, 1.0f);
        const float cut = -4.0f * a * quiet, fc = 2000.0f + 4000.0f * (1.0f - quiet);
        if (! shelfInit || std::fabs (cut - shelfCut) > 0.05f || std::fabs (fc - shelfFc) > 0.01f * shelfFc)
        {
            shelfInit = true; shelfCut = cut; shelfFc = fc;
            for (int c = 0; c < nch; ++c) { const auto k = shelf[c]; shelf[c].setHighShelfSlope (sr, fc, cut, 0.7); shelf[c].z1 = k.z1; shelf[c].z2 = k.z2; }
        }
    }

    // ---- crackle events (transient-driven, clustered, only while there is music)
    {
        const float cl = vnC.next (0.7f, 16);
        const float eS = envSlow.v;
        if (eS > 1e-5f && a > 0.0f && silentCount < (int64_t) (0.2 * sr))   // only while the music is actually there
        {
            float onset = clampf ((envFast.v - eS) / std::max (eS, 1e-6f) - 0.2f, 0.0f, 1.0f);   // sustained material wobbles around 0.1; a drum hit goes well past 1
            onset *= onset;   // only real transients count
            const float level = clampf ((20.0f * std::log10 (eS) + 60.0f) / 50.0f, 0.0f, 1.0f);
            const float cluster = 0.5f + 0.5f * cl;
            const float lam = std::pow (a, 1.5f) * (6.0f * level * cluster + 2000.0f * onset);   // (tune) 380 -> 2000 on the squared onset: dust on hits, almost none on sustained material
            const float p = 1.0f - std::exp (-lam * 16.0f / (float) sr);
            if (rngC.uniform() < p)
            {
                ++eventCount;
                if (eventLog) eventLog->push_back ((uint32_t) sampleIndex);
                if (nch == 1) triggerEvent (0);
                else if (rngC.chance (0.6f)) { triggerEvent (0); triggerEvent (1); }
                else triggerEvent (rngC.chance (0.5f) ? 0 : 1);
            }
        }
    }
    flushStates();
}

void TapeAge::process (float* y)
{
    float s = std::fabs (y[0]); if (nch > 1) s = std::max (s, std::fabs (y[1]));
    envFast.process (s); const float eS = envSlow.process (s); const float eB = envSlowB.process (s);
    for (int c = 0; c < nch; ++c) hist[c][(size_t) (wIdx & histMask)] = y[c];   // always written, so raising Age never plays stale audio
    if ((wIdx & 63) == 0) envRing[(size_t) ((wIdx >> 6) & envMask)] = eS;
    ++wIdx; ++sampleIndex;
    if (s < 1e-5f) { if (silentCount < (int64_t) 1 << 40) ++silentCount; } else silentCount = 0;

    const float a = smAge.next();
    if (a <= 0.0f)
    {
        if (wasActive) { resetParts(); wasActive = false; }
        return;
    }
    wasActive = true; aNow = a;
    if (ctr == 0) controlTick();
    ctr = (ctr + 1) & 15;

    // ---- envelopes needing the detector (only while Age is on)
    {
        float hf = 0.0f;
        for (int c = 0; c < nch; ++c) hf = std::max (hf, std::fabs (hpDet[c].process (y[c])));
        envHF.process (hf);
    }

    const bool useHalo = solo == 0 || (solo & Halo), useCrackle = solo == 0 || (solo & Crackle), useBleed = solo == 0 || (solo & Bleed);
    const float cDry = rDry.next(), gH = rHalo.next(), gBase = rBleed.next();
    const bool haloOn = eS >= 1e-7f;
    if (! haloOn && haloWasOn)
    {
        for (int c = 0; c < 2; ++c)
        {
            hp150[c].reset(); lp8k[c].reset(); ap1[c].line.reset(); ap2[c].line.reset(); jit[c].reset();
            for (int i = 0; i < 4; ++i) { fdn[c][i].reset(); fdnLp[c][i] = 0.0f; }
        }
    }
    haloWasOn = haloOn;
    const bool bleedOn = eB >= 1e-7f && stopFade > 0.0f;
    if (! bleedOn && bleedWasOn)
    {
        for (int c = 0; c < 2; ++c) { bHp[c].reset(); bLp[c].reset(); bJit[c].reset(); for (auto& g : grains[c]) g.on = false; nextGrain[c] = 0; }
    }
    bleedWasOn = bleedOn;

    bool anyVoice = false;
    for (int c = 0; c < nch; ++c)
    {
        // ---- 1. mistracking (dynamic high shelf)
        const float m = shelf[c].process (y[c]);

        // ---- 2. halo
        float halo = 0.0f;
        if (haloOn && (useHalo))
        {
            const float hug = m * hugN[c].next();
            // cloud: pre-filter -> two Schroeder all-passes -> 4-line FDN (Householder, 5 kHz loop lowpass) -> jitter read -> erode -> soften
            float u = hp150[c].process (m);
            for (AllpassD* ap : { &ap1[c], &ap2[c] })
            {
                const float d = ap->line.read (ap->len - 1), w = u + 0.6f * d;
                ap->line.push (w); u = d - 0.6f * w;
            }
            float d[4], fb[4], sum = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                d[i] = fdn[c][i].read (fdnLen[c][i] - 1);
                fdnLp[c][i] += fdnLpCoef * (d[i] - fdnLp[c][i]);
                fb[i] = fdnG[c][i] * fdnLp[c][i]; sum += fb[i];
            }
            const float half = 0.5f * sum, inj = 0.5f * u;
            for (int i = 0; i < 4; ++i) fdn[c][i].push (inj + fb[i] - half);
            const float cloud = d[0] - d[1] + d[2] - d[3];
            jit[c].push (cloud);
            const float j = clampf (jitN[c].next(), -3.0f, 3.0f);
            const float dis = jit[c].readCubic (jitBase + jitAmt * j);
            const float ero = 1.0f + 0.8f * clampf (eroN[c].next(), -1.25f, 1.25f);
            const float v = lp8k[c].process (dis * ero);
            halo = gH * (0.4f * hug + v);
        }

        // ---- 3. crackle bus
        float crack = 0.0f;
        if (useCrackle)
        {
            float sum = 0.0f;
            for (auto& v : voices[c])
            {
                if (! v.on) continue;
                anyVoice = true;
                if (v.attLeft > 0) { v.env += v.att; if (--v.attLeft == 0) v.env = 1.0f; } else v.env *= v.decay;
                sum += v.bp.process (rngNoise.bipolar() * v.env * v.amp);
                if (v.attLeft == 0 && v.env < 0.003f) { v.on = false; v.bp.reset(); }
            }
            if (crackIdle < (int) (0.012 * sr)) crack = crackLp[c].process (sum);
        }

        // ---- 4. side-B bleed: reversed history grains -> shroud
        float bleed = 0.0f;
        if (bleedOn && useBleed)
        {
            if (nextGrain[c] <= 0) startGrain (c);
            --nextGrain[c];
            float sum = 0.0f, w2 = 0.0f;
            for (auto& g : grains[c])
            {
                if (! g.on) continue;
                const float q = g.step * g.t; const int64_t qi = (int64_t) q; const float qf = q - (float) qi;
                const float x0 = hist[c][(size_t) ((g.start - qi - 1) & histMask)], x1 = hist[c][(size_t) ((g.start - qi) & histMask)];
                const float x = x0 + (x1 - x0) * (1.0f - qf);
                const float ph = g.t / g.len * 1024.0f; const int pi = (int) ph;
                const float w = hannTab[(size_t) pi] + (hannTab[(size_t) pi + 1] - hannTab[(size_t) pi]) * (ph - (float) pi);
                sum += w * g.gain * x; w2 += w * w;
                g.t += 1.0f; if (g.t >= g.len) g.on = false;
            }
            const float ghost = sum / std::sqrt (w2 + 1e-3f);
            const float b = bLp[c].process (bHp[c].process (ghost));
            bJit[c].push (b);
            const float dis = bJit[c].readCubic (bJitBase + bJitAmt * clampf (bJitN[c].next(), -3.0f, 3.0f));
            const float ero = 1.0f + 0.8f * clampf (bEroN[c].next(), -1.25f, 1.25f);
            bleed = dis * ero * gBase * eB;
        }

        float out = 0.0f;
        if (solo == 0) out = m * cDry + halo + crack + bleed;
        else
        {
            if (solo & Mistrack) out += m;
            out += halo + crack + bleed;
        }
        y[c] = out;
    }
    if (anyVoice) crackIdle = 0; else if (crackIdle < (1 << 20)) ++crackIdle;
}
} // namespace jy
