# Testing

Run everything: `scripts/dev.sh` (configure → build → ctest → pluginval → install). Unit tests alone: `build/junkyard_tests`.

## Automated (all passing at v0.4: 96 test cases; the v0.3 hiss-gate, level-follower, flutter and Age tests were replaced)
| Area | What is checked |
|---|---|
| MDCT | fast vs naive reference (N = 512/1024/2048, err < 1e-5 × peak); TDAC perfect reconstruction < −100 dB; codec pass-through = exact 2N delay at 44.1/48/96 kHz |
| Latency | impulse lands exactly at the reported latency for every section on/off combination × both Orders × 44.1/48/88.2/96 kHz × block sizes {1, 7, 64, 511, 512, 4096}; also through the quantising codec (MP3 + Stream) at 320 kbps; 1504 samples @ 48 kHz |
| Null | Mix 0 and host bypass = input delayed by L (< 1e-6) |
| Determinism | same input after `reset()` = bit-identical output (Chaos 0.8, tape, crunch, glitch, Stream); output independent of host block size (1 / 37 / 97 / 512) |
| Codec behaviour | bitrate monotonicity (non-zero bins fall as kbps falls, noise + music-like), measured −20 dB bandwidth within ±10 % of the cutoff table (192…48 kbps), Gilbert-Elliott bad-frame fraction rises with Glitch, Stream buffering gaps occur at Glitch 100 %, all modes × bitrates × glitch finite & deterministic, mode switching mid-stream stays finite/bounded |
| Tape | ADAA aliases less than plain tanh and is clean on −90 dB signals, wow+flutter pitch deviation within spec, Hiss 0 % exactly 0 / 1 % ≈ −80 / 100 % = −30 dBFS, Tone off transparent and −3 dB point within 20 % of the setting, Age 0 bit-transparent, Age leaves the long-term spectrum within 1 dB (stereo) / 3.5 dB (mono sum), silence in = silence out, full chain finite at extremes |
| Silence | every mode × sections on × Saturation 0/0.3/1 × Order: 3 s of silence after a noise burst → tail < 1e-7; two chained engines at −90 dBFS add < 3 dB; all 19 factory presets with Hiss 0, chained twice: tail < 1e-6 |
| Safety | fuzz (random params each block, 10 s noise + silence, mono + stereo): no NaN/Inf, peak ≤ 1; limiter bounds + NaN guard; edge signals (silence, DC, full-scale square, Nyquist square) × 3 modes × mono/stereo × block 1/64/512 finite and ≤ ceiling |
| Real-time safety | global `operator new` counter: **0 allocations** in `processBlock` (and bypass) over 600 blocks, all sections on, every mode, mono and stereo |
| Params / state | 20 IDs, display strings, text → value, save/load round trip, garbage/empty state ignored, missing params → defaults, an M1-era state fixture (`tests/fixtures/m1_state.xml`) loads, preset name survives state round trip |
| Hosting | knob readout text follows the slider; v1 states/user presets migrate (chaos/dropouts dropped, tone derived from age); sample-rate (22.05 → 192 kHz) and block-size changes re-report correct latency and stay stable; all parameters automated every block for 20 s |
| Presets | 19 factory presets load, `Init` == defaults, modified flag, programs exposed, Scrap leaves Mix/Output/Input/Low Keep alone and is undoable, user presets round-trip |

## v0.3 additions (`tests/test_v03.cpp`, plus migration test in `test_state.cpp`)
| Area | What is checked |
|---|---|
| Hiss gate | stopped + silent in = exactly 0 after the 50 ms ramp; running = about -30 dBFS at Hiss 100; stopped with a -43 dBFS tone in = hiss present, gone within 0.65 s of the tone ending |
| Gain matching | Saturation {30, 60, 100} x pink noise {-6, -18, -30} dBFS RMS: output within +-1 dB; step -30 -> -6 dBFS settles within 1.5 dB and never overshoots by more than 4 dB; Saturation 0 bit-exact; MP3 320 within 0.25 dB RMS, every mode at 64 kbps within 1 dB |
| Flutter | 100 % >= 0.5 % peak pitch deviation (measured 0.77 %), 76 % of modulation energy in 5-14 Hz; default 15 % <= 0.08 % (measured 0.066 %) |
| Age | Age 0 bit-exact delayed copy; at Age 100 with Wow = Flutter = 0 pitch moves 0.1 cent and the level never dips 6 dB (min -3.4 dB); envelope std 1.6 dB on a sine, 8-16 kHz band std 1.6 dB on pink noise; grain -29 dB re signal (800 Hz-8 kHz); print-through at 1.3 s, -32 dB, absent at Age 0; > 8 kHz spectrum within 0.3 dB (stereo and mono); RMS within 0.4 dB; silence in = silence out |
| State | v2 state with crush params loads without them and saves as v3 |
| Mode memory | per-mode knobs restored on UI switches, first visit keeps kbps (clamped), host-side mode change touches nothing else, memory survives get/setStateInformation, preset load and Scrap clear it, user preset XML never contains it |

