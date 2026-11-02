#include "Pattern.h"
#include <algorithm>
#include <vector>

namespace lb
{
namespace
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed ? seed : 0x9E3779B9u) { for (int i = 0; i < 4; ++i) next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float    uni()  { return (float) (next() & 0xFFFFFF) / (float) 0x1000000; }
    bool     chance (float p) { return uni() < p; }
    int      range (int lo, int hi) { return lo + (int) (next() % (uint32_t) (hi - lo + 1)); }
};

using Bar = std::vector<Note>;

inline bool isKick (int s)   { return (s % 4) == 0; }
inline bool is16thOff (int s){ return (s % 2) == 1; }

int pickDegree (Rng& r, const StyleDef& st, const GenParams& p, int prevDeg)
{
    if (prevDeg != INT32_MIN && r.chance (st.repeat * (1.0f - p.variation * 0.4f)))
        return prevDeg;

    float w[9];
    std::copy (st.degW, st.degW + 9, w);
    w[7] *= 0.3f + p.octaveJump * 2.4f;          // octave up
    w[8] *= 0.4f + p.octaveJump * 1.2f;          // octave down

    if (p.range == 0)      { w[1] *= .3f; w[3] *= .5f; w[5] *= .3f; w[8] = 0; w[7] *= .6f; }
    else if (p.range == 2) { for (int i = 1; i < 7; ++i) w[i] *= 1.5f; w[7] *= 1.3f; }

    float total = 0; for (float x : w) total += x;
    float pick = r.uni() * total;
    int idx = 0;
    for (; idx < 8; ++idx) { pick -= w[idx]; if (pick <= 0) break; }

    if (idx == 7) // octave up — sometimes the 5th or 7th above it in wide range
        return 7 + ((p.range == 2 && r.chance (.25f)) ? (r.chance (.5f) ? 4 : 2) : 0);
    if (idx == 8)
        return r.chance (.7f) ? -7 : -3; // octave down, or the 5th below
    return idx;
}

Bar makeMotif (Rng& r, const StyleDef& st, const GenParams& p)
{
    Bar bar;
    const float dens = 0.35f + p.density * 1.3f;

    bool on[16] {};
    for (int s = 0; s < 16; ++s)
    {
        float prob = st.stepProb[s] * dens;
        if (is16thOff (s)) prob *= 0.45f + p.syncopation * 1.1f;
        else if (! isKick (s)) prob *= 0.9f + (1.0f - p.syncopation) * 0.2f;
        if (isKick (s)) prob *= (1.0f - p.kickAvoid);
        on[s] = r.chance (std::clamp (prob, 0.0f, 0.98f));
    }

    int count = 0; for (bool b : on) count += b ? 1 : 0;
    // Guarantee a playable groove: at least 3 notes, preferring the style's strongest steps.
    while (count < 3)
    {
        int best = -1; float bestP = -1;
        for (int s = 0; s < 16; ++s)
            if (! on[s])
            {
                float pr = st.stepProb[s] * (isKick (s) ? (1.0f - p.kickAvoid) + .05f : 1.0f) + r.uni() * .15f;
                if (pr > bestP) { bestP = pr; best = s; }
            }
        if (best < 0) break;
        on[best] = true; ++count;
    }

    int prevDeg = INT32_MIN;
    bool first = true;
    for (int s = 0; s < 16; ++s)
    {
        if (! on[s]) continue;
        Note n;
        n.step = (int16_t) s;
        int deg = first && r.chance (.75f) ? 0 : pickDegree (r, st, p, prevDeg);
        first = false;
        n.degree = (int16_t) deg;
        prevDeg = deg;

        int next = 16;
        for (int t = s + 1; t < 16; ++t) if (on[t]) { next = t; break; }
        const int maxLen = std::max (1, next - s);
        float want = st.avgLen * (0.7f + r.uni() * 0.8f);
        n.len = (int16_t) std::clamp ((int) std::lround (want), 1, maxLen);

        const float slideP = st.slide * (0.2f + p.glide * 2.2f);
        n.slide  = (next - s) == n.len && next < 16 && r.chance (std::min (slideP, .9f));
        n.accent = r.chance (std::min (st.accent * 0.6f + p.accent * 0.6f, .95f))
                   || (s % 4 == 2 && r.chance (p.accent * .5f));
        n.vel    = (uint8_t) (n.accent ? 118 : 96);
        bar.push_back (n);
    }

    // Ghost notes / pitch dips: quiet chromatic approach notes into the next hit.
    if (p.ghost > 0.01f)
    {
        Bar ghosts;
        for (size_t i = 0; i < bar.size(); ++i)
        {
            const auto& cur = bar[i];
            const int gapStart = cur.step + cur.len;
            const int nextStep = (i + 1 < bar.size()) ? bar[i + 1].step : 16;
            if (nextStep - gapStart >= 1 && nextStep < 16 && r.chance (p.ghost * .55f))
            {
                Note g;
                g.step   = (int16_t) (nextStep - 1);
                g.len    = 1;
                g.degree = bar[i + 1].degree;
                g.chroma = (int8_t) (r.chance (.6f) ? -1 : (r.chance (.5f) ? -12 : 1));
                g.ghost  = true;
                g.vel    = (uint8_t) (52 + r.range (0, 14));
                if (g.step > cur.step) ghosts.push_back (g);
            }
        }
        bar.insert (bar.end(), ghosts.begin(), ghosts.end());
        std::sort (bar.begin(), bar.end(), [] (const Note& a, const Note& b) { return a.step < b.step; });
    }
    return bar;
}

