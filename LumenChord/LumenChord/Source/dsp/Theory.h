#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace lumen::chord
{

inline const juce::StringArray& keyNames() { static const juce::StringArray k { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return k; }

struct Scale { const char* name; std::array<int, 7> iv; };
inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "Minor",            { 0, 2, 3, 5, 7, 8, 10 } },
        { "Major",            { 0, 2, 4, 5, 7, 9, 11 } },
        { "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic Minor",    { 0, 2, 3, 5, 7, 9, 11 } },
        { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian",         { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian",           { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 } },
        { "Phrygian Dominant",{ 0, 1, 4, 5, 7, 8, 10 } },
        { "Locrian",          { 0, 1, 3, 5, 6, 8, 10 } },
    };
    return s;
}

enum ChordType { Triad, Seventh, Ninth, Add9, Sus2, Sus4, Power, EdmStack, kChordTypes };
inline juce::StringArray chordTypeNames() { return { "Triad", "7th", "9th", "Add9", "Sus2", "Sus4", "Power (1-5-8)", "EDM Stack" }; }

// slot quality override
enum Quality { QAuto, QMaj, QMin, QDim, QAug, QSus2, QSus4, QDom7, QMaj7, QMin7, QMin9, QMaj9, kQualities };
inline juce::StringArray qualityNames() { return { "Auto (in key)", "Major", "Minor", "Dim", "Aug", "Sus2", "Sus4", "7", "Maj7", "m7", "m9", "Maj9" }; }

// semitone of scale degree d (any integer, wraps with octaves)
inline int degreeSemis (const Scale& s, int d)
{
    const int oct = (int) std::floor (d / 7.0);
    const int i = d - oct * 7;
    return s.iv[(size_t) i] + 12 * oct;
}

// chord intervals (semitones above the root) for a scale degree / quality / type
inline std::vector<int> chordIntervals (const Scale& s, int degree, int type, int quality)
{
    std::vector<int> v;
    if (quality != QAuto)
    {
        switch (quality)
        {
            case QMaj:  v = { 0, 4, 7 }; break;
            case QMin:  v = { 0, 3, 7 }; break;
            case QDim:  v = { 0, 3, 6 }; break;
            case QAug:  v = { 0, 4, 8 }; break;
            case QSus2: v = { 0, 2, 7 }; break;
            case QSus4: v = { 0, 5, 7 }; break;
            case QDom7: v = { 0, 4, 7, 10 }; break;
            case QMaj7: v = { 0, 4, 7, 11 }; break;
            case QMin7: v = { 0, 3, 7, 10 }; break;
            case QMin9: v = { 0, 3, 7, 10, 14 }; break;
            case QMaj9: v = { 0, 4, 7, 11, 14 }; break;
            default:    v = { 0, 4, 7 }; break;
        }
        if (type == Power) v = { 0, 7, 12 };
        return v;
    }
    const int r = degreeSemis (s, degree);
    auto st = [&] (int k) { return degreeSemis (s, degree + k) - r; };
    switch (type)
    {
        case Triad:   v = { 0, st (2), st (4) }; break;
        case Seventh: v = { 0, st (2), st (4), st (6) }; break;
        case Ninth:   v = { 0, st (2), st (4), st (6), st (8) }; break;
        case Add9:    v = { 0, st (2), st (4), st (8) }; break;
        case Sus2:    v = { 0, st (1), st (4) }; break;
        case Sus4:    v = { 0, st (3), st (4) }; break;
        case Power:   v = { 0, st (4), 12 }; break;
        case EdmStack: v = { 0, st (4), 12, 12 + st (2), 12 + st (4) }; break;   // wide festival voicing
        default:      v = { 0, st (2), st (4) }; break;
    }
    return v;
}

// chord name from root pitch class and the notes (any octaves)
inline const juce::StringArray& flatNames() { static const juce::StringArray k { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" }; return k; }
// spell with flats when the key signature is a flat one (relative major F, Bb, Eb, Ab, Db, Gb)
inline bool useFlats (int key, const std::array<int, 7>& iv)
{
    const int third = iv[2];
    const int relMajor = third == 3 ? (key + 3) % 12 : key;
    return relMajor == 5 || relMajor == 10 || relMajor == 3 || relMajor == 8 || relMajor == 1 || relMajor == 6;
}
inline juce::String chordName (int rootPc, const std::vector<int>& notes, bool flats = false)
{
    const auto& keyNames = [flats]() -> const juce::StringArray& { return flats ? flatNames() : lumen::chord::keyNames(); };
    bool has[12] {};
    for (int n : notes) has[((n - rootPc) % 12 + 12) % 12] = true;
    juce::String q;
    const bool M3 = has[4], m3 = has[3], P5 = has[7], b5 = has[6], s5 = has[8], m7 = has[10], M7 = has[11], n9 = has[2], n4 = has[5];
    if (M3 && s5 && ! P5) q = "aug";
    else if (m3 && b5 && ! P5) q = m7 ? "m7b5" : "dim";
    else if (m3) q = "m";
    else if (! M3 && n9 && P5) q = "sus2";
    else if (! M3 && n4 && P5) q = "sus4";
    else if (! M3 && ! m3 && P5) q = "5";
    juce::String ext;
    if (M7) ext = q == "m" ? "(maj7)" : "maj7";
    else if (m7 && q != "m7b5") ext = "7";
    if ((M3 || m3) && n9) ext = ext.isEmpty() ? "add9" : (ext == "7" ? "9" : ext == "maj7" ? "maj9" : ext + "9");
    if (q == "m" && ext == "9") return keyNames()[rootPc] + "m9";
    if (q == "m" && ext == "7") return keyNames()[rootPc] + "m7";
    return keyNames()[rootPc] + q + ext;
}

inline juce::String romanFor (const Scale& s, int degree, int shift, const std::vector<int>& iv)
{
    static const char* up[] { "I", "II", "III", "IV", "V", "VI", "VII" };
    const bool minor = std::find (iv.begin(), iv.end(), 3) != iv.end() && std::find (iv.begin(), iv.end(), 4) == iv.end();
    const bool dim = std::find (iv.begin(), iv.end(), 6) != iv.end() && std::find (iv.begin(), iv.end(), 7) == iv.end();
    juce::String r = up[((degree % 7) + 7) % 7];
    if (minor || dim) r = r.toLowerCase();
    if (dim) r << "o";
    (void) s;
    return (shift < 0 ? "b" : shift > 0 ? "#" : "") + r;
}

// snap a MIDI note to the nearest note of the scale; returns the scale degree (absolute, any octave)
inline int snapToDegree (const Scale& s, int key, int note)
{
    int best = 0, bestD = 1000;
    for (int d = -7 * 2; d < 7 * 13; ++d)
    {
        const int n = key + degreeSemis (s, d);
        const int dist = std::abs (n - note);
        if (dist < bestD || (dist == bestD && n < note)) { bestD = dist; best = d; }
    }
    return best;
}

} // namespace lumen::chord
