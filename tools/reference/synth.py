#!/usr/bin/env python3
"""Synthetic test signals for when testaudio/ is empty (48 kHz stereo, ~10 s)."""
import numpy as np, soundfile as sf, sys, pathlib
from scipy import signal
SR = 48000
rng = np.random.default_rng(1234)

def pink(n):
    w = rng.standard_normal(n)
    b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
    a = [1, -2.494956002, 2.017265875, -0.522189400]
    p = signal.lfilter(b, a, w)
    return p / np.max(np.abs(p))

def castanets(dur):
    n = int(dur * SR); x = np.zeros(n)
    t = np.arange(int(0.12 * SR)) / SR
    for onset in np.arange(0.2, dur - 0.2, 0.17):
        i = int(onset * SR)
        click = rng.standard_normal(len(t)) * np.exp(-t / 0.006)
        for f, q in [(2200, 8), (3600, 10), (5200, 12)]:
            sos = signal.iirpeak(f / (SR / 2), q, ) if False else None
        b, a = signal.iirpeak(3000 / (SR / 2), 4)
        click = signal.lfilter(b, a, click) * 8 + click
        x[i:i + len(t)] += click[: n - i][: len(t)] * rng.uniform(0.6, 1.0)
    return x / np.max(np.abs(x)) * 0.8

def chord(dur):
    n = int(dur * SR); t = np.arange(n) / SR; x = np.zeros(n)
    for note in [220.0, 277.18, 329.63, 440.0]:
        for h in range(1, 14):
            f = note * h * np.sqrt(1 + 0.0004 * h * h)
            if f > 16000: break
            x += np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) * (1.0 / h ** 1.3) * np.exp(-t / 6.0)
    env = np.minimum(1, t / 0.01)
    x *= env
    return x / np.max(np.abs(x)) * 0.7

def speechlike(dur):
    n = int(dur * SR); t = np.arange(n) / SR
    f0 = 110 + 25 * np.sin(2 * np.pi * 0.7 * t) + 10 * np.sin(2 * np.pi * 3.1 * t)
    phase = np.cumsum(f0) / SR
    src = signal.sawtooth(2 * np.pi * phase) * 0.5
    src += 0.05 * rng.standard_normal(n)
    out = np.zeros(n)
    seg = int(0.12 * SR)
    forms = [(700, 1200, 2600), (300, 2200, 3000), (500, 1500, 2500), (800, 1100, 2800), (350, 900, 2400)]
    for s in range(0, n - seg, seg):
        fs_ = forms[(s // seg) % len(forms)]
        y = src[s:s + seg]
        acc = np.zeros(seg)
        for f, g in zip(fs_, (1.0, 0.6, 0.3)):
            b, a = signal.iirpeak(f / (SR / 2), 12)
            acc += g * signal.lfilter(b, a, y)
        w = np.hanning(seg)
        out[s:s + seg] += acc * w
    env = (0.6 + 0.4 * np.sin(2 * np.pi * 2.5 * t) ** 2)
    out *= env
    return out / np.max(np.abs(out)) * 0.7

def stereoize(m, width=0.3):
    d = signal.lfilter([0.5, 0.5], [1], np.roll(m, 37)) * width
    return np.stack([m + d, m - d], 1)

def main(outdir):
    out = pathlib.Path(outdir); out.mkdir(exist_ok=True)
    dur = 10.0
    sigs = {
        'synth_pink': np.stack([pink(int(dur * SR)) * 0.3, pink(int(dur * SR)) * 0.3], 1),
        'synth_castanets': stereoize(castanets(dur), 0.15),
        'synth_chord': stereoize(chord(dur), 0.25),
        'synth_speech': stereoize(speechlike(dur), 0.05),
    }
    mixn = int(dur * SR)
    mix = 0.45 * sigs['synth_chord'] + 0.35 * sigs['synth_castanets'] + 0.25 * sigs['synth_speech'] + 0.08 * sigs['synth_pink']
    sigs['synth_mix'] = mix / np.max(np.abs(mix)) * 0.8
    for k, v in sigs.items():
        sf.write(out / f'{k}.wav', v.astype(np.float32), SR, subtype='PCM_24')
        print('wrote', k)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '../../testaudio')
