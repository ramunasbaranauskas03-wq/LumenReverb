#pragma once
#include "Theory.h"
#include <vector>
#include <cstdint>
#include <string>
#include <array>
#include <cmath>
#include <algorithm>

// =====================================================================================
// Lumen Melody / Bass - pattern generators
//   Notes are stored as scale steps (rel) relative to the chord root (or to the key in KEEP mode),
//   so a hook automatically follows any chord progression (a diatonic sequence, like real EDM hooks).
// =====================================================================================
namespace lumen::melody
{
using namespace lumen::chord;

struct Note
{
    int step = 0, len = 2, rel = 0; float vel = 0.9f; bool approach = false;
    bool operator== (const Note& o) const { return step == o.step && len == o.len && rel == o.rel && approach == o.approach; }
};
using Pattern = std::vector<Note>;

enum LeadStyle { BigRoomHook, ElectroLead, ProgPluck, TranceLead, FutureChop, TropicalLead, MelodicTechno, PopTopline, kLeadStyles };
enum BassStyle { Offbeat, Rolling, OctaveBounce, ElectroSync, Sustain, BigRoomPump, FutureGlide, WalkingHouse, Reese, kBassStyles };
enum Contour { Arch, Rise, Fall, Wave, Free };

inline juce::StringArray leadStyleNames() { return { "Big Room Hook", "Electro Lead", "Prog House Pluck", "Trance Lead", "Future Bass Chops", "Tropical Pluck", "Melodic Techno", "Pop Topline" }; }
inline juce::StringArray bassStyleNames() { return { "Offbeat (house / trance)", "Rolling 16ths (psy / techno)", "Octave Bounce (disco / electro)", "Electro Sync (dutch)", "Sustained Root", "Big Room Pump", "Future Bass Glide", "Walking House", "Reese (DnB / dark)" }; }
inline juce::StringArray contourNames()   { return { "Arch", "Rise", "Fall", "Wave", "Free" }; }

// tiny deterministic RNG (same seed -> same pattern on every machine)
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 0x9e3779b9u) { if (s == 0) s = 1; next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float) (next() & 0xffffff) / 16777216.0f; }
    int i (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
    bool chance (float p) { return f() < p; }
};

// ---------------------------------------------------------------------------------------------
// rhythm templates: 16 steps, 'x' onset, '-' hold, '.' rest
inline const std::vector<const char*>& leadRhythms (int style)
{
    static const std::vector<std::vector<const char*>> r {
        /* BigRoomHook  */ { "x-.x-.x-x-.x-.x-", "x-.x-.x-.x-.x-x-", "x.xx.x.x.xx.x.x.", "x--x--x-x--x--x-" },
        /* ElectroLead  */ { "x.x.xx.x.x.xx.x.", "x..x..x.x..x.x..", "xx.x.xx.x.xx.x.x", "x.xx.x..x.xx.x.." },
        /* ProgPluck    */ { "x.x.x.x.x.x.x.x.", "x..x..x...x..x..", "x.xx.x.xx.x.x.x.", "x..x.x..x..x.x.." },
        /* TranceLead   */ { "x-x-x-x-x-x-x-x-", "x--x--x-x--x--x-", "x-x-xx-xx-x-xx-x", "x--x--x---x--x--" },
        /* FutureChop   */ { "x-.x.x--x-.x.x.x", "x.xx..x.x.xx..x.", "x--x.x-.x--x.x..", "xx.x.x..xx.x.x.." },
        /* TropicalLead */ { "x..x..x...x.x...", "x.xx..x...x..x..", "x..x..x.x.x.....", "x..x..x...xx.x.." },
        /* MelodicTechno*/ { "x.x.x.x.x.x.x.x.", "x..x..x..x..x..x", "x.xx.xx.x.xx.xx.", "x...x.x...x.x..." },
        /* PopTopline   */ { "x-x-x---x-x-x---", "x.x.x-x---x.x.x-", "x-x-x-x-x---x---", "x.x.x.x-----x.x." },
    };
    return r[(size_t) std::clamp (style, 0, (int) r.size() - 1)];
}

struct LeadSettings
{
    int style = BigRoomHook, bars = 4, hookBars = 1, contour = Arch, range = 7;
    float density = 0.5f, catchy = 0.7f;
    uint32_t seed = 1;
};

