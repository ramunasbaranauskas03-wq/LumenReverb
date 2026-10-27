# Lumen Kick 2 — Kick Designer (VST3 / AU instrument)

![Lumen Kick](screenshot.png)

Part of the Lumen series. This is a complete rewrite, built in the spirit of Sonic Academy Kick 3 and with a stronger sample workflow.

## Three layers: TOP (click) · MID (body / punch) · LOW (sub)
Each layer can play a **synth**, a **sample**, or both at once.
- **LOAD KICK + SPLIT** (or drop a file on the waveform) loads a kick and splits it into TOP, MID and LOW with zero-phase crossovers. The three bands add up to the original: tested at −71 dB difference.
- **Swap a single layer** by loading or dropping a sample on its panel, for example another kick's top click. The other bands stay exactly the same.
- **◀ ▶** step through the samples in the same folder, so you can audition tops or subs quickly.
- **BAND** on (the default) plays only that layer's band of the sample. With it off, the layer plays the full sample.
- **Add the synth to a sample**, for example a clean tuned sub under a sampled top and body.
- Sample controls: Level, Pitch (±24 st), Start, Nudge (align the layers).
- Layer controls: phase invert (Ø), Solo, and an envelope (Attack, Decay, Curve).
- **The split can be moved** by dragging the lines on the spectrum or with the LOW / MID and MID / TOP knobs. The sample is re-split instantly.
- The plugin **detects the low end of a loaded kick** and shows it as a note, e.g. "G1 −24c".

## Synth
- **LOW (sub):** sine with a pitch sweep (Sweep and Speed) and Drive. The tail is tuned to the **root note**. With **Key: Sub**, the sub follows MIDI notes, so you get tuned kicks.
- **MID (body):** Freq, Sweep, Speed, **Wave** (sine → triangle → square) and Drive.
- **TOP (click):** Click, Noise, Snap (909-style) or Tick, with Tone and Length. The click is identical on every hit, just like a sample.

## Master
- EQ: Low shelf (70 Hz), Mid bell, High shelf (7 kHz).
- **Drive:** Tube, Hard Clip, Fold or Crush, 2x oversampled.
- **Clipper:** keeps the output loud and never above 0 dBFS.
- Output, Velocity, and Global Pitch.

## Workflow
- **PLAY** auditions the kick. MIDI notes trigger it.
- A fast retrigger chokes the previous hit with a 3 ms fade, so there are no clicks.
- **The waveform** shows each layer in colour, the sub's pitch curve, and a playhead.
- **Drag the waveform** straight into your DAW, or use **EXPORT WAV** (24-bit, saved in Documents / Lumen Kick).
- **Samples are saved inside the project.** A project reloaded on another computer sounds identical (−86 dB difference).
- **A/B**, **COPY**, and **21 synth presets:** Techno, Hard Techno, House 909, Deep House, Tech House, Big Room, Psytrance, Trance, Hardstyle, DnB, Dubstep, Trap 808, Phonk, Future Bass, Melodic Techno, Minimal, Afro House, UK Garage, Lo-Fi, and Sub Only.

## Tested
- **Synth:**
  - The default kick peaks at 0.65 (−3.8 dBFS).
  - The tail is tuned to F1 (44.0 Hz), and Key: Sub plays A1 at 55.5 Hz.
  - Velocity 0.5 gives −5.95 dB.
  - A fast retrigger produces no click or build-up.
- **Samples:**
  - The three bands rebuild the original (−71 dB difference).
  - Each band stays in its own range, and swapping a layer only changes that band.
  - Pitch +12 st exactly doubles the frequency.
  - The detected low end is correct.
  - Moving the split re-splits the sample, the reloaded project is identical, and EXPORT WAV works.
- **Robustness:** every preset stays at or below 0 dBFS and ends within 3 s. Random automation with notes and re-splits stays finite.
- **CPU:** 4.5 % of one core at 16 kicks per second with sample + synth + drive.
- pluginval at strictness level 10: **SUCCESS**. Strict GCC and Clang builds are clean.

Note: v2 uses new parameters, so Kick Designer v1 settings in old projects will not carry over. The v1 drum-loop splitter has been removed.
