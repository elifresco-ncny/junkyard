#pragma once
#include <vector>

namespace jy
{
// Orthonormal-scaled sine-window MDCT (hop N, window 2N) via a 2N-point complex FFT with pre/post twiddles.
// forward(): applies the analysis window. inverse(): applies the synthesis window, ready for overlap-add.
class Mdct
{
public:
    explicit Mdct (int hop = 512);
    void setSize (int hop);
    int size() const { return N; }
    void forward (const float* in2N, float* outN);
    void inverse (const float* inN, float* out2N, bool rectangular = false); // rectangular = glitch: skips the synthesis window

private:
    void fft (bool inverse);
    int N = 0, M = 0;
    float scale = 1.0f;
    std::vector<float> win, preRe, preIm, postRe, postIm, twRe, twIm, re, im;
    std::vector<int> rev;
};
} // namespace jy
