#pragma once
// Drive "Learn" capture (audio thread side). Records 5 s of the signal entering the tape section, skipping silent 100 ms chunks.
// The audio thread writes the buffer only in Waiting / Recording. Message thread: start() / cancel() / read the buffer once
// state() == Analyzing (the audio thread never touches it after that). The audio thread owns every capture variable; the message
// thread only flips the atomics.
#include <atomic>
#include <cmath>
#include <vector>

namespace jy
{
class LearnCapture
{
public:
    enum State { Idle = 0, Waiting = 1, Recording = 2, Analyzing = 3 };

    // message thread (prepareToPlay): (re)allocates and cancels any learn in progress
    void prepare (double sampleRate, int numChannels)
    {
        state_.store (Idle);
        sr = sampleRate; nch = numChannels;
        chunkLen = std::max (1, (int) std::lround (0.1 * sr));
        total = (size_t) chunkLen * 50;   // 5 s (50 chunks)
        for (int c = 0; c < 2; ++c) buf[c].assign (c < nch ? total : 0, 0.0f);
        startReq.store (false);
    }
    State state() const { return (State) state_.load (std::memory_order_acquire); }
    bool start()   // message thread; false if a learn is already running
    {
        // Request the reset BEFORE entering Waiting, so the audio thread can never write with a stale write position.
        startReq.store (true, std::memory_order_release);
        int exp = Idle;
        if (! state_.compare_exchange_strong (exp, Waiting, std::memory_order_acq_rel)) { startReq.store (false, std::memory_order_release); return false; }
        return true;
    }
    bool cancel()  // Waiting / Recording -> Idle (the previous curve is kept by the caller)
    {
        int e = Waiting;
        if (state_.compare_exchange_strong (e, Idle)) return true;
        e = Recording;
        return state_.compare_exchange_strong (e, Idle);
    }
    void finish() { state_.store (Idle, std::memory_order_release); }   // message thread, after analysis
    const std::vector<float>* channel (int c) const { return &buf[c]; }
    int numChannels() const { return nch; }
    size_t capturedFrames() const { return wp; }
    size_t totalFrames() const { return total; }
    double sampleRate() const { return sr; }

    // audio thread, called with the signal entering the tape section
    void process (const float* const* x, int n)
    {
        if (startReq.exchange (false, std::memory_order_acq_rel)) { wp = 0; chunkStart = 0; chunkCount = 0; chunkPeak = 0.0f; }
        int st = state_.load (std::memory_order_acquire);
        if (st != Waiting && st != Recording) return;
        for (int i = 0; i < n; ++i)
        {
            if (wp >= total) return;   // never write past the buffer, whatever the thread interleaving
            float pk = 0.0f;
            for (int c = 0; c < nch; ++c) { buf[c][wp] = x[c][i]; pk = std::fmax (pk, std::fabs (x[c][i])); }
            ++wp; chunkPeak = std::fmax (chunkPeak, pk);
            if (++chunkCount < chunkLen) continue;
            chunkCount = 0;
            if (chunkPeak < 0.001f) wp = chunkStart;          // below -60 dBFS: rewind, silence is never learned
            else { chunkStart = wp; if (st == Waiting) { st = Recording; int e = Waiting; state_.compare_exchange_strong (e, Recording); } }
            chunkPeak = 0.0f;
            if (wp >= total) { int e = state_.load(); if (e == Waiting || e == Recording) state_.compare_exchange_strong (e, Analyzing, std::memory_order_acq_rel); return; }
            if (state_.load (std::memory_order_relaxed) == Idle) return;   // cancelled
        }
    }

private:
    std::atomic<int> state_ { Idle };
    std::atomic<bool> startReq { false };
    std::vector<float> buf[2];
    double sr = 48000.0; int nch = 2, chunkLen = 4800; size_t total = 240000;
    size_t wp = 0, chunkStart = 0; int chunkCount = 0; float chunkPeak = 0.0f;
};
} // namespace jy
