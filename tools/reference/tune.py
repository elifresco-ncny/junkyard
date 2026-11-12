#!/usr/bin/env python3
"""Coordinate-descent tuning of codec constants against LAME's zeroed-cell % (and bandwidth)."""
import subprocess, pathlib, sys, json, tempfile, os
from concurrent.futures import ThreadPoolExecutor
import numpy as np
from metrics import *
ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'testaudio'; REF = ROOT / 'renders' / 'compare' / 'refs'; RENDER = ROOT / 'build' / 'junkyard_render'
FILES = ['synth_speech', 'synth_mix', 'synth_chord', 'synth_castanets', 'synth_pink']
RATES = [128, 96, 64, 48, 32, 16]
TMP = pathlib.Path(tempfile.mkdtemp(prefix='jytune'))
_orig = {}; _ref = {}

def job(args):
    f, b, tune = args
    out = TMP / f'{f}_{b}_{abs(hash(tuple(sorted(tune.items()))))}.wav'
    cmd = [str(RENDER), str(SRC / f'{f}.wav'), str(out), '--bitrate', str(b), '--set', 'swirl=0']
    for k, v in tune.items(): cmd += ['--tune', f'{k}={v}']
    subprocess.run(cmd, capture_output=True, check=True)
    o, _ = load(SRC / f'{f}.wav'); r, _ = load(REF / f'{f}_lame{b}.wav'); j, _ = load(out)
    o1, r1, _ = align(o, r); o2, j1, _ = align(o, j)
    out.unlink()
    return (f, b, zeroed_pct(o1, r1), zeroed_pct(o2, j1), bandwidth20(o1, r1), bandwidth20(o2, j1))

def evaluate(tune, pool):
    res = list(pool.map(job, [(f, b, tune) for f in FILES for b in RATES]))
    err = 0
    for f, b, zr, zj, br, bj in res:
        err += abs(zj - zr) / max(zr, 5.0)
        err += 2 * max(0, abs(bj - br) / br - 0.05)
    return err / len(res), res

if __name__ == '__main__':
    space = {'noiseOffsetDb': [1.5, 3.5, 5.5], 'tonalOffsetDb': [6.5, 10.5, 14.5], 'outerPasses': [4, 6, 8, 12],
             'sfStepDb': [2.0, 3.0, 4.0, 5.0], 'c0': [0.15, 0.3, 0.5], 'c1': [1.5, 2.0, 3.0], 'c2': [1.0, 1.5, 2.2],
             'reservoirUse': [0.0, 0.5, 0.9], 'athOffsetDb': [-10.0, 0.0, 10.0]}
    cur = {'noiseOffsetDb': 3.5, 'tonalOffsetDb': 10.5, 'outerPasses': 6, 'sfStepDb': 3.0, 'c0': 0.3, 'c1': 1.5, 'c2': 1.5, 'reservoirUse': 0.5, 'athOffsetDb': 0.0}
    with ThreadPoolExecutor(4) as pool:
        best, _ = evaluate(cur, pool)
        print('start', best, flush=True)
        for sweep in range(int(sys.argv[1]) if len(sys.argv) > 1 else 1):
            for k, vals in space.items():
                for v in vals:
                    if v == cur[k]: continue
                    t = dict(cur); t[k] = v
                    e, _ = evaluate(t, pool)
                    print(f'  {k}={v}: {e:.4f}', flush=True)
                    if e < best - 1e-4: best, cur = e, t
                print('sweep', sweep, k, '->', cur[k], 'err', round(best, 4), flush=True)
        print('FINAL', json.dumps(cur), best)
