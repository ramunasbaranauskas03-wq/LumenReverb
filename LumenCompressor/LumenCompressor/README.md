# Lumen Compressor 2 — Professional Compressor (VST3 / AU)

![Lumen Compressor](screenshot.png)

Part of the Lumen series. This is a complete rewrite, built to the standard of FabFilter Pro-C 3.

## 10 compression styles
| Style | What it is | Use it for |
|---|---|---|
| **Clean** | Transparent peak VCA, exact curve | anything, limiting |
| **Glue** | SSL-style bus compressor with program-dependent release | drum bus, mix bus |
| **Punch** | FET (1176-style) feedback design, fast | drums, rap / lead vocal |
| **Opto** | LA-2A style: fast first half of the release, then a slow tail that lengthens the longer it compresses | vocals, bass |
| **Vari-Mu** | Fairchild style: the ratio grows with level | pads, master warmth |
| **Vocal** | Smooth and even | vocals, leads |
| **Master** | Transparent RMS with a wide knee | master bus |
| **Pump** | EDM side-chain pumping: snaps down and swells back | side-chain |
| **Upward** | Lifts the quiet parts and never lifts the noise floor | detail, sustain |
| **Smash** | 3-band upward + downward compression (OTT-style) | huge, dense sound |

## Controls
- **Dynamics:** Threshold, Ratio (1:1 to ∞:1), Knee, Attack (0.01–250 ms), Release (5 ms–3 s), **Hold**, **Range**, **Lookahead** (0–20 ms).
  - **AUTO REL:** program-dependent release.
  - **AUTO THR:** the threshold follows the input loudness, so you get the same compression on a quiet or a hot track.
- **Side-chain:**
  - Sources:
    - **Internal**
    - **External SC** (e.g. the kick)
    - **Tempo Sync:** pumps to the song tempo in 1/1 to 1/16 steps, including triplet and dotted, with an Offset. No kick needed.
  - Side-chain EQ (HP, LP and a bell) and **LISTEN**.
- **Stereo:** Stereo Link 0–100 % and channel modes Stereo, **Mid/Side**, Mid only, Side only, Left only and Right only.
- **Character:** Tube, Tape, Transformer, Diode and Bright.
  - **Drive** is auto-levelled, and it can sit before or after the compression.
- **Oversampling:** 1x, 2x, 4x or 8x, using linear-phase FIR filters.
  - The dry signal goes through the same oversampling as the wet one, so **Mix** (parallel compression) never comb-filters.
- **Output:** Makeup, **AUTO GAIN** (output as loud as the input, for an honest A/B), Mix, Output and **DELTA** (hear what is removed).
- **Display:** scrolling input / output / gain-reduction history, an interactive transfer curve (drag it to set the threshold), and IN / GR / OUT meters.
- **LEARN:** recognises the material (drums, bass, vocal, pad or full mix) and sets the style, timing and threshold.
- **PHASE ALIGN:** kick / bass phase alignment, carried over from v1.
- **A/B**, **COPY**, and **36 presets**: drums, kick, snare, room, bass, 808, side-chain pumping (tempo or kick), vocals, synths, mix bus, master and Smash.

## Tested
- **Static curve vs. theory:**

  | Style | Error |
  |---|---|
  | Clean (hard and soft knee) | 0.01 dB |
  | Master | 0.02 dB |
  | Punch | within 0.6 dB |
  | Opto | within 0.8 dB |

- **Ratio and range:**
  - ∞:1 holds the level at the threshold (−19.8 dB for a −20 dB threshold).
  - Range caps the reduction exactly.
  - Vari-Mu's ratio grows with level.
- **Timing:**
  - Attack 10 ms reaches 63 % of the reduction in 10 ms.
  - Release 100 ms recovers 63 % in about 110 ms.
  - Hold adds about 40–50 ms to the release (50 ms setting).
  - Auto release and Opto release slow down after long compression.
- **Latency and oversampling:**
  - Lookahead catches transients.
  - Reported latency matches the measured delay within 0.2 samples.
  - Oversampling is flat up to 20 kHz.
  - Mix 50 % has no comb filtering at 100 Hz, 5 kHz or 15 kHz.
- **Stereo and side-chain:** stereo link, Side-only, external side-chain, SC high-pass and Tempo Sync all checked.
- **Upward and Smash:**
  - Upward follows its curve exactly and never lifts the noise floor.
  - Smash narrows the loud-to-quiet gap from 28 dB to 15 dB.
- **AUTO THR:** gives identical gain reduction at −30 and −8 dB input.
- **Character:** all types add harmonics with less than 1.5 dB level change. 8x oversampling lowers aliasing by about 25 dB.
- **Auto gain:** within 0.1 dB.
- **Bass distortion:** low (−44 to −71 dB THD on a 50 Hz tone).
- **Robustness:** all presets checked, random automation of every parameter, block-size independence, state save / restore.
- **CPU:** about 1.4 % (1x), 2.9 % (2x), 4.5 % (4x) and 7.7 % (8x) of one core.
- pluginval at strictness level 10: **SUCCESS**. Strict GCC and Clang builds are clean.

Note: v2 uses new parameters, so Lumen Compressor v1 settings in old projects will not carry over (Phase Align settings do).
