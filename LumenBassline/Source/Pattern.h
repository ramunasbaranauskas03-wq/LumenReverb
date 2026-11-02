#pragma once
#include <array>
#include <cstdint>
#include <cmath>

//==============================================================================
// Lumen Bassline — pattern data, scales, styles and progressions.
// Notes are stored as 7-note scale DEGREES (plus an optional chromatic offset),
// so key, scale, octave and chord progression changes are applied live at
// playback without destroying the groove.
//==============================================================================

namespace lb
{
constexpr int stepsPerBar = 16;
constexpr int maxBars     = 8;
constexpr int maxSteps    = stepsPerBar * maxBars;
constexpr int maxNotes    = 512;

struct Note
{
    int16_t step   = 0;     // 16th-note index from loop start
    int16_t len    = 1;     // length in 16ths
    int16_t degree = 0;     // scale degree, 7-note space (0 = root, 7 = octave up)
    int8_t  chroma = 0;     // chromatic offset in semitones (approach / dip notes)
    uint8_t vel    = 100;
    bool    slide  = false; // legato glide into the NEXT note
    bool    accent = false;
    bool    ghost  = false;
    bool    locked = false; // kept by Generate / Mutate
};

struct Pattern
{
    int numNotes = 0;
    int bars     = 4;
    std::array<Note, maxNotes> notes {};

