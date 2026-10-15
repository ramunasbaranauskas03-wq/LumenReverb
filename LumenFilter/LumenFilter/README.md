# Lumen Filter

EDM multimode filter by Lumen Audio (VST3 + AU, macOS + Windows).

## Filter
- **11 types:** Ladder LP24, LP12, LP24, HP12, HP24, Band, Notch, Comb +, Comb -, Formant, Phaser.
- **Controls:** Cutoff, Resonance, Drive, Mix, Output.
- **Stereo Spread:** different cutoff on the left and right channels.
- **Fully transparent when wide open:** LP at the top of the range, HP at the bottom.
- **Oversampling:** 1x / 2x / 4x.

## RISE (build-up macro)
One knob for build-ups:
- **HP Riser:** a high-pass sweeps up.
- **LP Down:** a low-pass closes down.
- **Band Squeeze:** a "telephone" squeeze.

RISE also raises resonance and speeds up the LFO. Automate it over your 8 or 16 bars.

## Modulation
- **Tempo-synced LFO:** 8 shapes, including Pump and S&H, with stereo phase.
- **Envelope follower:** input or **sidechain**. Route your kick to the sidechain for a filter duck.

## Key Lock
Snaps the cutoff to notes (chromatic, major or minor, any root) so the resonance sings in key.

## Presets
14 factory presets: Wobble 1/8, Wobble Triplet, Pumping Filter, Build-Up HP Riser, Breakdown LP Down, Telephone Squeeze, Talking Formant, Phaser Sweep, Envelope Funk, Stereo Motion, Resonant Key Lock, Comb Metallic, Sidechain Duck Filter and more.

## Testing
- Validated with pluginval at strictness 10.
- DSP covered by automated tests: transparency, cutoff accuracy, tempo-locked LFO, sidechain duck, RISE, key lock, and stability at all settings.
