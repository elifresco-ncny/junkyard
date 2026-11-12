#include "JunkyardEngine.h"
#include <cmath>

namespace jy
{
void JunkyardEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate; maxBlock = std::max (kTick, maxBlockSize); nch = std::min (2, std::max (1, numChannels));
    codec.prepare (sr, nch, maxBlock);
    tape.proc.setCalPublisher (&calPub);
    tape.prepare (sr, nch, maxBlock);
    learnCap.prepare (sr, nch);
    latency = codec.lat + tape.lat;

    for (auto& d : dryLine) d.prepare (latency + 4);
    for (auto& d : lowLine) d.prepare (latency + 4);
    for (auto* bf : { &dry, &x, &low, &high, &a, &b, &c, &wetScratch })
        for (auto& ch : *bf) ch.assign ((size_t) maxBlock, 0.0f);
    for (auto* v : { &gIn, &gOut, &gMix, &gWidth, &gLow, &gWet, &gDuck }) v->assign ((size_t) maxBlock, 0.0f);

    for (auto* s : { &smIn, &smOut, &smMix, &smWidth, &smLow }) s->prepare (sr, 30.0);
    smLow.prepare (sr, 20.0);
    wetStep = (float) (1.0 / (0.020 * sr));
    duckStep = (float) (1.0 / (0.010 * sr));
    reset();
}

void JunkyardEngine::reset()
{
    codec.reset(); tape.reset();
    codec.wantOn = params.codecOn; tape.wantOn = params.tapeOn;
    for (auto& d : dryLine) d.reset();
    for (auto& d : lowLine) d.reset();
    xoFreq = params.lowKeepFreq;
    for (auto& f : xo) { f.setFrequency (sr, xoFreq); f.reset(); }
    smIn.setImmediate (std::pow (10.0f, params.inputGainDb / 20.0f));
    smOut.setImmediate (std::pow (10.0f, params.outputGainDb / 20.0f));
    smMix.setImmediate (params.mix);
    smWidth.setImmediate (params.width);
    smLow.setImmediate (params.lowKeep ? 1.0f : 0.0f);
    wetG = 1.0f; wetHold = 0; needPrewarm = false;
    orderCur = params.order; duckG = 1.0f;
    tickPos = 0;
    if (params.codecOn) { codec.running = true; codec.g = 1.0f; }
    if (params.tapeOn)  { tape.running = true;  tape.g = 1.0f; }
}

void JunkyardEngine::prewarm()
{
    codec.reprime(); tape.reprime();
    for (auto& f : xo) f.reset();
    for (auto& d : lowLine) d.reset();
    wetG = 0.0f; wetHold = latency; needPrewarm = false;
}

void JunkyardEngine::process (float* const* io, int n)
{
    // Fixed 64-sample ticks aligned to a global counter, so results never depend on the host block size
    // (smoothers and per-chunk control updates all run on the same grid).
    int done = 0;
    while (done < n)
    {
        const int k = std::min ({ n - done, maxBlock, kTick - tickPos });
        float* p[2] = { io[0] + done, nch > 1 ? io[1] + done : nullptr };
        processChunk (p, k);
        tickPos = (tickPos + k) % kTick;
        done += k;
    }
}

void JunkyardEngine::processBypassed (float* const* io, int n)
{
    int done = 0;
    while (done < n)
    {
        const int k = std::min (n - done, maxBlock);
        for (int ch = 0; ch < nch; ++ch)
            for (int i = 0; i < k; ++i)
                io[ch][done + i] = dryLine[(size_t) ch].process (io[ch][done + i], latency);
        done += k;
    }
    needPrewarm = true;
}

