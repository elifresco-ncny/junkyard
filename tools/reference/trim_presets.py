#!/usr/bin/env python3
"""Trim each factory preset's output_gain so its loudness on the reference mix is within ~1 dB of Init."""
import subprocess, json, pathlib, sys, tempfile
import numpy as np, soundfile as sf
ROOT = pathlib.Path(__file__).resolve().parents[2]
RENDER = ROOT / 'build' / 'junkyard_render'; PRE = ROOT / 'presets'
REFS = [ROOT / 'testaudio' / 'synth_mix.wav', ROOT / 'testaudio' / 'synth_speech.wav']
TMP = pathlib.Path(tempfile.mkdtemp(prefix='jytrim'))
sys.path.insert(0, str(ROOT / 'tools')); import make_presets as mp

def rms_db(path):
    x, _ = sf.read(path); return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)

def loud(xmlfile):
    v = []
    for i, r in enumerate(REFS):
        out = TMP / f'o{i}.wav'
        subprocess.run([str(RENDER), str(r), str(out), '--preset-file', str(xmlfile)], check=True, capture_output=True)
        v.append(rms_db(out))
    return float(np.mean(v))

def main():
    trims = {}
    for it in range(6):
        mp.OUT.mkdir(parents=True, exist_ok=True)
        for old in mp.OUT.glob('*.xml'): old.unlink()
        (PRE / 'trims.json').write_text(json.dumps(trims))
        for i, (n, d) in enumerate(mp.PRESETS, 1):
            if n in trims: d['output_gain'] = trims[n]
            mp.write(i, n, d)
        files = sorted(mp.OUT.glob('*.xml'))
        base = loud(files[0]); worst = 0
        for f, (n, d) in zip(files, mp.PRESETS):
            if n == 'Init': continue
            l = loud(f); diff = base - l
            worst = max(worst, abs(diff))
            if abs(diff) > 0.7: trims[n] = round(float(np.clip(trims.get(n, 0) + diff, -24, 12)), 1)
            print(f'  it{it} {n:28s} {l - base:+6.1f} dB vs Init  trim {trims.get(n, 0)}')
        print('iteration', it, 'worst |diff|', round(worst, 2))
        if worst < 0.7: break
    (PRE / 'trims.json').write_text(json.dumps(trims, indent=1))
    for i, (n, d) in enumerate(mp.PRESETS, 1):
        if n in trims: d['output_gain'] = trims[n]
        mp.write(i, n, d)

if __name__ == '__main__': main()
