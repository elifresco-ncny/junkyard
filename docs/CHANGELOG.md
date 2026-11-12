# CHANGELOG

## M1 — Parameters, state & engine skeleton
- All 24 automatable params with frozen IDs, state versioning (`stateVersion=1`), engine with placeholder codec (2N delay) / tape (10 ms delay), latency-aligned dry + bypass, Low Keep LR4, width, safety limiter.
- Reported latency 1504 samples @ 48 kHz (1024 codec + 480 tape).

## M2 — Codec core (MP3 mode)
- Own radix-2 FFT-based orthonormal MDCT (2N-pt complex FFT + twiddles), Bark bands (~21), psychoacoustic lite, MP3-style inner/outer loops, bit reservoir, LAME-like cutoff table, M/S + intensity stereo, Swirl, HF Loss.
- `junkyard_render` (CLI) and `junkyard_bench` (CPU: ~0.85 % of one core, all sections on, MP3 mode).

## M3 / M4
- M3: calibration vs LAME (see docs/CALIBRATION.md, DECISIONS.md).
- M4: Stream mode (noise fill, SBR-lite below 64 kbps, parametric-stereo-lite below 32, PLC smear, buffering gaps), Broken mode (scalefactor corruption, bin shuffles, spectral requantisation, frame holds, history/half-band/reversed/loud-burst glitches, L/R swap + mono collapse, rectangular-window buzz), Gilbert-Elliott glitch engine for all modes, mode switch at frame boundaries.

## M5 — Crunch + Tape
- Crusher: fractional bit depth (mid-tread, no dither), sample-and-hold rate (no AA); Rate at max = off at any host rate.
- Tape: wow (0.8 Hz sine + slow noise) & flutter (~9 Hz + 2nd harmonic + noise) on a 10 ms modulated cubic delay; pre-emphasised ADAA asymmetric tanh saturation with head bump + level-referenced makeup; age LP sweep 18k→5k + inter-channel azimuth drift; Poisson dropouts; tilted pink hiss −90..−30 dBFS. Order switch (10 ms duck on change).

## M6 — Chaos
- ChaosBank: smoothed random modulators (value noise): bitrate wander ±0.35·c, swirl +0.4·c, cutoff ±1 oct·c, wow rate/depth, flutter, crunch rate wobble, plus "bad network spells" (glitch boost for 0.5–4 s, smoothed). Never touches host parameters; effective values exposed via `JunkyardEngine::getModulated()` for the UI rings.
- Reseed on play: transport start → full engine reset with fixed seeds (processor checks `getPlayHead()`); setting is stored in plugin state.

## M7 — Real UI
- Scrapyard panel UI (960×560, resizable 75–150 %, aspect locked, scale saved), Bebas Neue + Share Tech Mono (OFL) compiled in. Knobs: drag, Shift-at-press = fine, double-click reset, wheel, hover = value, Cmd/Ctrl-click = type; cyan chaos rings; section panels dim when off; segmented mode/order selectors; green LCD bitrate readout (+ "~ effective kbps" when Chaos moves it); live Damage Scope (blue kept, black holes, red glitch frames); tooltips on everything; ⚙ menu (UI scale, reseed on play, build stamp, About); dev build stamp bottom-right.
- `junkyard_shot` renders the editor offscreen to PNG for self-review (`renders/ui/`).

## M8 — Presets
- 20 factory presets (XML compiled in; exposed as JUCE programs), loudness-trimmed to within ±1 dB of Init on a reference mix (`tools/reference/trim_presets.py`, `presets/trims.json`; regenerate XML with `tools/make_presets.py`).
- User presets: `.jyd` in `~/Library/Audio/Presets/Junkyard Audio/Junkyard/` (Windows: `Documents\Junkyard Audio\Junkyard\Presets`), Save button prompts for a name; modified `*` flag; ◄ ► step through factory + user; name button opens a Factory/User menu with Undo/Redo and "Show user preset folder".
- Scrap randomiser (mode, bitrate, swirl, HF loss, glitch, crunch, tape, order, width, chaos; Mix/Output/Input/Low Keep untouched), undoable (Cmd-Z in the editor, or the preset menu).
- Live `.adv` device presets work through `getStateInformation` (state also stores the preset name).

