#!/usr/bin/env python3
"""Generate presets/factory/NN_Name.xml (APVTS state format; real parameter values).
Run:  python3 tools/make_presets.py   (levels are trimmed afterwards by tools/reference/trim_presets.py)"""
import math, pathlib, json, sys
OUT = pathlib.Path(__file__).resolve().parents[1] / 'presets' / 'factory'
RANGES = {0: (8, 320), 1: (6, 256), 2: (1, 64)}
MODES = {'mp3': 0, 'stream': 1, 'broken': 2}
# v0.4 neutral tape defaults: Wow, Flutter, Drive, Hiss, Age 0 and Tone Off.
DEFAULTS = dict(input_gain=0, codec_on=1, codec_mode=0, bitrate=0.5637, swirl=25, hf_loss=0, glitch=0,
                tape_on=0, wow=0, flutter=0, saturation=0, hiss=0, tape_age=0, tape_tone=20000,
                order=0, width=100, low_keep=0, low_keep_freq=150, mix=100, output_gain=0)
# The v0.3 tape defaults. Presets are written in v0.3 units (below) and converted to v0.4 units by P(), so every preset with Tape on keeps
# its sound: a preset that did not mention a tape knob got the old default for it.
LEGACY_TAPE = dict(wow=20, flutter=15, saturation=30, hiss=10, tape_age=20)

def conv_wow(w):      # old depth 0.006*w  ->  v0.4 depth 0.03*w'^1.5 (v0.4.4 runs Wow at 0.75x, so presets wobble 25 % less)
    return round(100 * (0.2 * w / 100) ** (2 / 3), 1)
def conv_flutter(f):  # v0.3 depth 0.006*f^1.3  ->  new depth 0.015*f'^1.5, rounded to 0.5 %
    return round(100 * (0.4 * (f / 100) ** 1.3) ** (2 / 3) * 2) / 2
def conv_drive(s):    # old +18 dB * s  ->  new +30 dB * d
    return round(0.6 * s, 1)
ORDER = list(DEFAULTS.keys())

def norm(mode, kbps):
    lo, hi = RANGES[mode]; return round(math.log(kbps / lo) / math.log(hi / lo), 4)

def old_tone(age):  # the cutoff the old Age knob applied (v0.1); Age is no longer a lowpass
    return 20000 if age <= 0 else max(4000, round(0.7 * 18000 * (5000 / 18000) ** (age / 100.0)))  # -3 dB point of the old Age lowpass

def P(name, mode='mp3', kbps=None, **kw):
    d = dict(DEFAULTS); m = MODES[mode]; d['codec_mode'] = m
    if kbps is not None: d['bitrate'] = norm(m, kbps)
    if kw.get('tape_on') == 1:
        old = dict(LEGACY_TAPE); old.update({k: v for k, v in kw.items() if k in LEGACY_TAPE})
        d.update(wow=conv_wow(old['wow']), flutter=conv_flutter(old['flutter']), saturation=conv_drive(old['saturation']),
                 hiss=old['hiss'], tape_age=old['tape_age'])
        kw = {k: v for k, v in kw.items() if k not in LEGACY_TAPE}
    d.update(kw); return name, d

TAPE_OFF = dict(wow=0, flutter=0, hiss=0, tape_age=0)
PRESETS = [
    P('Init', 'mp3', 64, swirl=25),
    P('128k Car Stereo', 'mp3', 128, swirl=15),
    P('P2P Summer 2004', 'mp3', 64, swirl=40, glitch=8),
    P('Profile Page Player', 'mp3', 48, swirl=50, width=70, hf_loss=20),
    P('32k Podcast Rip', 'mp3', 32, swirl=35, width=0),
    P('Underwater Stream', 'stream', 24, swirl=72, glitch=6),
    P('Video Call Freeze', 'stream', 16, glitch=55, width=0, swirl=40),
    P('Bluetooth Buffering', 'stream', 96, glitch=38, swirl=15),
    P('Ringtone 2006', 'mp3', 16, hf_loss=50, width=0, swirl=30),
    P('Broken Torrent', 'broken', 12, glitch=65, swirl=50),
    P('Corrupted Drive', 'broken', 4, glitch=80, swirl=80),
    P('Dial-Up Radio', 'stream', 8, hf_loss=50, tape_on=1, hiss=55, wow=10, flutter=10, saturation=20, tape_age=50, tape_tone=old_tone(50), width=0, swirl=30),
    P('Walkman', 'mp3', 320, swirl=0, tape_on=1, wow=35, flutter=12, saturation=40, hiss=25, tape_age=40, tape_tone=old_tone(40)),
    P('Dubbed Cassette x3', 'mp3', 96, swirl=20, tape_on=1, tape_age=70, tape_tone=old_tone(70), wow=40, flutter=20, saturation=55, hiss=40),
    P('Attic Cassette', 'mp3', 320, swirl=0, tape_on=1, tape_age=100, tape_tone=7000, wow=40, flutter=45, saturation=45, hiss=30),
    P('Answering Machine', 'stream', 12, hf_loss=45, width=0, tape_on=1, tape_age=80, tape_tone=4000, saturation=40, hiss=35),
    P('Drum Bus Dirt', 'mp3', 80, swirl=20, tape_on=1, saturation=70, wow=0, flutter=0, hiss=0, tape_age=0, low_keep=1, low_keep_freq=120, mix=60),
    P('Vocal Haze', 'stream', 32, swirl=40, low_keep=1, low_keep_freq=180, mix=35),
    P('Lo-Fi Glue', 'mp3', 128, swirl=15, tape_on=1, wow=10, flutter=8, saturation=25, hiss=8, tape_age=15, tape_tone=old_tone(15), mix=40),
]

def fname(i, n): return f"{i:02d}_" + ''.join(c if c.isalnum() else '_' for c in n).strip('_') + '.xml'

def write(i, name, d):
    lines = [f'<?xml version="1.0" encoding="UTF-8"?>', f'<JUNKYARD stateVersion="3" presetName="{name}">']
    for k in ORDER: lines.append(f'  <PARAM id="{k}" value="{d[k]}"/>')
    lines.append('</JUNKYARD>')
    (OUT / fname(i, name)).write_text('\n'.join(lines) + '\n')

if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob('*.xml'): old.unlink()
    trims = json.load(open(OUT.parent / 'trims.json')) if (OUT.parent / 'trims.json').exists() else {}
    for i, (n, d) in enumerate(PRESETS, 1):
        if n in trims: d['output_gain'] = trims[n]
        write(i, n, d)
    print('wrote', len(PRESETS), 'presets')
