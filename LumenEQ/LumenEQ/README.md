# Lumen EQ 2 — Dynamic Linear-Phase EQ (VST3 / AU)

Part of the Lumen series. A complete rewrite: 24 bands, analog-matched filters, per-band dynamics with side-chain, a linear-phase mode, EQ Match, Unmask and 32 EDM presets.

## Filters
- **24 bands.** Double-click the graph to add a band. Drag to set frequency and gain. The mouse wheel sets Q. Alt-click deletes a band. Right-click opens the band menu.
- **8 types:** Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass and Tilt.
- **Slopes:** 6 / 12 / 18 / 24 / 36 / 48 / 72 / 96 dB/oct.
- **Analog-matched design:** no "cramping" near Nyquist. A 16 kHz bell at 48 kHz matches the analog curve. In precise mode the numerator is fitted by least squares; while a band is moving, a fast design is used.
- **Per-band channel:** Stereo, Mid, Side, Left or Right. For example, a Side low cut cleans the side channel below 100 Hz and leaves the mid untouched.
- **Click-free automation:** parameters glide logarithmically in 16-sample steps. A +18 dB jump produces no click.

## Dynamic bands
- **DYN RANGE** (±24 dB) and **THRESHOLD** per band. The curve on the graph moves live.
- **SIDE-CHAIN DETECT:** the band reacts to another track. For example, the bass dips at 60 Hz only when the kick hits.
- **ATTACK / RELEASE** are shared by all bands.

## Modes
- **Zero Latency:** IIR, about 1.2 % CPU with 12 bands.
- **Linear Phase:** a 2x2 FIR matrix, so L/R and M/S stay exact. Low / Medium / High quality. Medium has 4608 samples of latency at 48 kHz, which the DAW compensates. About 3 % CPU.

## Smart tools
- **EQ MATCH:** captures 8 s of audio and matches its tonal balance to a reference file or to the side-chain track, using up to 10 bands.
- **UNMASK:** finds where the track clashes with the side-chain (e.g. bass vs. kick) and creates dynamic cuts driven by the side-chain.
- **Collision zones:** red areas on the graph show where the track and the side-chain clash.
- **AUTO GAIN** keeps the loudness level matched for honest A/B comparisons. **GAIN SCALE** scales all band gains from −200 % to +200 % (negative = inverted).
- **A/B** comparison, **COPY**, band **SOLO**, and a PRE / POST / SC analyzer with a +4.5 dB/oct tilt.

## 32 EDM presets
Kick, Sub, Bass (Reese, 808, Duck Under Kick, Future Bass), Lead (Supersaw, Festival), Pluck, Pad, Chords, Arp, Keys, Vocals (Pop Air, Backing, De-Esser, Telephone), Snare, Clap, Hats (dynamic), Perc, Drum Bus, FX (Riser, Downlifter), Master (Linear) and Mix Bus.

## Tested
- DSP tests, all passed:
  - Bit-transparent with no bands.
  - Bell and shelf accuracy.
  - Low cut at 96 dB/oct.
  - Mid/Side separation.
  - Symmetric linear-phase impulse.
  - Dynamics, side-chain, glide, Auto Gain, EQ Match, Unmask and all 32 presets.
- pluginval at strictness level 10: **SUCCESS**.
- Strict GCC and Clang builds.

Note: v2 uses new parameters, so Lumen EQ v1 settings in old projects will not carry over.
