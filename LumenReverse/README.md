# Lumen Reverse

Reverse-swell / pre-verb generator for the Lumen series (VST3 · AU · Standalone).

## Workflow
1. **Capture** – Manual (record until STOP), Auto (starts on signal, stops on 0.6 s of silence) or Host Bar (records 1 beat–4 bars from the next bar while the DAW plays). Or drop any WAV/AIFF/FLAC/MP3 onto the display, or LOAD.
2. **Length** – 1/16 … 4 bars tempo-synced (follows project tempo and time signature, re-renders on tempo change) or free 0.1–16 s.
3. **Shape** – the swell re-renders in the background ~90 ms after you stop turning a knob.
4. **Play / place** – PLAY (with LOOP) to audition; MIDI note triggers (optional pitch tracking around C3); **Host Sync** lands the swell exactly on every 1/2/4/8/16-bar boundary; **DRAG TO DAW** drops a 24-bit WAV into the arrangement (also saved in Documents/Lumen Reverse/Exports).

## Engine
- 8-line modulated FDN reverb, Householder feedback, 4–6 stage input diffusion, damped loop, pre-delay up to 250 ms, decay 0.2–30 s.
- Characters: Room, Hall, Plate, Cathedral, Shimmer (octave-up pitch shifter inside the feedback loop), Bloom (long smeared diffusion).
- Swell modes: **Tail** (reverb of the phrase, flipped into the hit) and **Pre-Verb** (reverse → reverb → reverse, ghost build that flows through the phrase).
- Rise curve, tail fade, source fade/blend, log filter sweep across the swell, low/high cut.
- Motion: tempo-synced tremolo/gate (1/32–1/2 incl. triplets), 6 shapes, stereo phase offset, Follow (depth grows with the swell) – applied live and printed into exports.
- Output: M/S width, gain, dry-thru, normalize (−1 dBFS).
- 16 factory presets, A/B with copy, undo/redo, resizable UI, captured audio saved inside the DAW project.

## Build
Built by the repo workflow (Build Lumen Plugins) together with the other Lumen plugins.
Local: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`
(JUCE 8.0.4 is fetched automatically unless a `JUCE/` folder exists.)

## Ableton tips
- MIDI trigger: on a MIDI track set *MIDI To* → the audio track → *Lumen Reverse*.
- Host Sync: put it on the vocal/snare track, set Trigger = Host Sync, Cycle = 4 or 8 bars – the swell lands on every drop/phrase start.
