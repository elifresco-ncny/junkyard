#!/usr/bin/env python3
"""Encode every testaudio/*.wav with lame and ffmpeg-aac, decode back to 48 kHz stereo WAV into renders/compare/refs."""
import subprocess, pathlib, sys, soundfile as sf, numpy as np
from scipy import signal
ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'testaudio'; OUT = ROOT / 'renders' / 'compare' / 'refs'
LAME = [320, 192, 128, 96, 64, 48, 32, 24, 16, 8]
AAC = [128, 64, 32]

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(' '.join(map(str, cmd)), r.stderr[-400:], file=sys.stderr); raise SystemExit(1)

def to48k(path_in, path_out):
    x, sr = sf.read(path_in, always_2d=True)
    if sr != 48000: x = signal.resample_poly(x, 48000, sr, axis=0)
    if x.shape[1] == 1: x = np.repeat(x, 2, 1)
    sf.write(path_out, x.astype(np.float32), 48000, subtype='PCM_24')

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for w in sorted(SRC.glob('*.wav')):
        for b in LAME:
            mp3 = OUT / f'{w.stem}_lame{b}.mp3'; dec = OUT / f'_{w.stem}_lame{b}_dec.wav'; fin = OUT / f'{w.stem}_lame{b}.wav'
            run(['lame', '--quiet', '-b', str(b), '--cbr', str(w), str(mp3)])
            run(['lame', '--quiet', '--decode', str(mp3), str(dec)])
            to48k(dec, fin); dec.unlink(); mp3.unlink()
        for b in AAC:
            m4a = OUT / f'{w.stem}_aac{b}.m4a'; fin = OUT / f'{w.stem}_aac{b}.wav'
            run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(w), '-c:a', 'aac', '-b:a', f'{b}k', str(m4a)])
            dec = OUT / f'_{w.stem}_aac{b}_dec.wav'
            run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(m4a), '-ar', '48000', '-ac', '2', str(dec)])
            to48k(dec, fin); dec.unlink(); m4a.unlink()
        print('refs done:', w.name)

if __name__ == '__main__': main()
