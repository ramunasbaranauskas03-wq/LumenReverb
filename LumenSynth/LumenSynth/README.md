# Lumen Synth

EDM wavetable synthesizer by Lumen Audio (VST3 + AU, macOS + Windows), with an LED interface.

## Engine
- 2 wavetable oscillators, 9 band-limited tables × 16 morph frames (alias-free, about -90 dB)
- Warp modes: Sync, Bend, Squeeze, FM (B→A)
- Unison up to 16 voices per oscillator, with Detune, Blend, Width and Phase Random
- Sub oscillator (sine/tri/saw/square) and a noise generator with colour control
- Filter: zero-delay ladder LP24 with drive, plus SVF LP12/LP24/HP12/HP24/Band/Notch; key tracking and envelope amount
- 3 envelopes, 2 LFOs (8 shapes including Pump, tempo sync, retrigger) and an 8-slot mod matrix
- 4 macros
- Mono/legato/glide, pitch bend, velocity, sustain pedal, mod wheel and aftertouch
- FX: Distortion (Warm/Hard/Fold/Crush, 2x oversampled), OTT-style 3-band compressor, Chorus, tempo-synced ping-pong Delay, Reverb, tempo-locked Pump (sidechain), Master volume with soft clip

## Smart Design
- **GENERATE** builds a complete new patch for a style: Bass, Lead, Pluck, Pad, Keys or FX.
- **MUTATE** makes a variation of the current sound.
- **UNDO** steps back.

## Presets
34 factory presets, all loudness-matched to -14 LUFS.

Validated with pluginval at strictness 10.