## v0.4 additions (`tests/test_v04.cpp`, `test_age.cpp`, `test_learn.cpp`, `test_hardening.cpp`)
| Area | What is checked |
|---|---|
| Loudness | BS.1770-4 K-weighted LUFS: 997 Hz sine at -20 dB RMS reads -20 (one channel) / -17 (both), at 44.1 and 48 kHz |
| No level memory | tape (Drive 50 %) after a 1 s burst at -2 dBFS = without it (+-0.05 dB); Broken 32 kbps +-0.5 dB with and without the trim; trim table bilinear / <= 0 |
| Hiss | silent in = exactly 0 (Hiss 100 %, Age/Drive/Wow on); tone burst: 0 before it, -30 dBFS during, < -100 dBFS 1 s after; -40 dBFS pink 10 dB (+-1.5) below -10 dBFS pink; 0 % off, 1 % = -80 dBFS |
| Drive | Drive 0 bit exact; applied makeup gain bit-identical every sample (drums + silences + jumps); crest of the -10 LUFS drum loop <= 5.0 dB at 60 %, <= 4.0 at 100 %; default curve within 2.5 dB of the input on all 5 materials (plan 1 dB, see DECISIONS); calibration guard (table = DriveStage, +-0.05 dB); lookups / clamps |
| Learn (real processor) | accuracy +-0.5 dB (mastered mix, drums -24 LUFS) at Drive 0/30/60/100; Input -6 dB correction +-1 dB; silent input times out at 30 s; gaps skipped; click cancels; Use default; state round trip; preset load/Init/Scrap keep it; user preset has no `driveCal`; malformed property ignored; 1-sample blocks: 0 allocations on the audio thread and no level step |
| Wow / Flutter | Wow 100: +-2.95 % at 0.9 Hz; Flutter 100: >= 1.3 % at 5-14 Hz; Wow 20 <= 0.35 %; no clamping at 44.1/48/96/192 kHz over 60 s |
| Age | Age 0 bit exact; silence in/out bit exact (whole + each part); dry path pitch < 1 cent; mistracking (-0.1 dB loud, -3.5 dB quiet, 0 at Age 0, full mix > 8 kHz -0.95 dB); halo (hang, no echo, resembles the source, level -31/-25/-16 dB); crackle (centroid, no one-sample clicks, level, irregular, 6.7x denser on hits); bleed (exact 0 after 2 s, continuous, not a clean reverse, level -51/-44/-32 dB); loudness within 1 dB on all materials at -14/-30 LUFS |
| Defaults | Wow/Flutter/Drive/Hiss/Age 0, Tone Off, codec defaults, Init = defaults |

Hidden `[.report]` test (`build/junkyard_tests "[.report]"`) prints the measured numbers quoted in DECISIONS.md.

## Tools / manual-style checks run by the agent
- `pluginval --strictness-level 10 --validate-in-process` (GUI tests on, includes opening/closing the editor repeatedly and parameter fuzzing): VST3 **pass**; AU **pass** (installed component). `auval -v aufx Jky1 Jnky`: **pass**.
- `junkyard_soak 2` — 2 hours of audio rendered with Chaos 50–100 %, random parameter jumps every 2 s, all sections toggling: 0 non-finite samples, peak 0.965, CPU flat (≈0.65 % of a core all the way through), memory growth 0 KB. (A real-time Standalone soak was not run; this is the faster-than-real-time equivalent of the engine.)
- `junkyard_bench`: ≈1.0 % of one core per instance (MP3 mode, everything on, 48 kHz/128); 20 instances ≈ 18.7 % of one core total.
- Screenshots at 75 %, 100 %, 150 % (`junkyard_shot`) reviewed for layout.
- Calibration vs LAME: `docs/CALIBRATION.md`.