void JunkyardEngine::processChunk (float* const* io, int n)
{
    if (needPrewarm) prewarm();
    const auto& P = params;
    float *dryP[2], *xP[2], *lowP[2], *highP[2], *aP[2], *bP[2], *cP[2], *wP[2];
    ptrs (dry, dryP); ptrs (x, xP); ptrs (low, lowP); ptrs (high, highP);
    ptrs (a, aP); ptrs (b, bP); ptrs (c, cP); ptrs (wetScratch, wP);

    // parameter smoothers
    smIn.setTarget (std::pow (10.0f, P.inputGainDb / 20.0f));
    smOut.setTarget (std::pow (10.0f, P.outputGainDb / 20.0f));
    smMix.setTarget (P.mix);
    smWidth.setTarget (P.width);
    smLow.setTarget (P.lowKeep ? 1.0f : 0.0f);
    smIn.fill (gIn.data(), n); smOut.fill (gOut.data(), n); smMix.fill (gMix.data(), n);
    smWidth.fill (gWidth.data(), n); smLow.fill (gLow.data(), n);

    // sections
    codec.wantOn = P.codecOn; tape.wantOn = P.tapeOn;
    codec.proc.setParams (P); tape.proc.setParams (P);

    // wet-enable gain (prewarm)
    for (int i = 0; i < n; ++i)
    {
        if (wetHold > 0) --wetHold;
        else if (wetG < 1.0f) wetG = std::fmin (1.0f, wetG + wetStep);
        gWet[(size_t) i] = wetG;
    }
    // order-change duck
    {
        const bool changing = (P.order != orderCur);
        for (int i = 0; i < n; ++i)
        {
            if (changing || duckG < 1.0f)
            {
                if (P.order != orderCur)
                {
                    duckG = std::fmax (0.0f, duckG - duckStep);
                    if (duckG <= 0.0f) orderCur = P.order;
                }
                else duckG = std::fmin (1.0f, duckG + duckStep);
            }
            gDuck[(size_t) i] = duckG;
        }
    }

    // 1. dry (delayed by L) + input gain
    for (int ch = 0; ch < nch; ++ch)
        for (int i = 0; i < n; ++i)
        {
            const float in = io[ch][i];
            dryP[ch][i] = dryLine[(size_t) ch].process (in, latency);
            xP[ch][i] = in * gIn[(size_t) i];
        }

    // 2. Low Keep split
    if (std::fabs (P.lowKeepFreq - xoFreq) > 1e-4f * xoFreq)
    {
        xoFreq += (P.lowKeepFreq - xoFreq) * (1.0f - std::exp ((float) (-n / (0.03 * sr))));
        for (auto& f : xo) f.setFrequency (sr, xoFreq);
    }
    for (int ch = 0; ch < nch; ++ch)
        for (int i = 0; i < n; ++i)
        {
            float lo, hi;
            xo[(size_t) ch].process (xP[ch][i], lo, hi);
            const float k = gLow[(size_t) i];
            highP[ch][i] = xP[ch][i] + k * (hi - xP[ch][i]);     // damage input
            lowP[ch][i] = lowLine[(size_t) ch].process (lo, latency) * k; // low band, delayed by L
        }

    // 3. damage chain
    const float* cur[2] = { highP[0], highP[1] };
    float* bufs[2][2] = { { aP[0], aP[1] }, { bP[0], bP[1] } };
    int flip = 0;
    auto run = [&] (auto& sec)
    {
        float* out[2] = { bufs[flip][0], bufs[flip][1] };
        sec.process (cur, out, wP, n);
        cur[0] = out[0]; cur[1] = out[1];
        flip ^= 1;
    };
    // Learn listens to the signal entering the tape section (after Input, Low Keep and, in Codec > Tape, the codec), whether or not Tape is on.
    if (orderCur == DigitalThenTape) { run (codec); learnCap.process (cur, n); run (tape); }
    else                             { learnCap.process (cur, n); run (tape); run (codec); }

    // 4. width + low band + limiter + mix
    bool bad = false;
    for (int i = 0; i < n; ++i)
    {
        const float duck = gDuck[(size_t) i];
        float w0 = cur[0][i] * duck, w1 = nch > 1 ? cur[1][i] * duck : 0.0f;
        if (nch > 1)
        {
            const float wd = gWidth[(size_t) i];
            if (wd != 1.0f)
            {
                const float m = 0.5f * (w0 + w1), s = 0.5f * (w0 - w1) * wd;
                w0 = m + s; w1 = m - s;
            }
        }
        w0 += lowP[0][i]; if (nch > 1) w1 += lowP[1][i];
        bad |= limiter.process (w0);
        if (nch > 1) bad |= limiter.process (w1);

        const float mx = gMix[(size_t) i] * gWet[(size_t) i], og = gOut[(size_t) i];
        io[0][i] = (dryP[0][i] + mx * (w0 - dryP[0][i])) * og;
        if (nch > 1) io[1][i] = (dryP[1][i] + mx * (w1 - dryP[1][i])) * og;
    }
    if (bad) needPrewarm = true;
}
} // namespace jy
