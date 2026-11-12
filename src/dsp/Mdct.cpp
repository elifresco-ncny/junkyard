#include "Mdct.h"
#include <cmath>

namespace jy
{
Mdct::Mdct (int hop) { setSize (hop); }

void Mdct::setSize (int hop)
{
    N = hop; M = 2 * N;
    const double pi = 3.14159265358979323846, n0 = 0.5 + N / 2.0;
    scale = (float) std::sqrt (2.0 / N);
    win.resize ((size_t) M); preRe.resize ((size_t) M); preIm.resize ((size_t) M);
    postRe.resize ((size_t) N); postIm.resize ((size_t) N);
    re.assign ((size_t) M, 0.0f); im.assign ((size_t) M, 0.0f);
    for (int n = 0; n < M; ++n)
    {
        win[(size_t) n] = (float) std::sin (pi * (n + 0.5) / M);
        preRe[(size_t) n] = (float) std::cos (pi * n / M);
        preIm[(size_t) n] = (float) -std::sin (pi * n / M);
    }
    for (int k = 0; k < N; ++k)
    {
        postRe[(size_t) k] = (float) std::cos (pi * n0 * (k + 0.5) / N);
        postIm[(size_t) k] = (float) -std::sin (pi * n0 * (k + 0.5) / N);
    }
    twRe.resize ((size_t) M / 2); twIm.resize ((size_t) M / 2);
    for (int j = 0; j < M / 2; ++j) { twRe[(size_t) j] = (float) std::cos (2 * pi * j / M); twIm[(size_t) j] = (float) -std::sin (2 * pi * j / M); }
    rev.resize ((size_t) M);
    int bits = 0; while ((1 << bits) < M) ++bits;
    for (int i = 0; i < M; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
}

// In-place radix-2 on re/im. Forward uses e^{-i}, inverse e^{+i} (unnormalised).
void Mdct::fft (bool inverse)
{
    for (int i = 0; i < M; ++i) { const int j = rev[(size_t) i]; if (j > i) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); } }
    const float sgn = inverse ? -1.0f : 1.0f; // twIm already holds -sin
    for (int len = 2; len <= M; len <<= 1)
    {
        const int half = len / 2, step = M / len;
        for (int i = 0; i < M; i += len)
            for (int j = 0; j < half; ++j)
            {
                const float wr = twRe[(size_t) (j * step)], wi = sgn * twIm[(size_t) (j * step)];
                const size_t a = (size_t) (i + j), b = a + (size_t) half;
                const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
    }
}

void Mdct::forward (const float* x, float* X)
{
    for (int n = 0; n < M; ++n)
    {
        const float v = x[n] * win[(size_t) n];
        re[(size_t) n] = v * preRe[(size_t) n]; im[(size_t) n] = v * preIm[(size_t) n];
    }
    fft (false);
    for (int k = 0; k < N; ++k)
        X[k] = scale * (re[(size_t) k] * postRe[(size_t) k] - im[(size_t) k] * postIm[(size_t) k]);
}

void Mdct::inverse (const float* X, float* y, bool rect)
{
    for (int k = 0; k < N; ++k)
    {
        // multiply by conj(post) = e^{+i...}
        re[(size_t) k] = X[k] * postRe[(size_t) k]; im[(size_t) k] = -X[k] * postIm[(size_t) k];
    }
    for (int k = N; k < M; ++k) { re[(size_t) k] = 0.0f; im[(size_t) k] = 0.0f; }
    fft (true);
    for (int n = 0; n < M; ++n)
    {
        // Re( e^{+i pi n / 2N} * C ) ; pre = e^{-i...}, so conj(pre) = (preRe, -preIm)
        const float v = preRe[(size_t) n] * re[(size_t) n] + preIm[(size_t) n] * im[(size_t) n];
        y[n] = scale * (rect ? 1.0f : win[(size_t) n]) * v;
    }
}
} // namespace jy
