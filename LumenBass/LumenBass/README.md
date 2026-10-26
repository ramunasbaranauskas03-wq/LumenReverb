# Lumen Bass 2 — Multiband Bass Processor (VST3 / AU)

![Lumen Bass](screenshot.png)

Part of the Lumen series. A complete rewrite. The bass is split into three phase-coherent bands (Linkwitz-Riley, 24 dB/oct), and each band gets its own processing, like Neural DSP Parallax, Bassforge or MaxxBass.

## SUB (below the split, 40–250 Hz)
- **MONO SUB:** makes everything below the split mono, so the low end is tight and club / vinyl safe. Measured side rejection: −45 dB at 30 Hz, −28 dB at 50 Hz. Side content at 3 kHz is untouched.
- **TIGHT:** a glue compressor for the sub, so every note hits the same level. A 12 dB difference between notes becomes 4.7 dB. Distortion stays below −80 dB.
- **SUB GEN:** a sine oscillator that is phase-locked to the bass. **Unison** strengthens a weak sub (+4.8 dB). **Octave Down** adds a sub one octave lower.
- **PHONE + CHARACTER:** exact 2nd / 3rd / 4th harmonics of the sub, so the bass can be heard on phones and laptops. The harmonics follow the bass level exactly. Character goes from Warm to Edgy.

## BODY (the growl) and AIR (the fizz)
- **DRIVE**, with 6 types:
  - **Tube:** warm, even harmonics.
  - **Tape:** soft.
  - **Fuzz:** aggressive.
  - **Fold:** neuro-style growl.
  - **Rectify:** adds the octave above.
  - **Crush:** bit and sample-rate reduction.
- Drive is **level-independent**: it sounds the same on a quiet or a loud bass.
- Drive is **auto-levelled**: you hear the colour, not just a louder sound. The level change stays within ±2 dB.
- **TONE** (dark / bright tilt), **DRIVE MIX** (parallel blend), **SQUASH** (upward + downward OTT-style compression), **WIDTH** (0–200 %) and **LEVEL**.
- **Oversampling:** 1x, 2x, 4x (the default) or 8x. At 8x, aliasing from Fuzz at 4.1 kHz drops from −23 dB to −100 dB.

## Global
- **KICK DUCK:** route the kick to the side-chain input and the sub ducks under it, with adjustable **RELEASE**. **DUCK BODY** ducks the body band as well.
- **COMPARE:** plays the original at the **same loudness** as the processed sound, for an honest before/after.
- **DELTA:** you hear only what Lumen Bass adds or removes.
- **MIX** (phase-coherent dry/wet), **INPUT**, **OUTPUT**, and **SOFT CLIP** (oversampled; the output never exceeds 0 dBFS).
- **LEARN:** listens to 4 s of bass, finds its note and sets the split, Tight, Phone and Sub Gen.
- **Tuner:** shows the bass note and the kick note (from the side-chain), and tells you whether they are in key.
- Analyser with the three band zones and draggable split lines, a waveform view (in vs out), and meters for low-end mono correlation and phone loudness.
- **30 EDM presets:** Sub, 808 (Trap, Drill, Phonk), Reese DnB, Neuro, Dubstep, Riddim, Future Bass, Tech House, Bass House, Big Room, Melodic Techno, Deep House, Psytrance, UK Garage, Hardstyle, Synthwave, Lo-Fi and mixing tools.

## Tested
- Neutral preset: flat magnitude (0.00 dB), and Neutral + DELTA gives silence.
- The default sound clearly changes the bass: the difference signal sits at −6.6 dB, while the overall level stays close.
- Mono, Tight, Sub Gen, Phone, Drive auto-level, aliasing, Squash, Width, Kick Duck, Compare, Tuner and LEARN all checked.
- Random automation of every parameter: all output samples are finite and stay below 0 dBFS.
- All 30 presets checked. The result does not depend on block size. State save / restore works.
- CPU on a 55 Hz bass: 1.5 % (1x), 3.5 % (2x), 6.8 % (4x), 11.9 % (8x) of one core.
- pluginval at strictness level 10: **SUCCESS**. Strict GCC and Clang builds are clean.

Note: v2 uses new parameters, so Lumen Bass v1 settings in old projects will not carry over. Phase Align now lives only in Lumen Compressor.
