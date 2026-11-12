"""Shared metrics for comparing a processed signal against its original."""
import numpy as np, soundfile as sf
from scipy import signal

NFFT, HOP = 2048, 512

def load(p):
    x, sr = sf.read(p, always_2d=True)
    if x.shape[1] == 1: x = np.repeat(x, 2, 1)
    return x[:, :2], sr

def align(ref, test, maxlag=6000):
    """Return test shifted so it lines up with ref (cross-correlation on mid)."""
    n = min(len(ref), len(test), 240000)
    a = ref[:n].mean(1); b = test[:n].mean(1)
    c = signal.correlate(b, a, mode='full', method='fft')
    lags = np.arange(-n + 1, n)
    m = np.abs(lags) <= maxlag
    lag = lags[m][np.argmax(c[m])]
    t = test[lag:] if lag >= 0 else np.vstack([np.zeros((-lag, 2)), test])
    L = min(len(ref), len(t))
    return ref[:L], t[:L], int(lag)

def stft_pow(x):
    f, t, Z = signal.stft(x, 48000, window='hann', nperseg=NFFT, noverlap=NFFT - HOP, boundary=None)
    return f, t, np.abs(Z) ** 2

def bandwidth20(ref, test):
    """Highest frequency where the test/ref spectral ratio (time-averaged) stays above -20 dB."""
    f, _, Pr = stft_pow(ref.mean(1)); _, _, Pt = stft_pow(test.mean(1))
    pr = Pr.mean(1) + 1e-20; pt = Pt.mean(1) + 1e-20
    ratio = 10 * np.log10(pt / pr)
    # smooth over ~10 bins
    k = np.ones(9) / 9; ratio = np.convolve(ratio, k, mode='same')
    ok = (pr > pr.max() * 1e-7) & (f > 200)
    above = np.where((ratio > -20) & ok)[0]
    return float(f[above.max()]) if len(above) else 0.0

def zeroed_pct(ref, test, fmax=16000):
    """% of time-frequency cells (below fmax) where the original has energy but the test lost >30 dB of it."""
    f, _, Pr = stft_pow(ref.mean(1)); _, _, Pt = stft_pow(test.mean(1))
    sel = f < fmax
    Pr = Pr[sel]; Pt = Pt[sel]
    floor = Pr.max() * 1e-8
    live = Pr > floor
    lost = (Pt < Pr * 1e-3) & live
    return float(100.0 * lost.sum() / max(1, live.sum()))

def side_ratio_db(x):
    m = (x[:, 0] + x[:, 1]) / 2; s = (x[:, 0] - x[:, 1]) / 2
    return float(10 * np.log10((np.sum(s ** 2) + 1e-20) / (np.sum(m ** 2) + 1e-20)))

def preecho_db(ref, test):
    """Energy in the 20 ms before detected transients, test relative to ref (dB). Higher = more pre-echo."""
    r = ref.mean(1); t = test.mean(1)
    hop = 240
    env = np.array([np.sum(r[i:i + hop] ** 2) for i in range(0, len(r) - hop, hop)])
    ons = [i for i in range(3, len(env)) if env[i] > 30 * (env[i - 3:i].mean() + 1e-9) and env[i] > env.max() * 0.02]
    if not ons: return float('nan')
    num = den = 0.0
    for i in ons:
        a, b = (i - 4) * hop, i * hop
        num += np.sum(t[a:b] ** 2); den += np.sum(r[a:b] ** 2)
    return float(10 * np.log10((num + 1e-20) / (den + 1e-20)))

def all_metrics(ref, test):
    return dict(bw=bandwidth20(ref, test), zeroed=zeroed_pct(ref, test),
                side=side_ratio_db(test) - side_ratio_db(ref), preecho=preecho_db(ref, test))
