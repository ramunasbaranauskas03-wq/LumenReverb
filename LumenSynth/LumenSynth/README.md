# Lumen Synth 2

EDM wavetable synthesizer by Lumen Audio (VST3 + AU, macOS + Windows), with an LED interface.

## Sound engine
- 3 wavetable oscillators (A, B, C), each with:
  - 12 band-limited factory tables
  - Your own wavetables: **LOAD** button or drag a WAV onto the display. Serum-format files (2048-sample frames, up to 256 frames) and any other audio are accepted.
  - Unison up to 16 voices, with Detune, Blend, Width and Phase.
  - 8 warp modes: Sync (band-limited), Bend, Squeeze, FM, Mirror, Asym, Quantize, Remap.
- Sub oscillator and noise generator.
- 2 filters, Serial or Parallel, each with 11 types: Ladder LP24, LP12, LP24, HP12, HP24, Band, Notch, Comb +, Comb -, Formant, Phaser.
- **HQ mode**: oscillators and filters run at 2x sample rate.
- 3 envelopes.
- 4 LFOs: 9 shapes including a **drawable Custom shape**; modes Free / Retrig / Envelope (one-shot); tempo sync.
- Modulation: drag any LFO / ENV / MACRO / performance badge onto a knob.
  - Coloured mod rings show the range and live movement.
  - Right-click a knob for amount presets or to remove a modulation.
  - 16-slot matrix with live source meters.
  - Modulation can also reach FX destinations, e.g. Macro → Reverb Mix for build-ups.
- 4 macros. Voices, legato, glide, pitch bend, velocity, mod wheel, aftertouch, sustain pedal.

## FX rack (reorderable with the arrows)
Hyper/Dimension, Distortion (Warm/Hard/Fold/Crush, 2x oversampled), Flanger, Phaser, Chorus, tempo-synced ping-pong Delay, Compressor (with gain-reduction meter), Reverb, 3-band EQ, OTT, tempo-locked Pump. Master volume with soft clip.

## Arp & chords
- Arpeggiator: Up, Down, Up/Down, Random, As Played, Chord; 1-4 octaves; gate; swing. Synced to the DAW grid.
- Chord memory: Major, Minor, Maj7, Min7, Sus2, Sus4, Power, Octaves, Add9, Min9.

## Presets
- 41 factory presets, loudness-matched to -14 LUFS.
- Preset browser with search, categories and favourites (star).
- **SAVE** stores your own presets (including imported wavetables and drawn LFOs) in `Documents/Lumen Audio/Lumen Synth/Presets`.

## Smart Design
- **GENERATE** builds a full patch for the chosen style.
- **MUTATE** makes a variation.
- **UNDO** steps back.

## Interface
Resizable window: drag the corner or use ZOOM (75-150%).

## Testing
- Validated with pluginval at strictness 10.
- DSP covered by automated tests: aliasing, filter stability, arp timing, state save/restore, presets.
