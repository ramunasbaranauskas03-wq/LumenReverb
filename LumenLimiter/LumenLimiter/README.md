# Lumen Limiter 2 — Mastering Maximizer (VST3 / AU)

![Lumen Limiter](screenshot.png)

Part of the Lumen series. This is a complete rewrite in the spirit of Slate Digital FG-X 2, and it addresses the problems reviewers point out in FG-X 2:
- **Inaccurate constant-gain monitoring:** Lumen matches levels by measured LUFS (within 0.05 LU).
- **No release control:** Lumen has Release and Lookahead.
- **Ceiling only down to −3 dB:** Lumen goes down to −12 dB.
- **Distortion on bass-heavy mixes:** Lumen has a Balance control for that.

## Signal chain
**GLUE COMP → TRANSIENTS → GAIN → COLOUR → CLIP → TRUE-PEAK LIMITER → DITHER**

- **Glue Comp:** Ratio, Threshold, Attack, Release, Auto Release, Side-chain High-Pass, Mid/Side, Mix, and an analog-style gain-reduction needle meter.
- **Transients:**
  - **LO PUNCH:** pushes the low-end transients (the kick) before the limiter.
  - **DETAIL:** pushes the high-frequency transients (hats, snare snap).
  - **TRANSIENT:** adds attack to (+) or softens (−) the whole mix.
  - The low band uses a ripple-free sliding-max detector, so it reacts to the kick and not to the bass waveform itself.
- **Colour:** Warm, Tape or Bright, with Drive. It is auto-levelled and oversampled.
- **Clip:** an oversampled soft clipper before the limiter (Clip and Clip Shape controls). It shaves the sharpest peaks so the limiter works less.
- **Limiter:** a two-stage true-peak limiter (8-phase detection, with a 128-tap second stage that guarantees the ceiling).
  - Modes: **Clear / Tight / Punchy / Loud**.
  - Controls: **Release**, **Lookahead**, **Stereo Link**.
  - **BALANCE:** skews the limiting towards the lows (keeps the highs open on bass-heavy mixes) or the highs.
  - **True Peak** can be switched on or off.
  - Ceiling from −12 to 0 dB.
- **Dither:** 24-bit, 16-bit, or 16-bit noise-shaped.

## Loudness and monitoring
- **BS.1770 / EBU R128 metering:** Short-term, Integrated, Momentary, True Peak, LRA, PLR.
- **Display:**
  - A loudness history graph showing output, limiter gain reduction, clip, comp gain reduction and short-term LUFS.
  - A **loudness histogram**.
  - The target line.
- **Targets:** Spotify, Apple Music, YouTube, Tidal / Amazon, SoundCloud, Beatport / DJ, EDM Club, Techno / House, Hip-Hop, Pop, EBU, or Custom.
  - A **SHIFT** readout tells you how far you are from the target.
  - **MATCH TARGET** sets GAIN automatically.
- **EQUAL LOUD:** the output is matched to the input by LUFS, so you hear only the change in sound.
- **Listen modes:** **ST / M / S / L / R / DIFF**.
- **A/B**, **COPY**, and **26 presets**: streaming, EDM, Big Room, Techno, House, Tech House, DnB, Dubstep, Trap, Future Bass, Melodic Techno, Hardstyle, Pop, Rock, Lo-Fi, Ambient, Podcast, and tools.

## Tested
- **True peak:**
  - Every mode × Balance (−100 / 0 / +100) × Gain (+6 / +18 dB) at a −1 dBTP ceiling: worst true peak −1.03 dBTP (16× reference measurement).
  - Square wave + 12 kHz tone pushed +24 dB: −0.36 dBTP at a −0.3 ceiling, with oversampling at 1x and at 8x.
  - With True Peak off, the sample peak stays at or below the ceiling.
- **Transparency:** below the ceiling the output is bit-identical to the input, delayed by the latency.
- **Latency:** the reported latency matches the measured delay.
- **Metering:** a 997 Hz sine at −20 dBFS reads −20.0 LUFS.
- **Glue comp and transients:**
  - Glue comp follows its curve.
  - Lo Punch adds +2.6 dB to the kick transient, and the tail 140–230 ms after the kick stays within 0.65 dB.
  - Detail adds +3.4 dB to the hats. Transient −100 softens the peaks.
- **Clip and Balance:**
  - Clip takes most of the work away from the limiter: limiter gain reduction drops from 4.1 dB to 0.3 dB.
  - Balance Lows keeps the highs 3.1 dB more open on a bass-heavy mix.
- **Monitoring:**
  - Equal Loud matches the input within 0.05 LU.
  - DIFF with nothing to do is silent.
  - MATCH TARGET reaches −8 LUFS (−7.8).
- **Robustness:**
  - All presets stay at or below their ceiling.
  - Random automation of every parameter never goes above 0 dBFS.
  - Results don't depend on the block size. State save / restore works.
  - 16-bit dither puts every sample on the 16-bit grid.
- **CPU (everything on):** about 7 % (1x) to 16 % (8x) of one core.
- pluginval at strictness level 10: **SUCCESS**. Strict GCC and Clang builds are clean.

Note: v2 uses new parameters, so Lumen Limiter v1 settings in old projects will not carry over.
