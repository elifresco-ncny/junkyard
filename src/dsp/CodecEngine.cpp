#include "CodecEngine.h"
#include <cmath>
#include <algorithm>

namespace jy
{
void CodecEngine::prepare (double sampleRate, int numChannels, int)
{
    sr = sampleRate; nch = numChannels; hop = codecHop (sr);
    histFrames = std::max (8, (int) std::lround (sr / hop));
    mdct.setSize (hop);
    bands.prepare (sr, hop);
    psy.prepare (bands);
    for (auto& a : alloc) a.prepare (bands);
    for (auto* v : { &inbuf, &ola, &tmp }) for (auto& b : *v) b.assign ((size_t) 2 * hop, 0.0f);
    for (auto* v : { &outbuf, &X, &Xorig, &Y, &Xq, &lastGood }) for (auto& b : *v) b.assign ((size_t) hop, 0.0f);
    for (auto& b : hist) b.assign ((size_t) histFrames * hop, 0.0f);
    // log-spaced scope bins from ~40 Hz to Nyquist
    {
        const double b0 = std::max (1.0, 40.0 / (sr * 0.5 / hop));
        int prev = 0;
        for (int i = 0; i <= kScopeBins; ++i)
        {
            int e = (int) std::lround (b0 * std::pow ((double) hop / b0, (double) i / kScopeBins));
            e = std::min (hop, std::max (e, prev + (i < kScopeBins ? 1 : 0)));
            scopeLo[i] = e; prev = e;
        }
        scopeLo[kScopeBins] = hop;
    }
    reset();
}

void CodecEngine::reset()
{
    for (auto* v : { &inbuf, &outbuf, &ola, &lastGood, &hist }) for (auto& b : *v) std::fill (b.begin(), b.end(), 0.0f);
    pos = 0; reservoir = 0.0f; rng.setSeed (0x4A554E4B59415244ull);
    histPos = histCount = 0; bad = false; badRun = bufferFrames = bufferCooldown = holdFrames = stereoEvt = stereoEvtLeft = 0;
    rectNext = haveLastGood = false; sbrCtr = 0; brkTrimSm = 0.0f; brkTrimInit = false;
    for (auto& e : sbrEnv) for (auto& v : e) v = 0.0f;
}

void CodecEngine::process (const float* const* in, float* const* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < nch; ++c)
        {
            out[c][i] = outbuf[(size_t) c][(size_t) pos];
            inbuf[(size_t) c][(size_t) (hop + pos)] = in[c][i];
        }
        if (++pos == hop) { processFrame(); pos = 0; }
    }
}

// Stream mode "SBR-lite": copy [kc/2,kc) tiled up into [kc,kTop) and shape with a coarse 5-band envelope
// (taken from the original spectrum, refreshed every 2 frames, quantised to 3 dB).
void CodecEngine::sbrPatch (int c, int kc, int kTop, int)
{
    const int N = hop, srcLen = std::max (1, kc - kc / 2), W = kTop - kc;
    if (W < 5 || kc < 8) return;
    auto& q = Xq[(size_t) c]; const auto& o = Xorig[(size_t) c];
    if ((sbrCtr & 1) == 0)
        for (int i = 0; i < 5; ++i)
        {
            const int a = kc + W * i / 5, b = kc + W * (i + 1) / 5;
            float e = 0; for (int k = a; k < b; ++k) e += o[(size_t) k] * o[(size_t) k];
            e /= (float) std::max (1, b - a);
            const float db = 10.0f * std::log10 (e + 1e-14f);
            sbrEnv[c][i] = std::pow (10.0f, std::round (db / 3.0f) * 0.3f);
        }
    std::vector<float>& t = tmp[(size_t) c]; // scratch (inverse() overwrites it later)
    for (int k = kc; k < kTop; ++k) t[(size_t) k] = q[(size_t) (kc / 2 + (k - kc) % srcLen)];
    for (int i = 0; i < 5; ++i)
    {
        const int a = kc + W * i / 5, b = kc + W * (i + 1) / 5;
        float ep = 0; for (int k = a; k < b; ++k) ep += t[(size_t) k] * t[(size_t) k];
        ep /= (float) std::max (1, b - a);
        const float g = ep > 1e-14f ? std::min (8.0f, std::sqrt (sbrEnv[c][i] / ep)) : 0.0f;
        for (int k = a; k < b && k < N; ++k) q[(size_t) k] = t[(size_t) k] * g;
    }
}

