#pragma once
#include "DrumVoices.h"
#include <vector>
#include <string>
#include <functional>
#include <cstring>

// =====================================================================================
// Lumen Drums - step sequencer + genre groove generator
//   10 tracks x up to 64 steps (1/16), each track its own length (polymeter); per step velocity,
//   probability and ratchet; swing; 8 patterns (A-H); deterministic generator per genre.
// =====================================================================================
namespace lumen::drums
{
constexpr int kMaxSteps = 64, kPatterns = 8;

struct Step { uint8_t vel = 0, prob = 100, ratchet = 1; };
struct Track { std::array<Step, kMaxSteps> s {}; int length = 16; };
struct Pattern { std::array<Track, kPads> tr {}; };

// GM-style note map so exported MIDI drops straight onto any drum rack
inline int padNote (int pad) { static const int n[kPads] { 36, 39, 38, 42, 46, 51, 37, 56, 45, 49 }; return n[std::clamp (pad, 0, kPads - 1)]; }
inline int padForNote (int note)
{
    for (int p = 0; p < kPads; ++p) if (padNote (p) == note) return p;
    switch (note) { case 40: return RSnare; case 44: return RCHat; case 41: case 43: case 47: case 48: case 50: return RTom; case 57: return RFx; default: return -1; }
}
constexpr int kPatternNoteBase = 24;            // C0..G0 (C3 = 60) select patterns A-H

struct XRng
{
    uint32_t s;
    explicit XRng (uint32_t seed) : s (seed * 2654435761u + 0x9e3779b9u) { if (s == 0) s = 1; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float) (next() & 0xffffff) / 16777216.0f; }
    bool chance (float p) { return f() < p; }
    int i (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
};

// ------------------------------------------------------------------------------------
// genres: per track a 16-step template + kit (models / tune / decay / tone) + swing + tempo hint
//   X accent  x hit  g ghost (always)   o optional hit  p optional ghost   - nothing
struct Genre
{
    const char* name; int bpm; float swing;
    const char* t[kPads];
    int model[kPads]; float tune[kPads], decay[kPads], tone[kPads];
    int len[kPads];
};

inline const std::vector<Genre>& genres()
{
    //                    KICK                 CLAP                 SNARE                CL HAT               OP HAT               RIDE                 PERC 1               PERC 2               TOM                  FX
    static const std::vector<Genre> g {
        { "House", 124, 0.12f,
          { "X---X---X---X---", "----X-------X---", "-----------p--p-", "gpgpgpgpgpgpgpgp", "--x---x---x---x-", "----------------", "---o--o----o-o--", "-o------o---o---", "----------------", "X---------------" },
          { 0, 0, 0, 1, 1, 0, 0, 3, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .5f, .5f, .4f, .45f, .5f, .5f, .5f, .4f, .5f, .6f }, { 0, 0, 0, .1f, 0, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Tech House", 126, 0.08f,
          { "X---X---X---X---", "----X-------X---", "-p------p-----p-", "gpgxgpgxgpgxgpgx", "--x---x---x---x-", "----------------", "--o-o--o-o--o-o-", "o----o----o-----", "----------------", "X---------------" },
          { 0, 3, 2, 1, 1, 0, 2, 4, 0, 0 }, { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .45f, .4f, .4f, .4f, .45f, .5f, .5f, .4f, .5f, .6f }, { 0, 0, 0, .15f, .05f, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Big Room", 128, 0.0f,
          { "X---X---X---X---", "----X-------X---", "----X-------X---", "--x---x---x---x-", "--o---o---o---o-", "--o---o---o---o-", "----------------", "----------------", "----------------", "X---------------" },
          { 2, 2, 4, 1, 1, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .5f, .55f, .55f, .4f, .5f, .5f, .5f, .5f, .5f, .7f }, { 0, 0, 0, .15f, .1f, .05f, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Electro House", 128, 0.0f,
          { "X---X---X---X---", "----X-------X---", "----X------pX--p", "xgxgxgxgxgxgxgxg", "--x---x---x---x-", "----------------", "---o---o-o----o-", "----------------", "----------------", "X---------------" },
          { 2, 2, 4, 2, 2, 0, 2, 0, 0, 0 }, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .45f, .5f, .5f, .35f, .45f, .5f, .5f, .5f, .5f, .7f }, { 0, .05f, 0, .2f, .1f, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Future House", 125, 0.05f,
          { "X---X---X---X---", "----X-------X---", "-------p---p----", "--x-p-x---x-p-x-", "--o-------o-----", "----------------", "-o--o--o--o-o-o-", "----------------", "----------------", "X---------------" },
          { 0, 0, 1, 2, 2, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .45f, .5f, .45f, .35f, .4f, .5f, .5f, .5f, .5f, .6f }, { 0, 0, 0, .1f, 0, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Bass House", 126, 0.06f,
          { "X---X---X---X---", "----X-------X---", "----X-------X--p", "gpxpgpxpgpxpgpxp", "--x---x---x---x-", "----------------", "---o----o--o----", "--o---o---o---o-", "----------------", "X---------------" },
          { 3, 3, 0, 1, 1, 0, 2, 3, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .45f, .45f, .4f, .4f, .45f, .5f, .5f, .4f, .5f, .6f }, { 0, 0, 0, .15f, .05f, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Techno", 132, 0.0f,
          { "X---X---X---X---", "----o-------o---", "----------------", "--x---x---x---x-", "--o---o---o---o-", "x-x-x-x-x-x-x-x-", "o--o--o--o--", "--o--o----o--o--", "----------------", "X---------------" },
          { 3, 3, 2, 1, 1, 0, 2, 0, 0, 3 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .55f, .4f, .4f, .35f, .45f, .4f, .45f, .4f, .5f, .5f }, { 0, -.1f, 0, .1f, 0, .1f, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 12, 16, 16, 64 } },
        { "Trance", 138, 0.0f,
          { "X---X---X---X---", "----X-------X---", "--------------pp", "gggggggggggggggg", "--x---x---x---x-", "----------------", "----------------", "----------------", "----------------", "X---------------" },
          { 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .45f, .5f, .4f, .35f, .45f, .5f, .5f, .5f, .5f, .7f }, { 0, 0, 0, .2f, .1f, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Progressive House", 126, 0.04f,
          { "X---X---X---X---", "----X-------X---", "--------------p-", "gpxpgpxpgpxpgpxp", "--x---x---x---x-", "----------------", "---o-----o--o---", "----------------", "----------------", "X---------------" },
          { 0, 0, 0, 3, 1, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .5f, .55f, .45f, .4f, .45f, .5f, .5f, .5f, .5f, .7f }, { 0, 0, 0, .1f, 0, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Melbourne Bounce", 128, 0.0f,
          { "X---X---X---X---", "----X-------X---", "----x-------x---", "--x---x---x---x-", "----------------", "----------------", "--x---x---x---x-", "----------------", "----------------", "X---------------" },
          { 2, 3, 0, 2, 2, 0, 6, 0, 0, 2 }, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .4f, .4f, .4f, .35f, .4f, .5f, .4f, .5f, .5f, .5f }, { 0, 0, 0, .2f, 0, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Hardstyle", 150, 0.0f,
          { "X---X---X---X---", "----X-------X---", "----------------", "--x---x---x---x-", "--o---o---o---o-", "----------------", "----------------", "----------------", "----------------", "X---------------" },
          { 4, 2, 4, 1, 1, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .5f, .5f, .5f, .4f, .45f, .5f, .5f, .5f, .5f, .7f }, { 0, 0, 0, .15f, .05f, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Drum & Bass", 174, 0.0f,
          { "X-------o-X-----", "----------------", "----X-------X---", "x-g-x-g-x-g-x-g-", "------o-------o-", "----------------", "---p-----p---p--", "----------------", "----------------", "X---------------" },
          { 3, 0, 0, 1, 1, 0, 2, 0, 0, 0 }, { 2, 0, 3, 0, 0, 0, 0, 0, 0, 0 }, { .3f, .5f, .45f, .35f, .4f, .5f, .5f, .5f, .5f, .7f }, { 0, 0, .1f, .2f, .1f, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Dubstep", 140, 0.0f,
          { "X-----o-----o---X-----o---------", "--------X---------------X-------", "--------X---------------X-------", "x-g-x-g-x-g-x-g-x-g-x-g-x-g-x-g-", "--------------o---------------o-", "----------------", "------p---------------p---------", "----------------", "----------------", "X---------------" },
          { 0, 2, 4, 1, 1, 0, 2, 0, 0, 0 }, { -1, 0, -2, 0, 0, 0, 0, 0, 0, 0 }, { .45f, .5f, .55f, .35f, .4f, .5f, .5f, .5f, .5f, .7f }, { 0, 0, 0, .15f, .05f, 0, 0, 0, 0, 0 },
          { 32, 32, 32, 32, 32, 16, 32, 16, 16, 64 } },
        { "Trap", 140, 0.0f,
          { "X------o--X-----X-----o---o-----", "--------X---------------X-------", "--------X---------------X-------", "xgxgxgxgxgxgxgxgxgxgxgxgxgxgxgxg", "------------------------------o-", "----------------", "-------------p------------------", "----------------", "----------------", "X---------------" },
          { 6, 4, 3, 2, 2, 0, 2, 0, 0, 0 }, { -2, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .7f, .4f, .45f, .3f, .4f, .5f, .5f, .5f, .5f, .7f }, { 0, 0, 0, .25f, .1f, 0, 0, 0, 0, 0 },
          { 32, 32, 32, 32, 32, 16, 32, 16, 16, 64 } },
        { "UK Garage", 132, 0.5f,
          { "X-------X--o----", "----X-------X---", "----x-------x---", "--x-px--x-p-x-p-", "------o---------", "----------------", "-p-----p-o------", "----------------", "----------------", "X---------------" },
          { 1, 1, 1, 1, 1, 0, 2, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .4f, .45f, .4f, .4f, .4f, .5f, .5f, .5f, .5f, .6f }, { 0, 0, 0, .1f, 0, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
        { "Afro House", 122, 0.18f,
          { "X---X---X---X---", "--------X-------", "---o--o----o--o-", "p-x-p-x-p-x-p-x-", "----------------", "----------------", "-o-o--o--o-o--o-", "o--o--o---o--o--", "------------o---", "X---------------" },
          { 5, 1, 2, 3, 3, 0, 0, 1, 1, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { .4f, .45f, .4f, .4f, .4f, .5f, .5f, .45f, .5f, .6f }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
          { 16, 16, 16, 16, 16, 16, 16, 16, 16, 64 } },
    };
    return g;
}

struct GenSettings { int genre = 0; float complexity = 0.5f; uint32_t seed = 1; };

inline Track trackFromTemplate (const char* tpl, int len, float complexity, XRng& rng, int pad)
{
    Track tr; tr.length = std::clamp (len, 1, kMaxSteps);
    const int tl = (int) std::strlen (tpl);
    const float pOpt = 0.15f + 0.75f * complexity;
    for (int k = 0; k < tr.length; ++k)
    {
        const char c = tl > 0 ? tpl[k % tl] : '-';
        auto& s = tr.s[(size_t) k];
        auto vary = [&] (int v) { return (uint8_t) std::clamp (v + (int) ((rng.f() - 0.5f) * 14.0f), 1, 127); };
        switch (c)
        {
            case 'X': s.vel = 127; break;
            case 'x': s.vel = vary (104); break;
            case 'g': s.vel = vary (66); break;
            case 'o': if (rng.chance (pOpt)) s.vel = vary (98); break;
            case 'p': if (rng.chance (pOpt * 0.8f)) s.vel = vary (60); break;
            default: break;
        }
        // FX (crash) only on the first downbeat of its long cycle
        if (pad == RFx && k > 0) s.vel = 0;
        // busier grooves: extra ghost hits on hats / percs, ratchets on hats
        if (s.vel == 0 && (pad == RCHat || pad == RPerc1 || pad == RPerc2) && rng.chance (0.12f * complexity * complexity)) { s.vel = vary (55); s.prob = 70; }
        if (s.vel > 0 && pad == RCHat && complexity > 0.55f && rng.chance ((complexity - 0.55f) * 0.35f)) s.ratchet = (uint8_t) (rng.chance (0.7f) ? 2 : 3);
    }
    return tr;
}

inline Pattern generatePattern (const GenSettings& gs)
{
    const auto& G = genres()[(size_t) std::clamp (gs.genre, 0, (int) genres().size() - 1)];
    XRng rng (gs.seed * 7919u + (uint32_t) gs.genre * 104729u);
    Pattern p;
    for (int t = 0; t < kPads; ++t) p.tr[(size_t) t] = trackFromTemplate (G.t[t], G.len[t], gs.complexity, rng, t);
    return p;
}

// last beat (4 steps) becomes a fill: snare / clap roll with rising velocity, tom pickups, kick drop-out
inline void addFill (Pattern& p, uint32_t seed)
{
    XRng rng (seed * 31u + 17u);
    const int L = p.tr[RSnare].length;
    const int start = std::max (0, L - 4);
    for (int k = start; k < L; ++k)
    {
        auto& s = p.tr[RSnare].s[(size_t) k];
        s.vel = (uint8_t) (70 + 15 * (k - start)); s.prob = 100;
        s.ratchet = (uint8_t) (k == L - 1 ? 4 : k == L - 2 ? 2 : 1);
    }
    if (rng.chance (0.6f)) { auto& t = p.tr[RTom]; t.length = L; t.s[(size_t) (L - 3)].vel = 100; t.s[(size_t) (L - 1)].vel = 115; }
    if (rng.chance (0.5f)) p.tr[RKick].s[(size_t) (L - 1)].vel = 0;
    auto& oh = p.tr[ROHat]; oh.s[(size_t) std::min (oh.length - 1, L - 1)].vel = 0;
}

// mutate the optional hits, keep the anchors (4/4 kick, backbeat) in place
inline void vary (Pattern& p, const GenSettings& gs)
{
    const auto& G = genres()[(size_t) std::clamp (gs.genre, 0, (int) genres().size() - 1)];
    XRng rng (gs.seed * 131u + 7u);
    for (int t = 0; t < kPads; ++t)
    {
        auto& tr = p.tr[(size_t) t];
        const int tl = (int) std::strlen (G.t[t]);
        for (int k = 0; k < tr.length; ++k)
        {
            const char c = tl > 0 ? G.t[t][k % tl] : '-';
            if (c == 'X' || c == 'x') continue;
            auto& s = tr.s[(size_t) k];
            if (rng.chance (0.18f + 0.2f * gs.complexity))
            {
                if (s.vel > 0) s.vel = 0;
                else if ((c == 'o' || c == 'p' || t == RCHat || t == RPerc1 || t == RPerc2) && t != RFx) s.vel = (uint8_t) (c == 'p' ? 60 : 92);
            }
        }
    }
}

// song sections into A..F: intro, groove, drop, break, build (4-bar roll), fill
inline std::array<Pattern, kPatterns> generateSong (const GenSettings& gs)
{
    std::array<Pattern, kPatterns> s {};
    GenSettings g = gs;
    s[1] = generatePattern (g);                                               // B groove
    s[0] = s[1];                                                              // A intro: kick + hats only
    for (int t : { RClap, RSnare, RPerc1, RPerc2, RTom, RRide }) for (auto& st : s[0].tr[(size_t) t].s) st.vel = 0;
    g.complexity = std::min (1.0f, gs.complexity + 0.3f); g.seed = gs.seed + 1;
    s[2] = generatePattern (g);                                               // C drop: busier
    s[3] = s[1];                                                              // D break: no kick, keep percs
    for (auto& st : s[3].tr[RKick].s) st.vel = 0;
    // E build: 64-step accelerating snare + clap roll, kick on quarters, silence on the last beat
    {
        auto& b = s[4];
        for (int t = 0; t < kPads; ++t) b.tr[(size_t) t].length = 64;
        for (int k = 0; k < 60; k += 4) b.tr[RKick].s[(size_t) k].vel = 120;
        for (int k = 0; k < 60; ++k)
        {
            const int bar = k / 16;
            const int every = bar == 0 ? 4 : bar == 1 ? 2 : 1;
            if (k % every != 0) continue;
            auto& st = b.tr[RSnare].s[(size_t) k];
            st.vel = (uint8_t) std::clamp (55 + k, 1, 127);
            if (bar == 3) st.ratchet = (uint8_t) (k >= 56 ? 2 : 1);
        }
        for (int k = 0; k < 64; k += 2) b.tr[RCHat].s[(size_t) k].vel = (uint8_t) (60 + k / 2);
        b.tr[RFx].s[0].vel = 0;
    }
    s[5] = s[1]; addFill (s[5], gs.seed);                                    // F groove + fill
    s[6] = s[2]; addFill (s[6], gs.seed + 3);                                  // G drop + fill
    s[7] = s[1]; vary (s[7], { gs.genre, gs.complexity, gs.seed + 9 });      // H variation
    return s;
}

// ------------------------------------------------------------------------------------
// Player: sample-accurate, block-size independent (beat anchor), swing on odd 16ths,
// ratchets inside the step, probability rolled per hit.
struct Hit { int offset; int pad; float vel; };

class Player
{
public:
    void reset() { anchorOk = false; nextStep = -1; pend.clear(); now = 0; }
    void setSwing (float s) { swing = std::clamp (s, 0.0f, 1.0f); }

    // returns hits for this block (offsets relative to block start)
    void process (const Pattern& pat, int n, double beat, double bps, bool playing, std::vector<Hit>& hits, const std::function<bool (int)>& padMuted)
    {
        hits.clear();
        if (! playing) { anchorOk = false; nextStep = -1; pend.clear(); now += n; return; }
        const double predicted = anchorBeat + (double) (now - anchorS) * anchorBps;
        if (! anchorOk || std::abs (bps - anchorBps) > 1e-12 || std::abs (beat - predicted) > 1e-4)
        {
            const bool jump = anchorOk && std::abs (beat - predicted) > 1e-4;
            anchorBeat = beat; anchorS = now; anchorBps = bps; anchorOk = true;
            if (jump || nextStep < 0) { pend.clear(); nextStep = firstStepAtOrAfter (beat); }
        }
        for (int i = 0; i < n; ++i)
        {
            const double b = anchorBeat + (double) (now - anchorS) * anchorBps;
            // events land on the nearest sample (rounding, not truncation, so float error never adds a sample)
            const double bh = b + 0.5 * anchorBps;
            while (nextStep >= 0 && bh >= stepBeat (nextStep)) { fire (pat, nextStep, padMuted); ++nextStep; }
            for (size_t k = 0; k < pend.size();)
                if (bh >= pend[k].beat) { hits.push_back ({ i, pend[k].pad, pend[k].vel }); pend[k] = pend.back(); pend.pop_back(); }
                else ++k;
            ++now;
        }
    }
    int64_t currentStep() const { return nextStep - 1; }

private:
    struct Pend { double beat; int pad; float vel; };
    double stepBeat (int64_t k) const { return (double) k * 0.25 + ((k & 1) ? 0.25 * swing / 3.0 : 0.0); }
    int64_t firstStepAtOrAfter (double b) const { int64_t k = (int64_t) std::floor (b * 4.0) - 1; while (stepBeat (k) < b - 0.5 * anchorBps) ++k; return k; }

    void fire (const Pattern& pat, int64_t k, const std::function<bool (int)>& padMuted)
    {
        const double b0 = stepBeat (k);
        for (int t = 0; t < kPads; ++t)
        {
            const auto& tr = pat.tr[(size_t) t];
            const int L = std::max (1, tr.length);
            const auto& s = tr.s[(size_t) (((k % L) + L) % L)];
            if (s.vel == 0 || padMuted (t)) continue;
            if (s.prob < 100 && (float) (rng.next() % 100) >= (float) s.prob) continue;
            const int r = std::clamp ((int) s.ratchet, 1, 4);
            for (int j = 0; j < r; ++j)
            {
                const float v = (float) s.vel / 127.0f * (j == 0 ? 1.0f : 0.85f);
                pend.push_back ({ b0 + 0.25 * j / r, t, v });
            }
        }
    }

    std::vector<Pend> pend;
    float swing = 0;
    XRng rng { 99 };
    int64_t now = 0, nextStep = -1;
    double anchorBeat = 0, anchorBps = 0; int64_t anchorS = 0; bool anchorOk = false;
};

// pattern <-> text (state)
inline juce::String patternToString (const Pattern& p)
{
    juce::String s;
    for (int t = 0; t < kPads; ++t)
    {
        const auto& tr = p.tr[(size_t) t];
        s << tr.length << ":";
        for (int k = 0; k < tr.length; ++k) { const auto& st = tr.s[(size_t) k]; s << juce::String::toHexString ((int) st.vel).paddedLeft ('0', 2) << juce::String::toHexString ((int) st.prob).paddedLeft ('0', 2) << (int) st.ratchet; }
        s << ";";
    }
    return s;
}
inline Pattern patternFromString (const juce::String& str)
{
    Pattern p;
    juce::StringArray tracks; tracks.addTokens (str, ";", "");
    for (int t = 0; t < kPads && t < tracks.size(); ++t)
    {
        auto& tr = p.tr[(size_t) t];
        tr.length = std::clamp (tracks[t].upToFirstOccurrenceOf (":", false, false).getIntValue(), 1, kMaxSteps);
        const auto d = tracks[t].fromFirstOccurrenceOf (":", false, false);
        for (int k = 0; k < tr.length && (k + 1) * 5 <= d.length(); ++k)
        {
            tr.s[(size_t) k].vel = (uint8_t) d.substring (k * 5, k * 5 + 2).getHexValue32();
            tr.s[(size_t) k].prob = (uint8_t) std::clamp (d.substring (k * 5 + 2, k * 5 + 4).getHexValue32(), 0, 100);
            tr.s[(size_t) k].ratchet = (uint8_t) std::clamp (d.substring (k * 5 + 4, k * 5 + 5).getIntValue(), 1, 4);
        }
    }
    return p;
}

} // namespace lumen::drums
