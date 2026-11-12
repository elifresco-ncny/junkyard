#pragma once
// Lock-free hand-over of the learned Drive calibration from the message thread to the audio thread (double buffer).
// Writer: message thread only. Reader: audio thread, once per control tick (a single atomic load unless something changed).
#include "DriveCal.h"
#include <atomic>

namespace jy::drivecal
{
class CalPublisher
{
public:
    // message thread
    void publish (const LearnedCal& c)
    {
        const int idx = 1 - active.load (std::memory_order_acquire);
        Slot& s = slots[idx];
        for (int i = 0; i < kNd; ++i) s.C[i].store (c.C[i], std::memory_order_relaxed);
        s.Lcal.store (c.Lcal, std::memory_order_relaxed); s.inputDb.store (c.inputDbAtLearn, std::memory_order_relaxed);
        s.learned.store (c.learned, std::memory_order_relaxed);
        active.store (idx, std::memory_order_release);
        version.fetch_add (1, std::memory_order_release);
    }
    // audio thread
    uint32_t versionNow() const { return version.load (std::memory_order_acquire); }
    void read (LearnedCal& out) const
    {
        const Slot& s = slots[active.load (std::memory_order_acquire)];
        for (int i = 0; i < kNd; ++i) out.C[i] = s.C[i].load (std::memory_order_relaxed);
        out.Lcal = s.Lcal.load (std::memory_order_relaxed); out.inputDbAtLearn = s.inputDb.load (std::memory_order_relaxed);
        out.learned = s.learned.load (std::memory_order_relaxed);
    }
private:
    struct Slot { std::atomic<float> C[kNd] {}; std::atomic<float> Lcal { 0.0f }, inputDb { 0.0f }; std::atomic<bool> learned { false }; };
    Slot slots[2];
    std::atomic<int> active { 0 };
    std::atomic<uint32_t> version { 0 };
};
} // namespace jy::drivecal
