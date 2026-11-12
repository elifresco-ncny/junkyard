// junkyard_soak [hours=2] [wrecked] [learn] : renders hours of audio faster than real time through the real processor with random
// parameter jumps; reports NaN/peak problems, CPU per simulated 10 minutes, and memory growth.
//   wrecked : Age, Wow, Flutter and Drive pinned at 100 %, Hiss 50 %, Glitch 50 %, Tape on (modes and Order still cycle)
//   learn   : triggers Drive Learn twice (at 20 % and 60 % of the run) through the processor API and waits for it to finish
#include "PluginProcessor.h"
#include "dsp/CodecProfiles.h"
#include <chrono>
#include <iostream>
#include <random>
#include <cmath>
#include <string>
#include <thread>
#include <mach/mach.h>

static size_t rssBytes()
{
    mach_task_basic_info info; mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    return task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &n) == KERN_SUCCESS ? info.resident_size : 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double hours = argc > 1 ? std::atof (argv[1]) : 2.0;
    bool wrecked = false, learn = false;
    for (int i = 2; i < argc; ++i) { wrecked |= std::string (argv[i]) == "wrecked"; learn |= std::string (argv[i]) == "learn"; }
    const double sr = 48000; const int block = 128;
    JunkyardAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (sr, block); proc.prepareToPlay (sr, block);
    auto setR = [&] (const char* id, float real) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (real)); };
    std::mt19937 g (7); std::normal_distribution<float> nd (0.0f, 0.1f);
    auto rnd = [&] (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (g); };
    juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
    const long blocksPerSec = (long) (sr / block);
    const long totalSec = (long) (hours * 3600);
    const long learnAt[2] = { totalSec / 5, totalSec * 3 / 5 };
    int learnsDone = 0; long learnStartSec = -1;
    double t = 0; size_t rss0 = 0; int bad = 0; float peak = 0;
    auto wall0 = std::chrono::steady_clock::now(), slice0 = wall0;
    for (long s = 0; s < totalSec; ++s)
    {
        if (s % 2 == 0)
        {
            setR ("codec_mode", (float) (g() % 3)); setR ("bitrate", rnd (0, 1)); setR ("swirl", rnd (0, 100)); setR ("hf_loss", rnd (0, 60));
            setR ("glitch", wrecked ? 50.0f : rnd (0, 80)); setR ("wow", wrecked ? 100.0f : rnd (0, 100)); setR ("flutter", wrecked ? 100.0f : rnd (0, 100));
            setR ("saturation", wrecked ? 100.0f : rnd (0, 100)); setR ("hiss", wrecked ? 50.0f : rnd (0, 100)); setR ("tape_age", wrecked ? 100.0f : rnd (0, 100));
            setR ("width", rnd (0, 150)); setR ("order", (float) (g() & 1));
            setR ("codec_on", (g() % 8) != 0 ? 1.0f : 0.0f); setR ("tape_on", wrecked || (g() % 4) != 0 ? 1.0f : 0.0f);
            setR ("low_keep", (float) (g() & 1)); setR ("mix", rnd (30, 100)); setR ("input_gain", rnd (-12, 12));
            setR ("tape_tone", rnd (0, 1) < 0.5f ? 20000.0f : rnd (4000, 20000));
        }
        if (learn && learnsDone < 2 && s == learnAt[learnsDone] && learnStartSec < 0) { proc.startLearn(); learnStartSec = s; }
        for (long b = 0; b < blocksPerSec; ++b)
        {
            for (int i = 0; i < block; ++i, t += 1.0)
            {
                const float tone = (s / 20 % 3 == 2) ? 0.0f : 0.25f * std::sin ((float) (t * 0.0575)) + 0.1f * std::sin ((float) (t * 0.31));
                buf.setSample (0, i, tone + nd (g)); buf.setSample (1, i, 0.8f * tone + nd (g));
            }
            proc.processBlock (buf, midi);
            for (int i = 0; i < block; i += 16)
                for (int c = 0; c < 2; ++c) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) ++bad; peak = std::max (peak, std::fabs (v)); }
        }
        if (learnStartSec >= 0)
        {
            proc.learnTick();
            // the worker thread runs in real time: give it room whenever the analysis is running
            for (int k = 0; k < 400 && proc.getLearnUiState() == JunkyardAudioProcessor::LearnAnalysing; ++k) { std::this_thread::sleep_for (std::chrono::milliseconds (10)); proc.learnTick(); }
            if (proc.getLearnUiState() != JunkyardAudioProcessor::LearnListening && proc.getLearnUiState() != JunkyardAudioProcessor::LearnAnalysing)
            {
                std::cout << "learn #" << learnsDone + 1 << " finished at sim " << s / 60 << " min, learned=" << proc.isLearned() << std::endl;
                ++learnsDone; learnStartSec = -1;
            }
            else if (s - learnStartSec > 120) { std::cout << "learn #" << learnsDone + 1 << " gave up (no signal?)\n"; proc.cancelLearn(); proc.learnTick(); ++learnsDone; learnStartSec = -1; }
        }
        if (s == 120) rss0 = rssBytes();
        if ((s + 1) % 600 == 0)
        {
            const auto now = std::chrono::steady_clock::now();
            const double w = std::chrono::duration<double> (now - slice0).count(); slice0 = now;
            std::cout << "sim " << (s + 1) / 60 << " min: " << (100.0 * w / 600.0) << " % CPU, rss " << rssBytes() / 1048576 << " MB, bad " << bad << ", peak " << peak << std::endl;
        }
    }
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - wall0).count();
    const size_t rss1 = rssBytes();
    std::cout << "TOTAL " << hours << " h simulated in " << wall << " s; non-finite samples: " << bad << "; peak " << peak
              << "; rss growth after warmup " << (long) ((double) rss1 - (double) rss0) / 1024 << " KB; learns completed " << learnsDone << "\n";
    return bad == 0 && peak <= 8.0f ? 0 : 1;
}