// chordDeg(step) -> chord root degree at that step (0 everywhere in FOLLOW mode)
template <typename ChordFn>
inline Pattern generateLead (const LeadSettings& s, ChordFn chordDeg)
{
    Rng rng (s.seed * 7919u + (uint32_t) s.style * 131u);
    const int hookSteps = 16 * std::max (1, std::min (s.hookBars, s.bars));
    const int total = 16 * std::max (1, s.bars);
    // ---- rhythm of the hook
    std::vector<std::pair<int, int>> onsets;          // step, length
    const auto& tpl = leadRhythms (s.style);
    for (int b = 0; b < hookSteps / 16; ++b)
    {
        const char* t = tpl[(size_t) rng.i ((int) tpl.size())];
        std::string row (t);
        // density: add / remove onsets
        for (int k = 1; k < 16; ++k)
        {
            if (row[(size_t) k] == '.' && s.density > 0.5f && rng.chance ((s.density - 0.5f) * 1.2f)) row[(size_t) k] = 'x';
            else if (row[(size_t) k] == 'x' && k % 4 != 0 && s.density < 0.5f && rng.chance ((0.5f - s.density) * 1.4f)) row[(size_t) k] = '.';
        }
        for (int k = 0; k < 16; ++k)
            if (row[(size_t) k] == 'x')
            {
                int len = 1; while (k + len < 16 && row[(size_t) (k + len)] == '-') ++len;
                onsets.push_back ({ b * 16 + k, len });
            }
    }
    if (onsets.empty()) onsets.push_back ({ 0, 2 });
    // ---- pitches of the hook
    const int range = std::clamp (s.range, 3, 14), lo = -range / 3, hi = lo + range;
    auto target = [&] (float u)
    {
        const float mid = 0.5f * (float) (lo + hi), amp = 0.5f * (float) (hi - lo);
        switch (s.contour)
        {
            case Arch: return mid - amp * 0.5f + amp * std::sin (u * 3.14159f);
            case Rise: return (float) lo + (float) (hi - lo) * u;
            case Fall: return (float) hi - (float) (hi - lo) * u;
            case Wave: return mid + amp * 0.8f * std::sin (u * 6.28318f);
            default:   return mid;
        }
    };
    std::vector<int> rels; std::vector<int> used;
    int prev = 0, run = 0;
    for (size_t n = 0; n < onsets.size(); ++n)
    {
        const int st = onsets[n].first;
        const bool strong = st % 4 == 0;
        const int cd = chordDeg (st);
        const float u = (float) st / (float) hookSteps;
        std::vector<float> w; std::vector<int> cand;
        for (int r = lo; r <= hi; ++r)
        {
            const int rc = ((r - cd) % 7 + 7) % 7;          // position in the chord
            const bool ct = rc == 0 || rc == 2 || rc == 4;
            float sc = 0;
            sc += ct ? (strong ? 2.2f : 0.6f) : (strong ? -1.5f : 0.0f);
            const int d = std::abs (r - prev);
            if (n > 0)
            {
                // one repeat is a hook device, three in a row is a drone
                if (d == 0) sc += run == 0 ? 0.4f + 0.8f * s.catchy : run == 1 ? -0.8f : -4.0f;
                else sc += d <= 2 ? 1.4f : d <= 4 ? 0.7f : -0.6f;
            }
            sc -= 0.55f * std::abs ((float) r - target (u));
            // catchy hooks reuse a small set of notes (but at least four)
            if (used.size() >= 4 && std::find (used.begin(), used.end(), r) == used.end()) sc -= 1.2f * s.catchy;
            if (n == 0 && rc == 0) sc += 1.0f;
            cand.push_back (r); w.push_back (std::exp (sc * (1.0f + 1.0f * s.catchy)));
        }
        float sum = 0; for (float v : w) sum += v;
        float pick = rng.f() * sum; size_t k = 0;
        for (; k + 1 < w.size(); ++k) { pick -= w[k]; if (pick <= 0) break; }
        run = cand[k] == prev && n > 0 ? run + 1 : 0;
        prev = cand[k]; rels.push_back (prev);
        if (std::find (used.begin(), used.end(), prev) == used.end()) used.push_back (prev);
    }
    // ---- repeat the hook over the whole pattern; the last repeat becomes the "answer"
    Pattern p;
    const int reps = total / hookSteps;
    for (int rp = 0; rp < reps; ++rp)
    {
        const bool answer = reps >= 2 && rp == reps - 1;
        for (size_t n = 0; n < onsets.size(); ++n)
        {
            Note nt; nt.step = rp * hookSteps + onsets[n].first; nt.len = onsets[n].second;
            nt.rel = rels[n];
            const bool late = onsets[n].first >= hookSteps / 2;
            if (answer && late)
            {
                // answer phrase: new notes in the second half, ending on a stable tone
                const bool last = n + 1 == onsets.size();
                if (last) nt.rel = rng.chance (0.6f) ? 0 : (rng.chance (0.5f) ? 4 : 2);
                else if (rng.chance (0.75f - 0.35f * s.catchy)) nt.rel = std::clamp (nt.rel + (rng.chance (0.5f) ? 1 : -1) * (1 + rng.i (2)), lo, hi);
            }
            else if (rp > 0 && ! answer && rng.chance ((1.0f - s.catchy) * 0.25f))
                nt.rel = std::clamp (nt.rel + (rng.chance (0.5f) ? 1 : -1), lo, hi);
            nt.vel = (nt.step % 4 == 0 ? 1.0f : nt.step % 2 == 0 ? 0.85f : 0.75f) - 0.06f * rng.f();
            p.push_back (nt);
        }
    }
    return p;
}

// ---------------------------------------------------------------------------------------------
struct BassSettings { int style = Offbeat, bars = 4; float variation = 0.3f; bool approach = true; uint32_t seed = 1; };