    int  lengthSteps() const noexcept { return bars * stepsPerBar; }
    void clear() noexcept             { numNotes = 0; }
    bool add (const Note& n) noexcept
    {
        if (numNotes >= maxNotes) return false;
        notes[(size_t) numNotes++] = n;
        return true;
    }
    void remove (int index) noexcept
    {
        if (index < 0 || index >= numNotes) return;
        for (int i = index; i < numNotes - 1; ++i) notes[(size_t) i] = notes[(size_t) i + 1];
        --numNotes;
    }
    int noteAtStep (int step) const noexcept
    {
        for (int i = 0; i < numNotes; ++i) if (notes[(size_t) i].step == step) return i;
        return -1;
    }
    void sortByStep() noexcept
    {
        for (int i = 1; i < numNotes; ++i)
        {
            auto key = notes[(size_t) i];
            int j = i - 1;
            while (j >= 0 && notes[(size_t) j].step > key.step) { notes[(size_t) j + 1] = notes[(size_t) j]; --j; }
            notes[(size_t) j + 1] = key;
        }
    }
};

//==============================================================================
struct ScaleDef { const char* name; int size; int semis[7]; };

inline const ScaleDef& scale (int index)
{
    static const ScaleDef scales[] = {
        { "Minor",           7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Phrygian",        7, { 0, 1, 3, 5, 7, 8, 10 } },
        { "Dorian",          7, { 0, 2, 3, 5, 7, 9, 10 } },
        { "Harmonic Minor",  7, { 0, 2, 3, 5, 7, 8, 11 } },
        { "Major",           7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Mixolydian",      7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "Minor Pentatonic",5, { 0, 3, 5, 7, 10 } },
        { "Major Pentatonic",5, { 0, 2, 4, 7, 9 } },
        { "Phrygian Dominant",7,{ 0, 1, 4, 5, 7, 8, 10 } },
        { "Locrian",         7, { 0, 1, 3, 5, 6, 8, 10 } },
    };
    constexpr int n = (int) (sizeof (scales) / sizeof (scales[0]));
    return scales[(index % n + n) % n];
}
inline int numScales() { return 10; }

inline int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

// Degree (7-note space) -> semitones above root, honouring 5-note scales.
inline int degreeToSemis (int degree, int scaleIndex)
{
    const auto& s = scale (scaleIndex);
    const int oct = floorDiv (degree, 7);
    const int d7  = degree - oct * 7;
    int idx = (s.size == 7) ? d7 : (int) std::lround (d7 * s.size / 7.0);
    int extraOct = 0;
    if (idx >= s.size) { idx -= s.size; extraOct = 1; }
    return (oct + extraOct) * 12 + s.semis[idx];
}

//==============================================================================
// Progressions: root movement per bar, in scale degrees.
struct ProgressionDef { const char* name; int length; int degrees[8]; };

inline const ProgressionDef& progression (int index)
{
    static const ProgressionDef progs[] = {
        { "Static (i)",          1, { 0 } },
        { "i - VI",              2, { 0, 5 } },
        { "i - VII",             2, { 0, 6 } },
        { "i - iv",              2, { 0, 3 } },
        { "i - v",               2, { 0, 4 } },
        { "i - VI - III - VII",  4, { 0, 5, 2, 6 } },
        { "i - VII - VI - VII",  4, { 0, 6, 5, 6 } },
        { "i - iv - VI - v",     4, { 0, 3, 5, 4 } },
        { "i - i - VI - VII",    4, { 0, 0, 5, 6 } },
        { "i - III - VII - IV",  4, { 0, 2, 6, 3 } },
    };
    constexpr int n = (int) (sizeof (progs) / sizeof (progs[0]));
    return progs[(index % n + n) % n];
}
inline int numProgressions() { return 10; }

//==============================================================================
// Styles. stepProb = chance of a note on each 16th of a bar (before knobs).
// degW = weights for root, 2nd, 3rd, 4th, 5th, 6th, 7th, octave up, octave down.
struct StyleDef
{
    const char* name;
    float stepProb[16];
    float degW[9];
    float avgLen;     // preferred note length in 16ths
    float slide;      // base chance of glide into next note
    float accent;     // base chance of accent
    float repeat;     // chance to repeat the previous pitch (hypnotic-ness)
    float swing;      // built-in swing added on top of the knob
};

inline const StyleDef& style (int index)
{
    static const StyleDef styles[] = {
        { "Tech House Roller",   { .05f,.55f,.95f,.55f, .05f,.6f,.95f,.6f, .05f,.55f,.95f,.55f, .05f,.65f,.9f,.75f },
                                 { 10, .5f, 1.5f, 1, 3, .8f, 2, 2.5f, 1.5f }, 1.3f, .10f, .25f, .45f, .04f },
        { "Tech House Bounce",   { .1f,.2f,.9f,.2f, .1f,.5f,.6f,.7f, .1f,.2f,.9f,.25f, .1f,.6f,.5f,.8f },
                                 { 10, .3f, 1.2f, 1, 3.5f, .6f, 2.5f, 3, 1 }, 1.6f, .12f, .3f, .35f, .06f },
        { "Offbeat Pump",        { 0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,.35f },
                                 { 12, 0, .5f, .4f, 1.5f, .3f, .8f, 1.2f, .5f }, 1.8f, .02f, .15f, .7f, .0f },
        { "Bass House Stab",     { .15f,0,.8f,.4f, 0,.5f,.3f,.7f, .1f,0,.8f,.45f, 0,.55f,.35f,.8f },
                                 { 10, .6f, 1.2f, 1, 2.5f, .7f, 2, 3, .5f }, 1.2f, .25f, .45f, .3f, .03f },
        { "G-House Groove",      { .2f,.1f,.8f,.5f, .1f,.6f,.7f,.4f, .2f,.1f,.8f,.55f, .1f,.6f,.6f,.6f },
                                 { 10, .5f, 2, 1.2f, 3, .8f, 2.5f, 2, 1.5f }, 1.5f, .2f, .3f, .3f, .08f },
        { "Minimal Tick",        { 0,.4f,.6f,0, 0,.3f,.0f,.6f, 0,.45f,.5f,0, 0,.25f,.4f,.5f },
                                 { 12, .2f, .8f, .4f, 1.5f, .3f, 1, 1, 1 }, 1.0f, .05f, .3f, .6f, .07f },
        { "Deep House Walk",     { .8f,.1f,.5f,.3f, .2f,.3f,.7f,.2f, .6f,.2f,.5f,.3f, .2f,.35f,.6f,.4f },
                                 { 8, 1.2f, 2.5f, 2, 3, 1.5f, 2.5f, 2, 1 }, 2.2f, .15f, .2f, .15f, .10f },
        { "Classic House Octave",{ .1f,0,1,0, .1f,0,1,0, .1f,0,1,0, .1f,0,1,0 },
                                 { 6, 0, .3f, .3f, .8f, .2f, .5f, 6, 0 }, 1.6f, .05f, .2f, .0f, .06f },
        { "Melodic Techno Drive",{ .1f,.9f,.9f,.9f, .1f,.9f,.9f,.9f, .1f,.9f,.9f,.9f, .1f,.9f,.9f,.9f },
                                 { 14, .3f, 1.2f, .4f, 1.5f, .8f, 1, .8f, .3f }, 1.0f, .03f, .3f, .75f, .0f },
        { "Peak Techno Rumble",  { 0,.95f,.95f,.95f, 0,.95f,.95f,.95f, 0,.95f,.95f,.95f, 0,.95f,.95f,.95f },
                                 { 16, .1f, .5f, .2f, .8f, .2f, .5f, .3f, 0 }, 1.0f, .0f, .35f, .9f, .0f },
        { "Acid 303",            { .7f,.5f,.6f,.6f, .5f,.6f,.6f,.5f, .7f,.5f,.6f,.55f, .5f,.6f,.6f,.7f },
                                 { 8, .8f, 2.5f, 1.2f, 2.5f, 1.2f, 2.5f, 3.5f, .8f }, 1.2f, .35f, .45f, .25f, .03f },
        { "UKG Skippy",          { .7f,0,.2f,.6f, 0,.3f,.6f,0, .1f,.6f,0,.5f, .3f,0,.6f,.3f },
                                 { 10, .8f, 1.5f, 1, 2.5f, 1, 2, 2, 1.5f }, 1.8f, .25f, .25f, .25f, .14f },
        { "Funky Disco",         { .8f,0,.6f,.3f, .3f,0,.8f,.3f, .8f,0,.6f,.3f, .3f,0,.8f,.5f },
                                 { 8, .5f, 1.2f, 1, 2, .8f, 2, 5, .5f }, 1.2f, .05f, .3f, .1f, .08f },
        { "Progressive Gallop",  { .1f,0,.9f,.9f, .1f,0,.9f,.9f, .1f,0,.9f,.9f, .1f,0,.9f,.9f },
                                 { 14, .2f, 1, .5f, 1.5f, .6f, 1, .8f, .2f }, 1.0f, .03f, .25f, .7f, .0f },
        { "Afro House",          { .2f,0,.7f,.5f, 0,.6f,.3f,.6f, .2f,0,.7f,.5f, 0,.6f,.5f,.4f },
                                 { 10, .5f, 2, 1.5f, 3, .8f, 2, 1.5f, 1.5f }, 1.6f, .18f, .3f, .3f, .10f },
        { "Hypnotic Loop",       { .2f,.5f,.8f,.3f, .1f,.6f,.7f,.4f, .2f,.5f,.8f,.3f, .1f,.6f,.7f,.5f },
                                 { 20, 0, .4f, .2f, 1, 0, .4f, .8f, .2f }, 1.3f, .05f, .3f, .85f, .04f },
        { "Electro Rubber",      { .7f,.2f,.3f,.7f, 0,.6f,.2f,.5f, .4f,.6f,0,.6f, .2f,.4f,.7f,.3f },
                                 { 9, .6f, 2, 1, 2.5f, .8f, 2, 3, 1.5f }, 1.3f, .3f, .35f, .2f, .02f },
        { "Breakbeat Bass",      { .8f,0,.2f,.6f, 0,0,.4f,.3f, .2f,.5f,.6f,0, 0,.3f,.4f,.5f },
                                 { 9, .5f, 2, 1.2f, 2.5f, .8f, 2, 2, 1.5f }, 2.0f, .2f, .3f, .2f, .05f },
        { "Jackin' Chord Root",  { .3f,0,.9f,.2f, .1f,.2f,.8f,.4f, .3f,0,.9f,.2f, .1f,.3f,.8f,.6f },
                                 { 12, .3f, 1, .8f, 2.5f, .5f, 1.5f, 3, .8f }, 1.4f, .08f, .3f, .4f, .09f },
        { "Minimal Dub",         { .5f,0,0,.3f, 0,0,.5f,0, .2f,0,.4f,0, 0,.3f,0,.2f },
                                 { 12, .3f, 1, .6f, 2, .4f, 1.2f, .8f, 1.5f }, 3.5f, .3f, .15f, .5f, .06f },
    };
    constexpr int n = (int) (sizeof (styles) / sizeof (styles[0]));
    return styles[(index % n + n) % n];
}
inline int numStyles() { return 20; }

//==============================================================================
struct GenParams
{
    int   style = 0, bars = 4;
    int   range = 1;           // 0 tight, 1 normal, 2 wide
    float density = .5f, syncopation = .5f, kickAvoid = .8f;
    float octaveJump = .3f, glide = .3f, ghost = .2f, variation = .35f, accent = .3f;
    float humanize = .2f;
    bool  fill = true;
};

void generate (Pattern& out, const Pattern& lockedSource, const GenParams& p, uint32_t seed);
void mutate   (Pattern& pat, const GenParams& p, uint32_t seed, float amount);
} // namespace lb
