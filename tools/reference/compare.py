#!/usr/bin/env python3
"""Render each testaudio/*.wav through Junkyard at matching bitrates, compare with LAME/AAC refs.
Writes spectrogram sheets (PNG), WAVs and summary.md into renders/compare/."""
import subprocess, pathlib, sys, json
import numpy as np, soundfile as sf
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from metrics import *
ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'testaudio'; OUT = ROOT / 'renders' / 'compare'; REF = OUT / 'refs'
RENDER = ROOT / 'build' / 'junkyard_render'
LAME = [320, 192, 128, 96, 64, 48, 32, 24, 16, 8]; AAC = [128, 64, 32]

def render(inp, outp, kbps, mode='mp3', extra=()):
    cmd = [str(RENDER), str(inp), str(outp), '--mode', mode, '--bitrate', str(kbps), '--set', 'swirl=0', *extra]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr); raise SystemExit(1)

def spec(ax, x, title):
    f, t, P = stft_pow(x.mean(1))
    ax.imshow(10 * np.log10(P + 1e-12), origin='lower', aspect='auto', cmap='magma', vmin=-110, vmax=-20,
              extent=[0, t[-1], 0, f[-1] / 1000])
    ax.set_title(title, fontsize=8); ax.set_ylabel('kHz', fontsize=7); ax.tick_params(labelsize=6)

def main(only=None):
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for w in sorted(SRC.glob('*.wav')):
        if only and only not in w.stem: continue
        orig, _ = load(w)
        for fam, rates, mode in (('lame', LAME, 'mp3'), ('aac', AAC, 'stream')):
            for b in rates:
                refp = REF / f'{w.stem}_{fam}{b}.wav'
                if not refp.exists(): continue
                jyp = OUT / f'{w.stem}_{"lame" if fam=="lame" else "aac"}{b}_junkyard{b}.wav'
                render(w, jyp, b, mode)
                ref, _ = load(refp); jy, _ = load(jyp)
                o1, r1, _ = align(orig, ref); o2, j1, _ = align(orig, jy)
                mr = all_metrics(o1, r1); mj = all_metrics(o2, j1)
                rows.append(dict(file=w.stem, fam=fam, kbps=b, ref=mr, jy=mj))
                if fam == 'lame' and b in (128, 64, 32):
                    fig, axs = plt.subplots(3, 1, figsize=(9, 8), sharex=True)
                    spec(axs[0], o1, 'original'); spec(axs[1], r1, f'{fam} {b} kbps'); spec(axs[2], j1, f'junkyard {b} kbps')
                    plt.tight_layout(); plt.savefig(OUT / f'{w.stem}_{fam}{b}.png', dpi=90); plt.close()
    json.dump(rows, open(OUT / 'metrics.json', 'w'), indent=1)
    lines = ['| file | codec | kbps | BW ref | BW jy | zeroed% ref | zeroed% jy | side dB ref | side dB jy | preecho dB ref | preecho dB jy |', '|' + '---|' * 11]
    for r in rows:
        a, j = r['ref'], r['jy']
        lines.append(f"| {r['file']} | {r['fam']} | {r['kbps']} | {a['bw']:.0f} | {j['bw']:.0f} | {a['zeroed']:.1f} | {j['zeroed']:.1f} | {a['side']:.1f} | {j['side']:.1f} | {a['preecho']:.1f} | {j['preecho']:.1f} |")
    (OUT / 'summary.md').write_text('\n'.join(lines))
    # pass/fail vs plan targets (LAME only)
    bad = 0
    for r in rows:
        if r['fam'] != 'lame': continue
        a, j = r['ref'], r['jy']
        bwok = abs(j['bw'] - a['bw']) <= 0.10 * a['bw'] if a['bw'] else True
        zok = abs(j['zeroed'] - a['zeroed']) <= max(0.30 * a['zeroed'], 3.0)
        if not (bwok and zok): bad += 1
    print('\n'.join(lines)); print(f"\nLAME rows outside target (bw +-10%, zeroed +-30%): {bad} / {sum(r['fam']=='lame' for r in rows)}")

if __name__ == '__main__': main(sys.argv[1] if len(sys.argv) > 1 else None)
