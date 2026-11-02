# Lumen Synth XL

Wavetable sintezatorius Lumen serijai, sukurtas pagal Massive XL architektūrą: modulinis signalo kelias, Performer sekvenceriai, drag-and-drop moduliacija ir Mutator.

**v2:** 400 gamyklinių presetų (8 kategorijos po 50), Serum lygio FX grandinė (Hyper/Dimension, OTT, EQ, Bass Mono), analoginis drift, 36 wavetable'ai, presetų naršyklė su žanrų filtrais ir pavadinti makrai.

Formatai: VST3 ir Standalone (Windows), VST3, AU ir Standalone (macOS).

## Kaip įkelti į repo

1. Išarchyvuok ZIP į repo šaknį (`LumenReverb`). Turi atsirasti:
   - `LumenSynthXL/` (projektas)
   - `.github/workflows/lumen-synth-xl.yml` (build'as)
2. Padaryk commit ir push. GitHub Actions automatiškai sukurs Windows ir macOS versijas.
3. Atsisiųsk iš **Actions → Lumen Synth XL → Artifacts**: `LumenSynthXL-Windows` ir `LumenSynthXL-macOS`.

JUCE atsisiunčiamas automatiškai build'o metu, nieko papildomai diegti nereikia.

## Įdiegimas

**Windows:** nukopijuok `Lumen Synth XL.vst3` į `C:\Program Files\Common Files\VST3\`, tada Ableton'e Options → Preferences → Plug-Ins → Rescan.

**macOS:**
- `Lumen Synth XL.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
- `Lumen Synth XL.component` → `~/Library/Audio/Plug-Ins/Components/`
- Kadangi build'as nepasirašytas Apple sertifikatu, Terminal'e paleisk vieną kartą:
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Lumen Synth XL.vst3" ~/Library/Audio/Plug-Ins/Components/"Lumen Synth XL.component"`

## Kas viduje

**Garso šaltiniai**
- 2 wavetable osciliatoriai, 36 gamykliniai wavetable'ai (po 64 kadrus), band-limited mipmap'ai be aliasing
- Analog Drift: kiekvienas unison balsas turi savo derinimo nuokrypį ir lėtą „plaukimą“, filtras irgi šiek tiek gyvena
- 10 osciliatoriaus režimų: Standard, Bend +, Bend -, Mirror, PWM, Hard Sync, Formant, Jump, Quantize, Fold
- Wavetable importas (+ mygtukas): Serum tipo lentelės (2048 kadrai) arba bet koks semplas, kuris automatiškai supjaustomas į 64 kadrus
- Unison iki 8 balsų kiekvienam osciliatoriui (detune, stereo spread, blend), FM ir ring mod
- Sub osciliatorius ir triukšmas (White, Pink, Crackle, Metal, Vinyl)

**Signalo kelias**
- 2 filtrai, 11 modelių: LP12, LP24 ladder, Analog LP, HP12, HP24, Bandpass, Notch, Double Notch, Comb +/-, Formant
- Serial arba Parallel jungimas, kiekvienas šaltinis siunčiamas į A, B, A+B arba Direct
- Feedback kilpa į Filter A (Massive stiliaus)
- 2 Insert FX balso viduje: Drive, Tube, Wavefold, Rectify, Bitcrush, Downsample, Hard Clip; prieš arba po filtro
- Quality: High (2x oversampling), švarus drive, fold ir sync

**Moduliacija**
- 9 moduliatoriai, kiekvienas gali būti Envelope, LFO, Performer (16 žingsnių piešiamas sekvenceris) arba Random
- Sinchronizacija su DAW tempu, Retrigger, Bipolar
- 16 eilučių moduliacijos matrica, 8 makrai, velocity, key, aftertouch (ir polyphonic), mod wheel, pitch bend
- Animuoti moduliacijos žiedai ant rankenėlių: matosi, kiek ir kaip juda parametras

**Master FX** (eilė: Hyper > Distortion > OTT > Chorus > Phaser > Delay > Reverb > EQ > Tape > Output):
- Hyper/Dimension (Serum stiliaus platinimas), Distortion, OTT multiband (3 juostos, upward/downward), Chorus, Phaser, Delay, Reverb (stereo diffused FDN), EQ (low cut, low, mid, high), Tape Wobble
- Output: Stereo Width, Bass Mono (žemų dažnių mono iki pasirinkto dažnio), Soft Clip

**Darbo eiga:** presetų naršyklė (paspausk preseto pavadinimą), vartotojo presetai su kategorija, Mutator, Undo/Redo, keičiamo dydžio langas.

## Presetų biblioteka (400)

| Kategorija | Kiekis | Pavyzdžiai |
|---|---|---|
| Bass | 50 | Acid Line 303, Fidget Bass, Future Bwah, Classic Reese, Rolling Saw, Rubber Tech, 808 Glide |
| Lead | 50 | Festival Anthem, Big Room Lead, Sync Screamer, Afterhours Lead, Euphoric Supersaw, Hoover |
| Pad | 50 | Trance Supersaw Pad, Underground Drone, Pumping Pad, Glass Cathedral, String Ensemble |
| Pluck | 50 | House Pluck, Afterlife Pluck, Trance Pluck, Future Bass Pluck, Marimba, Kalimba |
| Keys | 50 | M1 Organ, House Piano, Rhodes Dream, Bell Keys, Vibraphone, Juno Keys |
| Chord | 50 | Deep House Stab, Detroit Stab, Big Room Chord, Rave Stab, Dub Chord, Afterlife Chord |
| Arp | 50 | Afterlife Arp, Trance Gate, Acid Sequence, Psy Roll, Polyrhythm 5, Bouncy House Seq |
| FX | 50 | Big Riser, Downlifter, Laser Zap, Siren, Impact Boom, Space Drone |

Žanrai: Electro House, Melodic Techno, House, Deep House, Tech House, Techno, Festival EDM, Progressive House, Future House, Bass House, Trance, Dubstep, Future Bass.

Visi presetai suvienodinti pagal garsumą (LUFS) pagal kategoriją, pikai iki -1 dBFS. Kiekvienas presetas turi 4 pavadintus makrus (pvz. Cutoff, Grit, Space, OTT).

Arp presetai groja seką patys: laikyk vieną natą, ir sintezatorius pats groja melodiją pagal DAW tempą.

## Kaip moduliuoti

- **Tempk** šaltinio mygtuką (ENV 1, LFO 3, M1, VELOCITY ir t. t.) iš apatinės juostos ant bet kurios rankenėlės.
- **Shift + tempimas** ant rankenėlės keičia moduliacijos gylį.
- **Dešinys pelės mygtukas** ant rankenėlės: invert, gylis, pašalinti.
- Paspaudus ENV/LFO/PERF mygtuką atsidaro to moduliatoriaus redaktorius MOD puslapyje.
- MATRIX puslapyje matosi ir redaguojamos visos 16 jungčių.

## Presetai

Vartotojo presetai saugomi: `Dokumentai/Lumen Audio/Lumen Synth XL/Presets/` (`.lxlpreset`). Importuoti wavetable'ai įrašomi kartu su presetu ir projektu.

## Presetų generatorius (`tools/`)

Gamykliniai presetai aprašyti Python skripte `tools/gen_presets.py`, kuris sugeneruoja `Source/FactoryPresets.cpp`. Norint pridėti ar pakeisti presetus, pataisyk skriptą ir paleisk `python3 tools/gen_presets.py tools/gains.json`. `tools/loudness.py` perskaičiuoja garsumą iš testinio renderio rezultatų.

## Vietinis build'as (nebūtinas)

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
