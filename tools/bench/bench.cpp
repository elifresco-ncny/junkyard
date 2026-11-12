// junkyard_bench [seconds] [instances] [age0] : CPU of stereo instances at 48 kHz / 128 block, everything on:
// Drive, Wow, Flutter and Age at 100 %, Hiss 50 %, Tone 8 kHz, Low Keep, Glitch (per mode). `age0` turns Age off to show Age's share.
#include "dsp/JunkyardEngine.h"
#include "dsp/CodecProfiles.h"
#include <chrono>
#include <iostream>
#include <random>
#include <cmath>
#include <cstring>

static jy::EngineParams everything (int mode, bool age0)
{
    jy::EngineParams p; p.codecMode = mode; p.bitrate = 0.55f; p.swirl = 0.4f; p.glitch = 0.3f; p.tapeOn = true; p.age = age0 ? 0.0f : 1.0f;
    p.flutter = 1.0f; p.wow = 1.0f; p.saturation = 1.0f; p.hiss = 0.5f; p.toneHz = 8000.0f; p.lowKeep = true; p.width = 0.8f;
    return p;
}

int main (int argc, char** argv)
{
    bool age0 = false; double seconds = 20.0; int instances = 0;
    for (int i = 1, pos = 0; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "age0")) age0 = true;
        else if (pos == 0) { seconds = std::atof (argv[i]); ++pos; }
        else if (pos == 1) { instances = std::atoi (argv[i]); ++pos; }
    }
    const double sr = 48000; const int block = 128;
    if (instances > 0)
    {
        std::vector<jy::JunkyardEngine> es ((size_t) instances);
        const auto p = everything (1, age0);
        for (auto& e : es) { e.setParams (p); e.prepare (sr, block, 2); }
        std::mt19937 g (1); std::normal_distribution<float> d (0.0f, 0.1f); std::vector<float> l (block), r (block);
        const long blocks = (long) (seconds * sr / block); auto t0 = std::chrono::steady_clock::now();
        for (long b = 0; b < blocks; ++b) for (auto& e : es)
        {
            for (int i = 0; i < block; ++i) { l[(size_t) i] = d (g); r[(size_t) i] = d (g); }
            float* ptr[2] = { l.data(), r.data() }; e.process (ptr, block);
        }
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::cout << instances << " instances" << (age0 ? " (Age 0)" : "") << ": " << (100.0 * el / seconds) << " % of one core total (" << (100.0 * el / seconds / instances) << " % each)\n";
        return 0;
    }
    for (int mode = 0; mode < 3; ++mode)
    {
        jy::JunkyardEngine e;
        const auto p = everything (mode, age0);
        e.setParams (p); e.prepare (sr, block, 2);
        std::mt19937 g (1); std::normal_distribution<float> d (0.0f, 0.1f);
        std::vector<float> l (block), r (block);
        const long blocks = (long) (seconds * sr / block);
        auto t0 = std::chrono::steady_clock::now();
        double t = 0;
        for (long b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, t += 1.0)
            {
                const float tone = 0.2f * std::sin ((float) (t * 0.0575)) + 0.1f * std::sin ((float) (t * 0.31));
                l[(size_t) i] = tone + d (g); r[(size_t) i] = 0.8f * tone + d (g);
            }
            float* ptr[2] = { l.data(), r.data() };
            e.process (ptr, block);
        }
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::cout << "mode " << mode << (age0 ? " (Age 0)" : "") << ": " << (100.0 * el / seconds) << " % of one core (" << seconds << " s audio in " << el << " s)\n";
    }
}