## M9 — Hardening
- Added tests: zero-allocation audio thread, edge signals, sample-rate/block-size changes, parameter automation fuzz, M1 state fixture, bypass/mix-0 null (see docs/TESTING.md). pluginval strictness 10 (VST3 + AU, GUI tests on) and auval pass; 2 h faster-than-real-time soak clean; 20 instances ≈ 18.7 % of a core.

## v0.2 — owner feedback round 1 (FIX_PLAN.md)
- **Knob readout** now follows the value during a drag (`slider.onValueChange` repaints the label; the readout also stays while the button is held).
- **Standing noise with two instances fixed.** Root causes: Hiss 0 % was −90 dBFS pink noise (now exactly off; 1 % ≈ −80 dBFS, 100 % = −30 dBFS, hiss generator skipped when 0, own RNG), and the float ADAA saturator turned a −90 dB floor into −71 dB noise (now double precision, cached `tanh(bias)`). Saturation fades in over the first 5 % so 0 % is exactly transparent and nothing jumps; head bump 0…+3 dB scaled by saturation, coefficients updated only on change.
- **Tone knob (new, `tape_tone`)**: the old Age lowpass, now its own control, 4–20 kHz, 20 kHz = Off (bypassed), smoothed. Specified as the −3 dB point.
- **Age rebuilt, no HF loss**: azimuth wander (±0.25 ms, magnitude-flat all-pass fractional delay), level wobble (2–8 Hz, ±1.5 dB), signal-dependent modulation noise (1–8 kHz, ≈ −45 dB). Age 0 is bit-transparent; silence in = silence out.
- **Dropouts removed. Chaos removed** (parameter, ChaosBank, mod plumbing, ring UI, LCD sub-line). `ValueNoise` kept (moved to `ValueNoise.h`). Footer: ORDER left, INPUT · MIX · OUTPUT right. Codec output for chaos 0 is bit-identical to before for MP3 and Stream (checked by render diff; the only differences are the limiter change above 0 dBFS and the Broken burst cap).
- **Tooltips** rewritten to plain descriptions (table in FIX_PLAN.md §6); About text simplified.
- **State v2 migration**: drops chaos/dropouts, derives `tape_tone` from the old Age (0.7 × the old lowpass nominal cutoff = its −3 dB point, clamped ≥ 4 kHz; Age 0 → Off). User-preset files go through the same migration.
- **Cmd-Z key handler removed** (it was stealing Live's undo). Undo/Redo stay in the preset menu.
- **Safety limiter**: knee 0 dBFS, ceiling +6 dBFS (was −1/−0.3 dBFS and coloured normal material). NaN/Inf guard unchanged. Broken glitch "loud burst" gain capped at +12 dB (was +12…20).
- **Scrap** now sets bitrate through a change gesture; dead variable removed.
- Housekeeping: removed `tune.py.bak`, added `assets/fonts/OFL-ShareTechMono.txt`, PLAN.md/CLAUDE.md updated, version 0.2.0.
- **Presets**: regenerated for the 23-parameter set and re-trimmed (all within ±0.7 dB of Init).
  - Underwater Stream: chaos 40 → swirl 60→72, glitch 6.
  - Video Call Freeze: chaos 50 → glitch 45→55, swirl 30→40.
  - Bluetooth Buffering: chaos 30 → glitch 30→38, swirl 10→15.
  - Broken Torrent: chaos 60 → glitch 55→65, swirl 40→50.
  - **Ghost Stream kept**, re-tuned as heavy Stream 20 (swirl 80, glitch 30) + tape wow 45/age 40, which keeps it distinct from Underwater Stream.
  - Dubbed Cassette x3: dropouts 25 removed. Dial-Up Radio, Walkman, Dubbed Cassette, Answering Machine, Lo-Fi Glue, Ghost Stream: `tape_tone` set to the old Age lowpass (−3 dB point) so they stay as dark as before; Age values unchanged but now mean wear instead of darkness. Drum Bus Crunch: tone Off (its age was 0).
  - Trim changes: Broken Torrent −5.0 dB, Corrupted Drive −6.5 dB, Vocal Haze +2.0, Video Call Freeze +1.4, Ghost Stream +1.0, Drum Bus +1.0.

## v0.2.1 — Age mono fix (build #48)
- Age azimuth drift cut from 0.25 ms to 0.02 ms L/R offset. In mono or with Width narrowed, Age no longer dulls the highs (mono HF change at Age 100: −0.09 dB, was about −3 dB). Mono test limit restored to ±2 dB.

## v0.3 — owner feedback round 2 (FIX_PLAN_2.md)
- **Crunch removed** (note 3): `crush_on`, `bits`, `downsample`, the Crusher section, the CRUNCH panel and Bits/Rate knobs are gone. 23 -> 20 parameters. State version 3; the migration drops the three PARAM entries from v1/v2 states and user presets. Order choices renamed `Codec > Tape` / `Tape > Codec` (IDs and values unchanged). Editor re-flowed: TAPE (600 px) and STEREO (328 px) fill the lower row, window size unchanged. Codec-only and tape-only renders are bit-identical to v0.2.1 (6 renders compared).
- **Hiss only while playing** (note 1): the host's `isPlaying` reaches the engine as `transportRunning` (true when the host gives no position info, so Standalone always hisses). The hiss gain is multiplied by a gate that is open while the transport runs or the tape input has been above -80 dBFS within the last 0.5 s, ramped over 50 ms. Closed gate skips the pink generator.
- **Gain matching** (note 4): Saturation now has level-tracking makeup (two stereo-linked 300 ms mean-square followers around the stage, correction clamped to +-12 dB, 50 ms smoothing, held below -70 dBFS). Saturation 30/60/100 is now within +-1 dB of the input at -6/-18/-30 dBFS (was -4.4/-6.4/-7.8 dB loud, up to +5.2 dB quiet). Broken mode gets a slow (1 s) level follower that only ever turns the corrupted output down (gain 0.5..1): it was +1.2..+2.3 dB hotter than the input at 64 kbps, now within +-1 dB. MP3 and Stream already matched. **Note 6 ("320 kbps a hair louder")**: measured -0.08 dB RMS, so not louder; codec overshoot on a few peaks can make a meter read hotter. No change, locked in by a test.
- **Flutter** (note 2b): +-0.6 % at 100 % (was +-0.15 %), curve flutter^1.3, rate 8 Hz wandering +-35 %, plus 40-80 Hz "scrape" noise (+-0.04 % * flutter). Default 15 % is +-0.066 % (was +-0.02 %).
- **Age rebuilt** (notes 2, 2b): no pitch movement, no dropouts, no static high cut. Level wobble +-3 dB * a^1.2 (slow + fast, plus a very slow L/R imbalance), grain -28 dB re the signal * a^1.2 (800 Hz-8 kHz, signal-dependent), HF wander (6 kHz high shelf, 0..-6 dB * a, slow, never boosts, 70 % L/R correlated), print-through ghost echo (1.3 s, -48 dB at 25 % to -32 dB at 100 %, 150 Hz-3 kHz, off below 25 %). Gains/coefficients update every 16 samples (no per-sample `pow`). Azimuth drift unchanged.
- **Tooltips** rewritten to the exact text in FIX_PLAN_2.md section 5. About box now shows only version, build stamp and font credits.
- **Mode memory** (note 9): clicking the Mode switch in the UI stores Bitrate/Swirl/HF Loss/Glitch for the mode being left and restores the entered mode's values (first visit keeps the kbps, clamped). Stored in a `MODE_MEMORY` child of the session state; presets, Init, Scrap and undo clear it; it is stripped from user presets. Host/automation mode changes only change the mode.
- **Presets** (note 8), 19 in total: removed *192k Almost Fine* and *Ghost Stream*; *Drum Bus Crunch* -> *Drum Bus Dirt* (Saturation 70, no Crunch); *Vocal Haze (parallel)* -> *Vocal Haze*; new *Attic Cassette* (MP3 320, Age 100, Tone 7 kHz, Wow 40, Flutter 45, Sat 45, Hiss 30); *Ringtone 2006* re-tuned (MP3 16, HF Loss 50, Width 0, Swirl 30); *Answering Machine* re-tuned (Stream 12, HF Loss 45, Width 0, Age 80, Tone 4 kHz, Sat 40, Hiss 35); *Walkman* Flutter 25 -> 12 and *Dubbed Cassette x3* Flutter 30 -> 20 (flutter is about 2.5x stronger). All re-trimmed to within +-0.7 dB of Init: trims Video Call Freeze +1.4, Broken Torrent -1.0, Corrupted Drive -1.9, Drum Bus Dirt +0.9, Vocal Haze +2.0.
- Tools: `junkyard_soak <hours> wrecked` pins Age/Wow/Flutter 100 and Glitch 50; bench now runs with Age/Wow/Flutter 100. Renders in `renders/v0.3/{age,flutter,presets,ui}`.
- Version 0.3.0.

## v0.4 — owner feedback round 3 (FIX_PLAN_3.md)
- **No moving gain anywhere.** Removed the tape saturation level follower and the Broken-mode follower. Broken now uses a fixed trim table that depends only on Bitrate and Glitch (`brokenTrimDb`, generated by `junkyard_calibrate broken`, never boosts). New "no level memory" tests: the tape (Drive 50 %) and Broken at 32 kbps (with and without the trim) give the same level after a loud burst as without it.
- **Hiss follows the music.** No transport gate any more (`EngineParams::transportRunning` is gone; the transport is used only for reseed-on-play). The hiss level follows an envelope of the tape signal after the delay line (3 ms attack / 100 ms release): full hiss while the music is loud, falling 1 dB per dB below the knee, gone below -80 dBFS. Silent input = exactly 0; hiss never starts before the music; 1 s after the music ends it is < -100 dBFS; -40 dBFS pink gives 10.8 dB less hiss than -10 dBFS pink.
- **Drive replaces Saturation** (ID `saturation` unchanged, display name Drive, default 0, 0-100 % = 0..+30 dB; Saturation 100 of v0.1 = Drive 60 %). The stage is `DriveStage` (same filters / ADAA / bias; head bump 3 dB at +30 dB). Level matching is a **fixed calibrated curve** (`DriveCalTable.h`: 31 drives x 37 loudnesses, five synthetic materials, BS.1770 K-weighted LUFS; default row = minimax over -26..-6 LUFS, `kDefaultL` = -14 LUFS; record in `docs/DRIVE_CAL.md`). A kick at Drive 100 % reaches a crest factor of 2.1 dB (4.0 dB at 60 %).
- **Learn** button (small pill at the top right of the Drive knob; default = dim outline, listening = pulsing hazard, analysing = solid hazard, learned = soft fill): click to listen to 5 s of the signal entering the tape section (silence is skipped, 30 s timeout) and fit the curve to it (+-0.5 dB at Drive 0 / 30 / 60 / 100 on the mastered mix and a -24 LUFS drum loop); right-click for "Learn from input" / "Use default curve". Works with the editor closed (processor-owned 20 Hz timer + one worker thread), saved with the Live set as the `driveCal` state property, never in presets, kept across preset loads / Init / Scrap. Moving Input after learning keeps Drive's own compensation accurate (Input -6 dB gives output -6 dB +-1). The curve change ramps over 200 ms; there is no automatic gain change at any time.
- **Wow and Flutter** are obvious at 100 %: Wow +-3 % (was +-0.6 %), Flutter +-1.5 % (was +-0.6 %), both with a ^1.5 curve (Wow 20 % = +-0.26 %). The delay buffer limit is 1.95 x the 10 ms centre; no clamping at 44.1 / 48 / 96 / 192 kHz over 60 s at 100/100.
- **Age rebuilt** into four signal-driven parts (old wobble, HF wander, print-through echo, azimuth drift and grain deleted): **Mistracking** (dull on quiet passages, open on loud), **Halo** (fuzzy cloud that hugs and hangs after notes), **Crackle** (soft dust on transients), **Side-B bleed** (faint reversed ghost of the music's own past). Age 0 bit-transparent, silence in = silence out exactly. Tone moved to the end of the tape chain.
- **Neutral tape defaults:** Wow, Flutter, Drive, Hiss, Age = 0, Tone = Off (codec defaults unchanged). Init equals the defaults.
- **Presets** keep their sound: the six tape presets and Drum Bus Dirt / Lo-Fi Glue were converted to the new units (table below), then re-trimmed to within +-0.7 dB of Init. Their Age is the new Age, a different effect: Walkman, Dubbed Cassette x3, Attic Cassette, Dial-Up Radio, Answering Machine and Lo-Fi Glue need an ear check. Scrap's wow / flutter / drive ranges were converted too.
- **Tooltips** for Drive, Learn, Hiss, Age, Wow, Flutter, Tone rewritten (FIX_PLAN_3 section 7).
- New tools / tests: `junkyard_calibrate` (drive / broken / age / alias / agerender), shared synthetic corpus (`Corpus.h`), `Loudness.h` (BS.1770-4), `junkyard_bench` Age 0/100 modes, `junkyard_soak <h> wrecked learn`, `junkyard_shot --learn / --listening`; tests `test_v04.cpp`, `test_age.cpp`, `test_learn.cpp` (96 test cases in total). Renders in `renders/v0.4/{presets,age,ui}`.
- Version 0.4.0.

| Preset (Tape on) | Wow | Flutter | Drive (was Saturation) | Hiss | Age | Output trim (dB) |
|---|---|---|---|---|---|---|
| Dial-Up Radio | 10 -> 7.4 | 10 -> 7.5 | 20 -> 12 | 55 | 50 | 0.0 -> -2.3 |
| Walkman | 35 -> 17 | 12 -> 8.5 | 40 -> 24 | 25 | 40 | 0.0 -> -2.4 |
| Dubbed Cassette x3 | 40 -> 18.6 | 20 -> 13.5 | 55 -> 33 | 40 | 70 | 0.0 -> -2.6 |
| Attic Cassette | 40 -> 18.6 | 45 -> 27 | 45 -> 27 | 30 | 100 | 0.0 -> -2.5 |
| Answering Machine | 20 -> 11.7 | 15 -> 10.5 | 40 -> 24 | 35 | 80 | 0.0 -> -2.3 |
| Drum Bus Dirt | 0 | 0 | 70 -> 42 | 0 | 0 | +0.9 -> -0.9 |
| Lo-Fi Glue | 10 -> 7.4 | 8 -> 6 | 25 -> 15 | 8 | 15 | 0.0 -> -0.8 |

Other trim changes: Broken Torrent -1.0 -> -0.9, Corrupted Drive -1.9 -> -4.0 (Broken's level now comes from the fixed table). Presets with Tape off did not change their parameters.

## v0.4.1 — review fixes (build #51)
- Learn: fixed a race where pressing Learn again after a finished learn could write past the capture buffer (reset is now requested before entering Waiting, plus a bounds guard).
- Crackle grains +10 dB (mean −22 dB re envelope, cap −10): measured −35 dB re the signal on drums (was about −45), still passing the soft/organic tests.
- Tail length reported to the host: 0.1 s → 1.5 s, so freezes and bounces keep the Age halo and side-B tails.
- Flutter tooltip: "up to 2%" (measures ±1.9 % at 100).
- Learn button redesigned: a quiet outlined pill centred above the Drive knob with a small status LED (off = default curve, breathing = listening, bright = analysing, lit = learned) instead of a pill that fills with yellow.

## v0.4.2 — Learn control redesign (build #52)
- Learn moved into the Tape panel header, above the stripe, with the push button centred on the Drive knob: lamp on the left, a round 3D push button (cap sunk in a dark collar, presses down on click, fires on release), "LEARN" text on the right, 11 px gaps.
- Lamp: off = unlit amber lens (default curve); listening/analysing = smooth sine fade (1.4 s period, 8-85 % brightness, lens and glow fade together); learned = fully lit with a soft glow. LEARN text brightens once learned.

## v0.4.3 — Learn lamp, variant B (build #53)
- Learn control picked from four rendered variants (owner chose B): smaller and flatter. Lamp 8 px flat lens (no highlight) in a dark rim, push button 18 px (flat collar, cap with a soft top-to-bottom shade), "LEARN" text 12 px, 8 px gaps.
- Lit lamp is now the panel yellow (`hazard`, #F2C200, was #FFD21A) with a light glow (alpha 0.25, was 0.55).
- Listening/analysing pulse unchanged: sine fade, 1.4 s period, peaking at 85 % brightness (floor 8 %).
- Lamp 8 -> 7 px and LEARN text 12 -> 11 px (owner picked size variant B of four mockups; build #54). Button and gaps unchanged.

## v0.4.4 — gentler Wow (build #55)
- Wow depth x0.75 (owner: "slightly turn down the sensitivity to the wobble"): peak speed `0.0225 * w^1.5` (was 0.03), so Wow 100 % is about +-2.2 % (was +-2.95 %), Wow 50 % about +-0.8 % (was +-1.0 %). Rate and shape unchanged. Tooltip "up to 2.2%".
- Presets keep their knob values, so every preset with Wow wobbles 25 % less.

## v1.0.0
Debut.