void CodecEngine::pushScope (bool glitched)
{
    ScopeFrame f;
    auto toU8 = [] (float e) { const float db = 10.0f * std::log10 (e + 1e-12f); return (uint8_t) std::min (255.0f, std::max (0.0f, (db + 100.0f) * 2.55f)); };
    for (int i = 0; i < kScopeBins; ++i)
    {
        const int a = std::min (scopeLo[i], hop - 1), b = std::max (a + 1, std::min (scopeLo[i + 1], hop));
        float eo = 0, ek = 0;
        for (int c = 0; c < nch; ++c)
            for (int k = a; k < b; ++k) { eo += Xorig[(size_t) c][(size_t) k] * Xorig[(size_t) c][(size_t) k]; ek += Xq[(size_t) c][(size_t) k] * Xq[(size_t) c][(size_t) k]; }
        const float inv = 1.0f / (float) ((b - a) * nch);
        f.orig[i] = toU8 (eo * inv); f.kept[i] = toU8 (ek * inv);
    }
    f.glitch = glitched ? 1 : 0;
    scope->push (f);
}

void CodecEngine::applyGlitch (float g, int mode, int kmax)
{
    const int N = hop;
    // Gilbert-Elliott two-state loss model, evaluated per frame
    const float pGB = 0.25f * g * g, pBG = 0.6f - 0.4f * g;
    const bool wasBad = bad;
    if (! bad) { if (g > 0.0f && rng.chance (pGB)) bad = true; }
    else if (rng.chance (pBG)) bad = false;
    badRun = bad ? (wasBad ? badRun + 1 : 0) : 0;
    if (bad) diag.badFrames += 1;

    // "buffering" gaps (Stream): 150-600 ms of silence
    if (bufferCooldown > 0) --bufferCooldown;
    if (mode == ModeStream && bad && ! wasBad && bufferFrames == 0 && bufferCooldown == 0 && rng.chance (0.01f + 0.08f * g * g))
        bufferFrames = std::max (1, (int) std::lround (rng.range (0.15f, 0.6f) * (float) sr / (float) N));
    if (bufferFrames > 0)
    {
        if (--bufferFrames == 0) bufferCooldown = (int) (2.0 * sr / N); // at least ~2 s of normal playback between gaps
        diag.bufferingFrames += 1;
        for (int c = 0; c < nch; ++c) std::fill (Xq[(size_t) c].begin(), Xq[(size_t) c].end(), 0.0f);
        return;
    }
    if (! bad) return;

    if (mode == ModeMP3 && badRun == 0) glitchMute = rng.chance (0.4f); // per burst: short mute or frame repeat (stutter)
    for (int c = 0; c < nch; ++c)
    {
        auto& q = Xq[(size_t) c]; auto& lg = lastGood[(size_t) c];
        if (mode == ModeMP3)
        {
            if (glitchMute) std::fill (q.begin(), q.end(), 0.0f);
            else q = lg;
        }
        else if (mode == ModeStream)
        {
            const float gain = std::pow (0.6f, (float) (badRun + 1));
            for (int k = 0; k < N; ++k) q[(size_t) k] = lg[(size_t) k] * gain * (rng.chance (0.5f) ? -1.0f : 1.0f);
        }
    }
    if (mode == ModeBroken)
    {
        const int ev = rng.index (5);
        for (int c = 0; c < nch; ++c)
        {
            auto& q = Xq[(size_t) c];
            switch (ev)
            {
                case 0: { const int f = histCount > 0 ? rng.index (std::min (histCount, histFrames)) : 0;
                          const int slot = ((histPos - 1 - f) % histFrames + histFrames) % histFrames;
                          std::copy (hist[(size_t) c].begin() + (long) slot * N, hist[(size_t) c].begin() + (long) (slot + 1) * N, q.begin()); break; }
                case 1: { const bool lo = rng.chance (0.5f); for (int k = 0; k < N; ++k) if ((k < N / 2) == lo) q[(size_t) k] = 0.0f; break; }
                case 2: std::reverse (q.begin(), q.begin() + kmax); break;
                case 3: { const float gn = std::pow (10.0f, rng.range (6.0f, 12.0f) / 20.0f); for (auto& v : q) v *= gn; break; } // burst gain capped at +12 dB
                default: holdFrames = std::max (holdFrames, 2 + rng.index (7)); break;
            }
        }
    }
}