Bar vary (Rng& r, const Bar& src, const StyleDef& st, const GenParams& p, float amount)
{
    Bar out;
    for (size_t i = 0; i < src.size(); ++i)
    {
        Note n = src[i];
        if (i > 0 && ! n.ghost && r.chance (amount * .5f))
        {
            const int choice = r.range (0, 3);
            if (choice == 0)      n.degree = (int16_t) pickDegree (r, st, p, INT32_MIN);
            else if (choice == 1) n.degree = (int16_t) (n.degree + (r.chance (.5f) ? 7 : -7));
            else if (choice == 2) n.degree = (int16_t) (n.degree + r.range (-2, 2));
            else if (r.chance (.5f)) continue; // drop the note
        }
        if (r.chance (amount * .25f)) n.accent = ! n.accent;
        out.push_back (n);
    }
    // occasionally add a new syncopated hit in a free 16th
    const int adds = (int) (amount * 3.0f * (0.5f + p.density));
    for (int a = 0; a < adds; ++a)
    {
        const int s = r.range (1, 15);
        if (isKick (s) && r.chance (p.kickAvoid)) continue;
        bool taken = false;
        for (auto& n : out) if (s >= n.step && s < n.step + n.len) { taken = true; break; }
        if (taken || ! r.chance (amount)) continue;
        Note n; n.step = (int16_t) s; n.len = 1;
        n.degree = (int16_t) pickDegree (r, st, p, INT32_MIN);
        n.vel = 96;
        out.push_back (n);
    }
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.step < b.step; });
    if (out.empty() && ! src.empty()) out.push_back (src.front());
    return out;
}

void addFill (Rng& r, Bar& bar, const GenParams& p, bool big)
{
    const int start = big ? 8 : 12;
    bar.erase (std::remove_if (bar.begin(), bar.end(), [start] (const Note& n) { return n.step >= start && ! n.locked; }),
               bar.end());
    for (auto& n : bar) n.len = (int16_t) std::min<int> (n.len, std::max (1, start - n.step));

    static const int runs[4][8] = {
        { 0, 2, 4, 7, 0, 2, 4, 7 },     // up through the triad
        { 7, 6, 4, 2, 7, 6, 4, 2 },     // octave walk down
        { 0, 0, 7, 0, 0, 7, 0, 7 },     // octave stutter
        { 0, 1, 2, 4, 5, 6, 7, 9 },     // scale climb
    };
    const auto& run = runs[r.range (0, 3)];
    int k = 0;
    for (int s = start; s < 16; ++s)
    {
        const bool hit = (s % 2 == 1) || r.chance (.35f + p.density * .5f);
        if (! hit) continue;
        Note n; n.step = (int16_t) s; n.len = 1;
        n.degree = (int16_t) run[k++ % 8];
        n.accent = (s == 15) || r.chance (.2f);
        n.vel = (uint8_t) (n.accent ? 120 : 100);
        n.slide = r.chance (p.glide * .4f) && s < 15;
        bar.push_back (n);
    }
}

