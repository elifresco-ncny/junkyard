#pragma once
#include <vector>

namespace jy
{
constexpr int kMaxBands = 48;

struct Band { int start = 0, end = 0; float bark = 0, hz = 0; int width() const { return end - start; } };

// Bark-spaced scale-factor bands (~21-24) for a given sample rate / hop size.
class BandLayout
{
public:
    void prepare (double sampleRate, int hop);
    int count() const { return nBands; }
    const Band& operator[] (int i) const { return bands[(size_t) i]; }
    int hop() const { return N; }
    double binHz() const { return hz; }
    // Spreading gain from masker band j to victim band i (linear energy)
    float spread (int victim, int masker) const { return spreadMat[(size_t) (victim * kMaxBands + masker)]; }
    int binForHz (double f) const;
    static float hzToBark (float f);
private:
    std::vector<Band> bands;
    std::vector<float> spreadMat;
    int nBands = 0, N = 512;
    double hz = 46.875;
};
} // namespace jy
