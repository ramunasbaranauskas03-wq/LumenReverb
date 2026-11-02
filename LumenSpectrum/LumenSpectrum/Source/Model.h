#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cmath>

// Lumen Spectrum - shared model: instrument roles, colours, frequency bands, genre guides
namespace lumen::spec
{
// ---------------------------------------------------------------- analysis grid
constexpr int   kMaxSlots = 32;          // instances that can see each other
constexpr int   kPoints   = 480;         // log-spaced display points 20 Hz .. 20 kHz
constexpr float kFMin     = 20.0f, kFMax = 20000.0f;
constexpr int   kRing     = 1 << 15;     // per-instance analysis ring (32768 = max FFT)

inline float pointFreq (float i)  { return kFMin * std::pow (kFMax / kFMin, i / (float) (kPoints - 1)); }
inline float freqToPoint (float f) { return (float) (kPoints - 1) * std::log (f / kFMin) / std::log (kFMax / kFMin); }

// ---------------------------------------------------------------- instrument roles
struct Role
{
    const char* name;
    juce::uint32 argb;
    int priority;        // masking advice: lower number = keeps its space
    bool isBus;          // busses contain other tracks -> excluded from masking / ownership
    const char* keys;    // comma separated track-name keywords for auto detection
};

constexpr int kNumRoles = 14;
inline const std::array<Role, kNumRoles>& roles()
{
    static const std::array<Role, kNumRoles> r {{
        { "Kick",         0xffff6b6b, 0,  false, "kick,kck,bd" },
        { "Sub",          0xff7c83fd, 5,  false, "sub" },
        { "Bass",         0xffb48cff, 2,  false, "bass,808,reese,low end" },
        { "Clap / Snare", 0xffff9f6b, 4,  false, "clap,snare,snr" },
        { "Hats / Tops",  0xff6be4d3, 8,  false, "hat,hh,top,ride,crash,cymbal,shaker" },
        { "Percussion",   0xfff7d26b, 7,  false, "perc,tom,conga,bongo,rim" },
        { "Vocal",        0xffff7ec7, 1,  false, "vox,vocal,voc,acapella,chop" },
        { "Lead",         0xffffc46b, 3,  false, "lead,hook,pluck,arp" },
        { "Chords / Synth",0xffc8f06b,6,  false, "chord,synth,stab,keys,piano,organ" },
        { "Pad",          0xff6bb8ff, 9,  false, "pad,string,atmo,drone" },
        { "FX / Riser",   0xffa3a8b8, 10, false, "fx,riser,sweep,impact,noise,uplift,downlift,crash fx" },
        { "Drum Bus",     0xffff8f8f, 11, true,  "drum bus,drums,drum grp,beat,loop" },
        { "Music Bus",    0xff9be7ff, 12, true,  "music bus,music,inst bus,synths" },
        { "Master",       0xffeef0f6, 99, true,  "master,mixbus,mix bus,2bus,main" },
    }};
    return r;
}
constexpr int kRoleMaster = 13;
constexpr int kDefaultRole = 8;

// guess a role from a DAW track name (most specific first)
inline int guessRole (const juce::String& trackName)
{
    auto n = trackName.toLowerCase();
    static const int order[] { 13, 11, 12, 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    for (int r : order)
        for (auto& k : juce::StringArray::fromTokens (roles()[(size_t) r].keys, ",", ""))
            if (k.isNotEmpty() && n.contains (k)) return r;
    return -1;
}

// ---------------------------------------------------------------- colour palette (custom colours)
constexpr int kNumPalette = 12;
inline const std::array<std::pair<const char*, juce::uint32>, kNumPalette>& palette()
{
    static const std::array<std::pair<const char*, juce::uint32>, kNumPalette> p {{
        { "Coral", 0xffff6b6b }, { "Orange", 0xffff9f6b }, { "Amber", 0xffffc46b }, { "Lemon", 0xfff7e36b },
        { "Lime", 0xffc8f06b }, { "Mint", 0xff6bf0a8 }, { "Teal", 0xff6be4d3 }, { "Sky", 0xff6bb8ff },
        { "Indigo", 0xff7c83fd }, { "Violet", 0xffb48cff }, { "Pink", 0xffff7ec7 }, { "Silver", 0xffd6d9e2 },
    }};
    return p;
}

// ---------------------------------------------------------------- frequency regions (as in the band strip)
struct Region { const char* name; float lo, hi; juce::uint32 argb; };
constexpr int kNumRegions = 6;
inline const std::array<Region, kNumRegions>& regions()
{
    static const std::array<Region, kNumRegions> r {{
        { "Sub",       20.0f,   60.0f,    0xff7c83fd },
        { "Bass",      60.0f,   250.0f,   0xffa98bf5 },
        { "Low mids",  250.0f,  500.0f,   0xffeb8b8b },
        { "Mids",      500.0f,  2000.0f,  0xfff2b46b },
        { "High mids", 2000.0f, 6000.0f,  0xffc9e58a },
        { "Highs",     6000.0f, 20000.0f, 0xff7fdcc9 },
    }};
    return r;
}
inline int regionOf (float f)
{
    for (int i = 0; i < kNumRegions; ++i) if (f < regions()[(size_t) i].hi) return i;
    return kNumRegions - 1;
}
// colour along the spectrum: blends smoothly between neighbouring region colours
inline juce::Colour spectrumColour (float f)
{
    auto& R = regions();
    const float lf = std::log2 (juce::jlimit (kFMin, kFMax, f));
    auto centre = [&] (int i) { return 0.5f * (std::log2 (R[(size_t) i].lo) + std::log2 (R[(size_t) i].hi)); };
    if (lf <= centre (0)) return juce::Colour (R[0].argb);
    for (int i = 0; i < kNumRegions - 1; ++i)
    {
        const float c = centre (i), cn = centre (i + 1);
        if (lf <= cn)
        {
            const float t = (lf - c) / (cn - c);
            // keep each region colour flat through most of its width, blend near the borders
            const float s = juce::jlimit (0.0f, 1.0f, (t - 0.35f) / 0.3f);
            return juce::Colour (R[(size_t) i].argb).interpolatedWith (juce::Colour (R[(size_t) i + 1].argb), s);
        }
    }
    return juce::Colour (R.back().argb);
}

// ---------------------------------------------------------------- tonal-balance guides
// Band levels relative to the "Mids" band, measured with a 4.5 dB/oct slope (a balanced
// mix reads roughly flat). These are starting guides - use SNAP REF on a reference track
// for an exact target.
struct Genre { const char* name; float rel[kNumRegions]; };
constexpr int kNumGenres = 9;
inline const std::array<Genre, kNumGenres>& genres()
{
    static const std::array<Genre, kNumGenres> g {{
        { "Tech House",        { 3.0f, 5.0f, 1.0f, 0.0f,  0.0f, -1.0f } },
        { "House",             { 2.0f, 4.0f, 1.0f, 0.0f,  0.0f, -1.0f } },
        { "Melodic Techno",    { 4.0f, 4.0f, 0.0f, 0.0f, -1.0f, -2.0f } },
        { "Techno",            { 4.0f, 4.5f, 1.0f, 0.0f,  0.0f, -1.5f } },
        { "Festival EDM",      { 3.0f, 5.0f, 0.0f, 0.0f,  1.0f,  0.0f } },
        { "Drum & Bass",       { 5.0f, 3.0f, 0.0f, 0.0f,  0.0f,  0.0f } },
        { "Dubstep / Bass",    { 6.0f, 4.0f, 0.0f, 0.0f,  1.0f,  0.0f } },
        { "Afro House",        { 2.5f, 4.0f, 1.0f, 0.0f, -0.5f, -1.0f } },
        { "Pop",               { 1.0f, 3.0f, 1.0f, 0.0f,  0.0f, -1.0f } },
    }};
    return g;
}
constexpr float kTargetTolerance = 2.0f;

// ---------------------------------------------------------------- helpers
inline juce::String noteName (float f)
{
    if (f <= 0.0f) return {};
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
    const int n = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - (float) n) * 100.0f);
    juce::String s = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    if (cents != 0) s << " " << (cents > 0 ? "+" : "") << cents << "c";
    return s;
}
inline juce::String freqText (float f)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz";
    if (f >= 100.0f)  return juce::String (juce::roundToInt (f)) + " Hz";
    return juce::String (f, 1) + " Hz";
}
inline float powToDb (float p) { return 10.0f * std::log10 (p + 1.0e-14f); }
inline float dbToPow (float d) { return std::pow (10.0f, d * 0.1f); }
} // namespace lumen::spec