inline Pattern bassBar (int style)
{
    // {step, len, rel}
    std::vector<std::array<int, 3>> v;
    switch (style)
    {
        case Offbeat:      v = { { 2, 2, 0 }, { 6, 2, 0 }, { 10, 2, 0 }, { 14, 2, 0 } }; break;
        case Rolling:      for (int k = 0; k < 16; ++k) if (k % 4 != 0) v.push_back ({ k, 1, 0 }); break;
        case OctaveBounce: for (int k = 0; k < 16; k += 2) v.push_back ({ k, 1, (k / 2) % 2 ? 7 : 0 }); break;
        case ElectroSync:  v = { { 0, 2, 0 }, { 3, 2, 0 }, { 6, 1, 7 }, { 8, 2, 0 }, { 11, 1, 4 }, { 13, 2, 7 } }; break;
        case Sustain:      v = { { 0, 16, 0 } }; break;
        case BigRoomPump:  v = { { 2, 1, 0 }, { 3, 1, 0 }, { 6, 1, 0 }, { 7, 1, 0 }, { 10, 1, 0 }, { 11, 1, 0 }, { 14, 1, 0 }, { 15, 1, 7 } }; break;
        case FutureGlide:  v = { { 0, 3, 0 }, { 3, 3, 0 }, { 6, 4, 7 }, { 10, 2, 4 }, { 12, 4, 0 } }; break;
        case WalkingHouse: v = { { 0, 3, 0 }, { 4, 3, 2 }, { 8, 3, 4 }, { 12, 3, 5 } }; break;
        case Reese:        v = { { 0, 7, 0 }, { 8, 6, 0 }, { 14, 2, -1 } }; break;
        default:           v = { { 2, 2, 0 }, { 6, 2, 0 }, { 10, 2, 0 }, { 14, 2, 0 } }; break;
    }
    Pattern p;
    for (auto& a : v) { Note n; n.step = a[0]; n.len = a[1]; n.rel = a[2]; p.push_back (n); }
    return p;
}

inline Pattern generateBass (const BassSettings& s)
{
    Rng rng (s.seed * 104729u + (uint32_t) s.style * 31u);
    Pattern p;
    const auto bar = bassBar (s.style);
    for (int b = 0; b < std::max (1, s.bars); ++b)
        for (size_t k = 0; k < bar.size(); ++k)
        {
            Note n = bar[k]; n.step += b * 16;
            // accents follow the groove: offbeats push, downbeats lighter in pump styles
            n.vel = (n.step % 4 == 2 ? 1.0f : n.step % 4 == 0 ? 0.92f : 0.8f) - 0.05f * rng.f();
            if (s.variation > 0 && k > 0 && rng.chance (s.variation * 0.35f))
            {
                const int r = rng.i (3);
                n.rel = r == 0 ? (n.rel == 7 ? 0 : 7) : r == 1 ? 4 : n.rel;    // octave flip / fifth
            }
            p.push_back (n);
        }
    // ghost notes and fills
    if (s.variation > 0.4f && s.style != Sustain && s.style != Reese)
        for (int b = 0; b < s.bars; ++b)
            if (rng.chance ((s.variation - 0.4f) * 1.2f))
            {
                const int st = b * 16 + 1 + 2 * rng.i (7);
                if (std::none_of (p.begin(), p.end(), [&] (const Note& n) { return st >= n.step && st < n.step + n.len; }))
                { Note g; g.step = st; g.len = 1; g.rel = rng.chance (0.5f) ? 7 : 0; g.vel = 0.6f; p.push_back (g); }
            }
    // approach note on the last 16th of each bar (sounds only when the chord changes)
    if (s.approach && s.style != Sustain)
        for (int b = 0; b < s.bars; ++b)
        {
            const int st = b * 16 + 15;
            p.erase (std::remove_if (p.begin(), p.end(), [&] (const Note& n) { return n.step == st; }), p.end());
            for (auto& n : p) if (n.step < st && n.step + n.len > st) n.len = st - n.step;
            Note a; a.step = st; a.len = 1; a.rel = 0; a.vel = 0.85f; a.approach = true; p.push_back (a);
        }
    std::sort (p.begin(), p.end(), [] (const Note& a, const Note& b) { return a.step < b.step; });
    return p;
}

// pattern <-> text (state)
inline juce::String patternToString (const Pattern& p)
{
    juce::StringArray a;
    for (auto& n : p) a.add (juce::String (n.step) + ":" + juce::String (n.len) + ":" + juce::String (n.rel) + ":" + juce::String (n.vel, 2) + ":" + juce::String ((int) n.approach));
    return a.joinIntoString (",");
}
inline Pattern patternFromString (const juce::String& s)
{
    Pattern p; juce::StringArray a; a.addTokens (s, ",", "");
    for (auto& t : a)
    {
        juce::StringArray f; f.addTokens (t, ":", "");
        if (f.size() < 4) continue;
        Note n; n.step = f[0].getIntValue(); n.len = std::max (1, f[1].getIntValue()); n.rel = f[2].getIntValue(); n.vel = f[3].getFloatValue(); n.approach = f.size() > 4 && f[4].getIntValue() != 0;
        p.push_back (n);
    }
    return p;
}

} // namespace lumen::melody