## v0.3 verification (build #49)
- pluginval strictness 10 (`validate.sh max au`, GUI tests skipped as in the sandboxed dev loop): VST3 **pass**, AU (installed) **pass**; `auval -v aufx Jky1 Jnky` **pass**.
- `junkyard_soak 1 wrecked` (Age/Wow/Flutter 100, Glitch 50, modes cycling, random everything else, 1 h simulated): 0 non-finite samples, peak 1.96, CPU flat 0.87 %, memory growth 0 KB.
- `junkyard_bench 10` (Age/Wow/Flutter 100, Sat 60, tape + Low Keep on, 48 kHz / 128): MP3 1.26 %, Stream 1.01 %, Broken 0.90 % of one core; `junkyard_bench 10 20` (20 instances, Stream): 19.2 % total (0.96 % each).
- `junkyard_shot` at 100 % and 150 % (`renders/v0.3/ui/`): CODEC / TAPE / STEREO (now SIGNAL) fill the panels, no hole where Crunch was.

## Not testable here (owner's checklist)
- Offline render / Freeze / Export in Ableton; sample-rate change in Live's audio prefs while playing; real-project use for a session; listening judgement of every mode.

## v1.0.0 verification (universal Release build, `scripts/build-release-mac.sh`)
- Slices: VST3 and AU both `x86_64 arm64`; deployment target 11.0, no `-march` flags.
- Tests: arm64 `ctest` 96/96; x86_64 under Rosetta `junkyard_tests` all passed (96 test cases, 12,773,051 assertions). No test needed a tolerance change (see DECISIONS.md v1.0, "Loosened tests").
- pluginval strictness 10 (`--validate-in-process --skip-gui-tests`) on the Release VST3: arm64 **SUCCESS**, x86_64 (`arch -x86_64`) **SUCCESS**.
- `auval -v aufx Jky1 Jnky` on the installed Release AU: native arm64 **PASS**, `arch -x86_64 auval` **PASS**. (The dev build was reinstalled afterwards.)
- `junkyard_bench` (Release, 20 s audio, one instance): arm64 MP3 1.29 %, Stream 1.16 %, Broken 1.09 % of one core; x86_64 under Rosetta (information only, not a limit; Rosetta is slower than a real Intel Mac) 1.96 / 1.93 / 1.84 %.
- Not run here: Gatekeeper / `spctl` checks (need the signed, notarized pkg), Windows build and pluginval (Phases 4/5/W, never run on Windows yet).

### Packaging (Phase 3, unsigned mode only; no Developer ID certificate exists yet)
- `scripts/release-mac.sh --unsigned` produced `dist/Junkyard-1.0.0-mac-UNSIGNED.zip` (13.6 MB): both bundles are `x86_64 arm64`, ad-hoc signed, `codesign --verify --strict` passes after extracting the zip, the zip contains `INSTALL.txt`, `LICENSE.txt`, `THIRD_PARTY_NOTICES.txt` and `SOURCE.txt` and no `._` files.
- `--test-pkg` built an unsigned installer for structure checks only: two component pkgs with `relocatable="false"` and install locations `/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/Components`, the license and welcome pages, VST3/AU choices, `hostArchitectures="arm64,x86_64"`, macOS 11+. It was **not installed**.
- **Not exercised** (needs the certificate and the notary profile): Developer ID signing, hardened runtime, `notarytool`, `stapler`, `spctl --assess` with the quarantine flag, `pkgutil --check-signature`. The signed path of `release-mac.sh` has only been syntax-checked (`bash -n`) and read through.
- `pkgbuild` and `productbuild` fail inside the Claude Code sandbox ("cannot write component property list"); run the release scripts with the sandbox disabled.

### v1.0.0 re-verification after the title/section-title UI change
- Dev build #58: 96/96 tests, pluginval VST3 + AU, auval pass. Universal Release rebuilt: all 96 tests pass on arm64 and on x86_64 (12,773,055 assertions), pluginval strictness 10 pass on both slices. Unsigned zip and structure-test pkg regenerated.
- Layout measured on offscreen renders against the mockups: JUNKYARD glyph centre y = 26.0 (preset bar centre 26); CODEC glyph 8.0-24.0 (6 px above, 6 px below the stripe), x = 19 + side bearing; ON text centre y = 16.0 = title glyph centre.