void CodecEngine::processFrame()
{
    const int N = hop;
    const auto& T = tuning();
    const EngineParams P = params; // per-frame snapshot
    const int mode = P.codecMode;
    const float bitrateEff = std::min (1.0f, std::max (0.0f, P.bitrate));
    const float swirlEff = std::min (1.0f, std::max (0.0f, P.swirl));
    const float glitchEff = std::min (1.0f, std::max (0.0f, P.glitch));
    const float kbps = bitrateToKbps (mode, bitrateEff);
    diag.frames += 1;

    for (int c = 0; c < nch; ++c)
    {
        mdct.forward (inbuf[(size_t) c].data(), X[(size_t) c].data());
        std::copy (inbuf[(size_t) c].begin() + N, inbuf[(size_t) c].end(), inbuf[(size_t) c].begin());
        std::copy (X[(size_t) c].begin(), X[(size_t) c].end(), Xorig[(size_t) c].begin());
    }

    bool rect = false;
    if (! passThrough)
    {
        // ---- bandwidth: cutoff (+ SBR core for Stream < 64 kbps) + HF loss
        const bool sbr = (mode == ModeStream && kbps < 64.0f);
        float fc = sbr ? 5500.0f + (kbps - 6.0f) / 58.0f * 2500.0f : cutoffHz (kbps) * (mode == ModeStream ? 1.02f : 1.0f);
        fc = fc * std::exp2 (-P.hfLoss * std::log2 (std::max (1.0f, fc / 2500.0f)));
        fc = std::min (fc, (float) (sr * 0.5));
        const double binHz = sr * 0.5 / N;
        const float kc = (float) std::min<double> (N, fc / binHz);
        const int kmax = std::min (N, (int) std::ceil (kc));
        const float taper = mode == ModeStream ? 6.0f : 4.0f;
        for (int c = 0; c < nch; ++c)
        {
            auto& x = X[(size_t) c];
            for (int k = 0; k < N; ++k)
            {
                if ((float) k >= kc) x[(size_t) k] = 0.0f;
                else if ((float) k > kc - taper) x[(size_t) k] *= (kc - (float) k) / taper;
            }
        }

        const float baseBits = kbps * 1000.0f * (float) N / (float) sr;
        const float avail = std::max (0.0f, baseBits + T.reservoirUse * reservoir - T.headerBits);
        float used = 0.0f;
        AllocParams ap; ap.kmax = kmax; ap.swirl = swirlEff; ap.rng = &rng;
        bool ms = false;
        const bool psMode = (mode == ModeStream && kbps < 32.0f);

        if (nch == 1)
        {
            psy.analyse (X[0].data(), kmax, psyRes[0]);
            ap.budgetBits = avail;
            used = alloc[0].run (X[0].data(), Xq[0].data(), psyRes[0], ap).usedBits;
            diag.codedBins += kmax; for (int k = 0; k < kmax; ++k) diag.nonzeroBins += alloc[0].nonzero()[(size_t) k];
        }
        else
        {
            auto& L = X[0]; auto& R = X[1];
            double sLR = 0, sLL = 0, sRR = 0;
            for (int k = 0; k < kmax; ++k) { sLR += L[(size_t) k] * R[(size_t) k]; sLL += L[(size_t) k] * L[(size_t) k]; sRR += R[(size_t) k] * R[(size_t) k]; }
            const double corr = sLR / std::sqrt (sLL * sRR + 1e-20);
            ms = (kbps <= T.msBitrateKbps) || corr > T.msCorr;
            const float r2 = 0.70710678f;
            auto& A = Y[0]; auto& B = Y[1];
            std::fill (std::begin (isBand), std::end (isBand), false);
            if (ms)
            {
                for (int k = 0; k < N; ++k) { A[(size_t) k] = (L[(size_t) k] + R[(size_t) k]) * r2; B[(size_t) k] = (L[(size_t) k] - R[(size_t) k]) * r2; }
                const float fis = psMode ? 0.0f : intensityStartHz (kbps);
                if (fis < (float) (sr * 0.5))
                {
                    const int kis = bands.binForHz (fis);
                    for (int b = 0; b < bands.count(); ++b)
                    {
                        const int s = bands[b].start, e = std::min (bands[b].end, kmax);
                        if (s < kis || s >= e) continue;
                        float eL = 0, eR = 0, eM = 0;
                        for (int k = s; k < e; ++k) { eL += L[(size_t) k] * L[(size_t) k]; eR += R[(size_t) k] * R[(size_t) k]; eM += A[(size_t) k] * A[(size_t) k]; }
                        const float g = std::min (3.0f, std::sqrt ((eL + eR) / (eM + 1e-20f)));
                        for (int k = s; k < e; ++k) { A[(size_t) k] *= g; B[(size_t) k] = 0.0f; }
                        isBand[b] = true;
                        isRatio[b] = std::round (eL / (eL + eR + 1e-20f) * 6.0f) / 6.0f;
                    }
                }
            }
            else
            {
                std::copy (L.begin(), L.end(), A.begin()); std::copy (R.begin(), R.end(), B.begin());
            }
            psy.analyse (A.data(), kmax, psyRes[0]);
            psy.analyse (B.data(), kmax, psyRes[1]);
            const float peA = psyRes[0].pe + 1.0f, peB = psyRes[1].pe + 1.0f;
            const float shareB = std::max (0.10f, std::min (0.9f, peB / (peA + peB)));
            ap.budgetBits = avail * (1.0f - shareB);
            auto ra = alloc[0].run (A.data(), Xq[0].data(), psyRes[0], ap);
            ap.budgetBits = avail * shareB;
            auto rb = alloc[1].run (B.data(), Xq[1].data(), psyRes[1], ap);
            used = ra.usedBits + rb.usedBits;
            diag.codedBins += 2 * kmax;
            for (int k = 0; k < kmax; ++k) diag.nonzeroBins += alloc[0].nonzero()[(size_t) k] + alloc[1].nonzero()[(size_t) k];
            diag.msFrames += ms ? 1 : 0;
        }

        // ---- mode-specific post-quantisation, in the coded (M/S or L/R) domain
        const int nCoded = nch;
        for (int c = 0; c < nCoded; ++c)
        {
            auto& q = Xq[(size_t) c];
            if (mode == ModeStream)
            {
                // noise filling: zeroed bands get noise at the band's coarse energy - 6 dB
                for (int b = 0; b < bands.count(); ++b)
                {
                    if (! alloc[(size_t) c].bandZero()[(size_t) b] || bands[b].start >= kmax) continue;
                    if (c == 1 && isBand[b]) continue;
                    const float amp = std::sqrt (0.25f * psyRes[c].energy[b]);
                    if (amp <= 0.0f) continue;
                    for (int k = bands[b].start; k < std::min (bands[b].end, kmax); ++k) q[(size_t) k] = amp * rng.bipolar() * 1.732f;
                }
            }
            else if (mode == ModeBroken)
            {
                const float pCorrupt = 0.05f + 0.25f * (1.0f - bitrateEff);
                for (int b = 0; b < bands.count(); ++b)
                {
                    if (rng.chance (pCorrupt))
                    {
                        const float db = rng.range (6.0f, 20.0f) * (rng.chance (0.5f) ? 1.0f : -1.0f);
                        const float gn = std::pow (10.0f, db / 20.0f);
                        for (int k = bands[b].start; k < std::min (bands[b].end, N); ++k) q[(size_t) k] *= gn;
                    }
                }
                if (rng.chance (0.10f)) // "bit error": shuffle bins inside a band
                {
                    const int b = rng.index (bands.count());
                    for (int k = bands[b].start; k + 1 < bands[b].end; ++k) if (rng.chance (0.5f)) std::swap (q[(size_t) k], q[(size_t) (k + 1)]);
                }
                if (rng.chance (0.08f * (1.0f - bitrateEff) + 0.02f)) // bit-depth style spectral quantisation
                {
                    float pk = 1e-9f; for (int k = 0; k < kmax; ++k) pk = std::max (pk, std::fabs (q[(size_t) k]));
                    const float st = pk / (float) (1 << (2 + rng.index (3)));
                    for (int k = 0; k < kmax; ++k) q[(size_t) k] = std::round (q[(size_t) k] / st) * st;
                }
            }
        }

        // ---- decode stereo
        if (nch == 2 && ms)
        {
            const float r2 = 0.70710678f;
            auto& qa = Xq[0]; auto& qb = Xq[1];
            for (int k = 0; k < N; ++k)
            {
                const float a = qa[(size_t) k], b = qb[(size_t) k];
                qa[(size_t) k] = (a + b) * r2; qb[(size_t) k] = (a - b) * r2;
            }
            for (int b = 0; b < bands.count(); ++b)
                if (isBand[b])
                {
                    const float sl = std::sqrt (isRatio[b]), sr_ = std::sqrt (1.0f - isRatio[b]);
                    for (int k = bands[b].start; k < std::min (bands[b].end, kmax); ++k)
                    {
                        const float D = qa[(size_t) k] / r2;
                        qa[(size_t) k] = D * sl; qb[(size_t) k] = D * sr_;
                    }
                }
            if (psMode) // slight decorrelation of the right channel (parametric-stereo-lite)
                for (int k = 1; k < kmax; ++k) qb[(size_t) k] = 0.92f * qb[(size_t) k] + ((k & 1) ? 0.39f : -0.39f) * qb[(size_t) (k - 1)] * 0.5f;
        }

        // ---- SBR-lite
        if (sbr)
        {
            const int kTop = std::min (N, std::min (2 * kmax, bands.binForHz (16000.0)));
            for (int c = 0; c < nch; ++c) sbrPatch (c, kmax, kTop, mode);
            ++sbrCtr;
        }

        reservoir = std::min (T.reservoirMax, std::max (0.0f, reservoir + baseBits - used - T.headerBits));
        diag.bitsUsed += used;

        // history + last good frame (pre-glitch decode)
        const bool wasBadOrBuf = bad || bufferFrames > 0;
        for (int c = 0; c < nch; ++c)
        {
            std::copy (Xq[(size_t) c].begin(), Xq[(size_t) c].end(), hist[(size_t) c].begin() + (long) histPos * N);
            if (! wasBadOrBuf) std::copy (Xq[(size_t) c].begin(), Xq[(size_t) c].end(), lastGood[(size_t) c].begin());
        }
        histPos = (histPos + 1) % histFrames; histCount = std::min (histFrames, histCount + 1);
        haveLastGood = true;

        // ---- glitch engine (all modes) + Broken-mode frame holds / stereo events
        applyGlitch (glitchEff, mode, kmax);
        if (mode == ModeBroken)
        {
            if (holdFrames > 0)
            {
                --holdFrames;
                for (int c = 0; c < nch; ++c) std::copy (lastGood[(size_t) c].begin(), lastGood[(size_t) c].end(), Xq[(size_t) c].begin());
            }
            else if (rng.chance (0.015f + 0.03f * (1.0f - bitrateEff))) holdFrames = 2 + rng.index (7);
            if (nch == 2)
            {
                if (stereoEvtLeft > 0) --stereoEvtLeft;
                else if (rng.chance (0.01f + 0.05f * (1.0f - bitrateEff))) { stereoEvt = 1 + rng.index (2); stereoEvtLeft = 1 + rng.index (6); }
                else stereoEvt = 0;
                if (stereoEvtLeft > 0 || stereoEvt)
                {
                    if (stereoEvt == 1) std::swap (Xq[0], Xq[1]);
                    else if (stereoEvt == 2)
                        for (int k = 0; k < N; ++k) { const float m = 0.5f * (Xq[0][(size_t) k] + Xq[1][(size_t) k]); Xq[0][(size_t) k] = Xq[1][(size_t) k] = m; }
                }
            }
            {   // Broken only ever adds energy (corrupted bands jump up more than down). The level is brought back by a FIXED trim that
                // depends only on (Bitrate, Glitch): a table calibrated by junkyard_calibrate. No follower, no level memory.
                const float target = disableBrokenTrim ? 0.0f : brokenTrimDb (bitrateEff, glitchEff);
                if (! brkTrimInit) { brkTrimSm = target; brkTrimInit = true; }
                else brkTrimSm += 0.2212f * (target - brkTrimSm);   // one-pole over ~4 frames, only so parameter changes don't step
                if (brkTrimSm != 0.0f)
                {
                    const float g = std::pow (10.0f, brkTrimSm / 20.0f);
                    for (int c = 0; c < nch; ++c) for (int k = 0; k < N; ++k) Xq[(size_t) c][(size_t) k] *= g;
                }
            }
            rect = rng.chance (0.003f + 0.03f * (1.0f - bitrateEff) * (1.0f - bitrateEff) + 0.02f * glitchEff * glitchEff);
        }
    }
    else
    {
        for (int c = 0; c < nch; ++c) std::copy (X[(size_t) c].begin(), X[(size_t) c].end(), Xq[(size_t) c].begin());
    }

    if (scope != nullptr && scope->active.load (std::memory_order_relaxed) && ! passThrough)
        pushScope (bad || bufferFrames > 0 || holdFrames > 0 || rect);

    for (int c = 0; c < nch; ++c)
    {
        auto& y = tmp[(size_t) c];
        mdct.inverse (Xq[(size_t) c].data(), y.data(), rect);
        auto& o = outbuf[(size_t) c]; auto& ol = ola[(size_t) c];
        for (int i = 0; i < N; ++i) { o[(size_t) i] = ol[(size_t) i] + y[(size_t) i]; ol[(size_t) i] = y[(size_t) (N + i)]; }
    }
}
} // namespace jy
