# Lumen Reference

Reference-track comparison plugin by Lumen Audio (VST3 + AU, macOS + Windows). Put it last on your master bus.

## Features
- **4 reference slots.** Load WAV / MP3 / FLAC / AIFF by clicking or dragging onto the window. Files are resampled to your project rate automatically.
- **MIX / REF switch.** Click-free A/B with **level match**: the reference is brought to your mix's integrated loudness, so you compare sound, not volume.
- **Loop region.** Drag the amber handles; the analysis follows the loop.
- **Playback modes:**
  - Free: plays with the DAW transport.
  - Sync: locked to DAW time.
  - Preview: plays while the DAW is stopped.
- **Tonal Balance.** 1/6-octave mix vs reference curves (loudness-independent, pink-weighted), plus a difference curve.
- **Band Balance.** Six bands from Sub to Air, showing how many dB you are above or below the reference.
- **Loudness & Dynamics.** Integrated and short-term LUFS (BS.1770), true peak, PLR (punch) and LRA, each with differences.
- **Stereo Width & Mono Safety.** Width and L/R correlation per band for mix and reference.
- **Smart Advice.** Plain-language suggestions ranked by importance.
- **Match EQ.** CAPTURE fits an 8-band EQ that moves your mix toward the reference, with an AMOUNT control.
- **Mono** check and reference **trim**.

**Important:** switch back to MIX (or bypass) before exporting.

Validated with pluginval at strictness 10. Analysis is covered by automated tests: flat pink-noise response, EBU loudness, correlation, level-matched A/B, and Match EQ convergence.