void humanizeAndFinish (Rng& r, Pattern& out, const GenParams& p)
{
    for (int i = 0; i < out.numNotes; ++i)
    {
        auto& n = out.notes[(size_t) i];
        if (n.locked) continue;
        const int jitter = (int) std::lround ((r.uni() * 2.0f - 1.0f) * p.humanize * 14.0f);
        n.vel = (uint8_t) std::clamp ((int) n.vel + jitter, 30, 127);
    }
    out.sortByStep();
    // no overlaps except slides (which are rendered legato)
    for (int i = 0; i < out.numNotes; ++i)
    {
        auto& n = out.notes[(size_t) i];
        const int nextStep = (i + 1 < out.numNotes) ? out.notes[(size_t) i + 1].step : out.lengthSteps();
        n.len = (int16_t) std::clamp<int> (n.len, 1, std::max (1, nextStep - n.step));
        if (n.slide && (n.step + n.len != nextStep || i + 1 >= out.numNotes)) n.slide = false;
    }
}
} // namespace

//==============================================================================
void generate (Pattern& out, const Pattern& lockedSource, const GenParams& p, uint32_t seed)
{
    Rng r (seed);
    const auto& st = style (p.style);
    const int bars = std::clamp (p.bars, 1, maxBars);

    const Bar A  = makeMotif (r, st, p);
    const Bar A2 = vary (r, A, st, p, p.variation * .5f);
    Bar B = vary (r, A, st, p, std::min (1.0f, p.variation + .15f));
    Bar C = vary (r, A2, st, p, std::min (1.0f, p.variation + .25f));
    if (p.fill) { addFill (r, B, p, false); addFill (r, C, p, true); }

    // Phrase forms: call / response so a 4- or 8-bar loop still feels written.
    std::vector<const Bar*> form;
    switch (bars)
    {
        case 1:  form = { p.fill ? &B : &A }; break;
        case 2:  form = { &A, p.fill ? &B : &A2 }; break;
        case 4:  form = { &A, &A2, &A, &B }; break;
        default: form = { &A, &A2, &A, &B, &A, &A2, &A, &C }; break;
    }

    out.clear();
    out.bars = bars;
    for (int b = 0; b < bars; ++b)
        for (auto n : *form[(size_t) (b % (int) form.size())])
        {
            n.step = (int16_t) (n.step + b * stepsPerBar);
            n.locked = false;
            out.add (n);
        }

    // Locked notes survive: remove anything colliding with them, then put them back.
    for (int i = 0; i < lockedSource.numNotes; ++i)
    {
        const auto& L = lockedSource.notes[(size_t) i];
        if (! L.locked || L.step >= out.lengthSteps()) continue;
        for (int j = out.numNotes - 1; j >= 0; --j)
        {
            const auto& n = out.notes[(size_t) j];
            if (n.step >= L.step && n.step < L.step + L.len) out.remove (j);
            else if (n.step < L.step && n.step + n.len > L.step) out.notes[(size_t) j].len = (int16_t) (L.step - n.step);
        }
        out.add (L);
    }
    humanizeAndFinish (r, out, p);
}

void mutate (Pattern& pat, const GenParams& p, uint32_t seed, float amount)
{
    Rng r (seed);
    const auto& st = style (p.style);
    Pattern src = pat;
    pat.clear();
    pat.bars = src.bars;

    for (int i = 0; i < src.numNotes; ++i)
    {
        Note n = src.notes[(size_t) i];
        if (! n.locked && r.chance (amount))
        {
            switch (r.range (0, 5))
            {
                case 0: n.degree = (int16_t) pickDegree (r, st, p, INT32_MIN); break;
                case 1: n.degree = (int16_t) (n.degree + (r.chance (.5f) ? 7 : -7)); break;
                case 2: n.step = (int16_t) std::clamp (n.step + (r.chance (.5f) ? 1 : -1), 0, src.lengthSteps() - 1); break;
                case 3: n.slide = ! n.slide; break;
                case 4: n.accent = ! n.accent; n.vel = (uint8_t) (n.accent ? 118 : 96); break;
                default: if (n.step % stepsPerBar != 0) continue; break; // drop
            }
        }
        if (pat.noteAtStep (n.step) < 0) pat.add (n);
    }
    // sprinkle a couple of new notes
    const int adds = (int) std::lround (amount * src.bars * 1.5f);
    for (int a = 0; a < adds; ++a)
    {
        const int s = r.range (0, src.lengthSteps() - 1);
        if ((s % 4 == 0 && r.chance (p.kickAvoid)) || pat.noteAtStep (s) >= 0) continue;
        Note n; n.step = (int16_t) s; n.len = 1; n.vel = 96;
        n.degree = (int16_t) pickDegree (r, st, p, INT32_MIN);
        pat.add (n);
    }
    humanizeAndFinish (r, pat, p);
}
} // namespace lb
