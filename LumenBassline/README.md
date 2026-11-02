# Lumen Bassline Pro

MIDI bassline generator + preview bass synth (VST3 / AU), part of the Lumen series.

## Build
Built by `.github/workflows/build.yml` with the other Lumen plugins (Windows on every push;
macOS via Actions -> Build Lumen Plugins -> Run workflow -> tick macOS).

Install: Windows `Lumen Bassline.vst3` -> `C:\Program Files\Common Files\VST3`.
macOS `.vst3` -> `~/Library/Audio/Plug-Ins/VST3`, `.component` -> `~/Library/Audio/Plug-Ins/Components`
(if macOS blocks it: `xattr -cr <path to plugin>`).

## Use in Ableton
Load it on a MIDI track as an instrument. Press PLAY (or start Ableton), hit GENERATE,
then drag DRAG MIDI onto a clip slot. Turn SYNTH off to hear only the MIDI on your own bass synth
(route this track's MIDI to it), or just drag the clip onto your synth's track.

## Features
- 20 styles: Tech House Roller/Bounce, Offbeat Pump, Bass House Stab, G-House, Minimal Tick, Deep House Walk,
  Classic House Octave, Melodic Techno Drive, Peak Techno Rumble, Acid 303, UKG Skippy, Funky Disco,
  Progressive Gallop, Afro House, Hypnotic Loop, Electro Rubber, Breakbeat Bass, Jackin' Chord Root, Minimal Dub
- 10 scales, key, octave, 1/2/4/8 bars, Tight/Normal/Wide range
- Chord progressions (i–VI, i–VI–III–VII …) applied live, or FOLLOW MIDI: hold a note/chord on the input to move the root
- Phrase-aware generation (A / A' / A / B call-and-response) with automatic fills
- Groove: density, syncopation, kick avoid, swing, gate, humanize
- Melody: octave jumps, 303-style slides, ghost / pitch-dip notes, variation, accents
- LIVE RESHAPE: knobs reshape the current groove with the same seed
- MUTATE to evolve, history < >, seed display
- Editable piano roll: click add/delete, drag move, edge = length, shift-click lock (locked notes survive Generate),
  right-click slide / accent / ghost / octave
- Preview synth: 7 sounds (Sub Rumble, Grit Saw, Acid Squelch, Reese, Pluck, Organ, Rubber Square), filter + envelope,
  drive, sub, glide, 4x4 kick + hats with sidechain duck so you hear the bass in context
- MIDI out, DRAG MIDI to the DAW, SAVE .MID; pattern saved with the project
