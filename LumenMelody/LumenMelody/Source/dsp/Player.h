#pragma once
#include "Generator.h"
#include <juce_audio_basics/juce_audio_basics.h>

// =====================================================================================
// Plays the lead + bass patterns in sync with the host (sample-accurate, block-size independent)
//   lead -> MIDI channel 1, bass -> MIDI channel 2
// =====================================================================================
namespace lumen::melody
{
enum ChordSource { Internal, FollowInput };
enum Harmony { FollowChord, KeepLine };

struct PlayParams
{
    int key = 9, scale = 0, source = Internal, harmony = FollowChord;
    int prog[8] { 0, 5, 2, 6, 0, 5, 2, 6 }; int numSlots = 4; double chordBeats = 4.0;
    int bars = 4;
    bool leadOn = true, bassOn = true;
    int leadOct = 4, bassOct = 1;
    float leadGate = 0.85f, bassGate = 0.8f, swing = 0.0f, humanize = 0.0f, velocity = 0.9f;
};

class Player
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset() { now = 0; offs.clear(); for (auto& c : count) std::fill (std::begin (c), std::end (c), 0); anchorOk = false; nextStep = -1; held.clear(); inputDeg = 0; freeBeat = 0; wasPlaying = false; }
    void setParams (const PlayParams& np) { p = np; }
    void setPatterns (const Pattern& lead, const Pattern& bass) { leadP = lead; bassP = bass; }

    // chord root degree (0..6) at a 16th step of the song
    int chordAtStep (int64_t step) const
    {
        if (p.source == FollowInput) return inputDeg;
        const int64_t perSlot = std::max<int64_t> (1, (int64_t) std::llround (p.chordBeats * 4.0));
        const int64_t slot = ((step / perSlot) % std::max (1, p.numSlots) + std::max (1, p.numSlots)) % std::max (1, p.numSlots);
        return ((p.prog[slot] % 7) + 7) % 7;
    }

    // MIDI pitch for a pattern note at a song step
    int pitchFor (const Note& n, int64_t songStep, bool bass) const
    {
        const auto& sc = scales()[(size_t) std::clamp (p.scale, 0, (int) scales().size() - 1)];
        const int cd = chordAtStep (songStep);
        int deg, shiftDeg = cd;
        if (n.approach)
        {
            const int nd = chordAtStep (songStep + n.len);
            shiftDeg = nd;
            if (nd == cd) deg = cd + 7 * 0 + n.rel;                         // no change: just the root
            else deg = nd + (((nd - cd) % 7 + 7) % 7 <= 3 ? -1 : 1);         // one scale step into the next root
        }
        else if (bass || p.harmony == FollowChord) deg = cd + n.rel;
        else
        {
            deg = n.rel;
            // KEEP mode: strong beats snap to the nearest chord tone
            if (songStep % 4 == 0)
            {
                auto isCt = [&] (int d) { const int r = ((d - cd) % 7 + 7) % 7; return r == 0 || r == 2 || r == 4; };
                if (! isCt (deg)) deg = isCt (deg - 1) ? deg - 1 : deg + 1;
            }
        }
        const int base = 12 * ((bass ? p.bassOct : p.leadOct) + 1) + p.key;
        // keep the line centred: chords on high degrees move the shape an octave down
        const int rootSemi = degreeSemis (sc, shiftDeg);
        const int shift = (bass || p.harmony == FollowChord) && rootSemi > 6 ? -12 : 0;
        return std::clamp (base + degreeSemis (sc, deg) + shift, 0, 127);
    }

    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int n, double beat, double bps, bool playing)
    {
        out.clear();
        if (! playing && wasPlaying) allNotesOff (out, 0);
        if (playing)
        {
            const double predicted = anchorBeat + (double) (now - anchorS) * anchorBps;
            if (! anchorOk || std::abs (bps - anchorBps) > 1e-12 || std::abs (beat - predicted) > 1e-4)
            {
                const bool jump = anchorOk && std::abs (beat - predicted) > 1e-4;
                anchorBeat = beat; anchorS = now; anchorBps = bps; anchorOk = true;
                if (jump || nextStep < 0) { if (jump) allNotesOff (out, 0); nextStep = firstStepAtOrAfter (beat); }
            }
        }
        else { anchorOk = false; nextStep = -1; }
        wasPlaying = playing;
        auto it = in.begin();
        for (int i = 0; i < n; ++i)
        {
            for (size_t k = 0; k < offs.size();)
                if (offs[k].at <= now) { noteOff (out, i, offs[k].ch, offs[k].note); offs[k] = offs.back(); offs.pop_back(); }
                else ++k;
            while (it != in.end() && (*it).samplePosition <= i) { handleInput ((*it).getMessage()); ++it; }
            if (playing)
            {
                const double b = anchorBeat + (double) (now - anchorS) * anchorBps;
                while (nextStep >= 0 && b + 1e-9 >= stepBeat (nextStep)) { fireStep (out, i, nextStep, bps); ++nextStep; }
            }
            ++now;
        }
        while (it != in.end()) { handleInput ((*it).getMessage()); ++it; }
    }

    void allNotesOff (juce::MidiBuffer& out, int pos)
    {
        for (int c = 0; c < 2; ++c) for (int nt = 0; nt < 128; ++nt) if (count[c][nt] > 0) { out.addEvent (juce::MidiMessage::noteOff (c + 1, nt), pos); count[c][nt] = 0; }
        offs.clear();
    }

    std::vector<int> sounding (int ch) const { std::vector<int> v; for (int i = 0; i < 128; ++i) if (count[ch][i] > 0) v.push_back (i); return v; }
    int currentInputDegree() const { return inputDeg; }
    int64_t lastStep() const { return nextStep - 1; }

private:
    struct Off { int64_t at; int ch, note; };

    double stepBeat (int64_t k) const { return (double) k * 0.25 + ((k & 1) ? 0.125 * p.swing : 0.0); }
    int64_t firstStepAtOrAfter (double b) const
    {
        int64_t k = (int64_t) std::floor (b * 4.0) - 1;
        while (stepBeat (k) < b - 1e-9) ++k;
        return k;
    }

    void handleInput (const juce::MidiMessage& m)
    {
        if (m.isNoteOn()) held.push_back (m.getNoteNumber());
        else if (m.isNoteOff()) { auto f = std::find (held.begin(), held.end(), m.getNoteNumber()); if (f != held.end()) held.erase (f); }
        else return;
        if (held.empty()) return;                                       // keep the last chord when released
        // chord detection: the root whose triad matches the most held pitch classes (ties -> lowest note)
        bool pcs[12] {}; int lowest = 127;
        for (int v : held) { pcs[v % 12] = true; lowest = std::min (lowest, v); }
        int bestRoot = lowest % 12, best = -100;
        for (int r = 0; r < 12; ++r)
        {
            if (! pcs[r]) continue;
            int sc = 3;
            sc += pcs[(r + 7) % 12] ? 2 : 0;
            sc += pcs[(r + 3) % 12] || pcs[(r + 4) % 12] ? 2 : 0;
            sc += pcs[(r + 10) % 12] || pcs[(r + 11) % 12] ? 1 : 0;
            if (r == lowest % 12) sc += 1;
            if (sc > best) { best = sc; bestRoot = r; }
        }
        const auto& sc = scales()[(size_t) std::clamp (p.scale, 0, (int) scales().size() - 1)];
        int bestD = 0, bestDist = 99;
        for (int d = 0; d < 7; ++d)
        {
            const int pc = (p.key + degreeSemis (sc, d)) % 12;
            int dist = std::abs (pc - bestRoot); dist = std::min (dist, 12 - dist);
            if (dist < bestDist) { bestDist = dist; bestD = d; }
        }
        inputDeg = bestD;
    }

    void fireStep (juce::MidiBuffer& out, int pos, int64_t songStep, double bps)
    {
        const int len = std::max (1, p.bars) * 16;
        const int ps = (int) (((songStep % len) + len) % len);
        for (int part = 0; part < 2; ++part)
        {
            const bool bass = part == 1;
            if (bass ? ! p.bassOn : ! p.leadOn) continue;
            const auto& pat = bass ? bassP : leadP;
            for (auto& nt : pat)
            {
                if (nt.step != ps) continue;
                const int pitch = pitchFor (nt, songStep, bass);
                float v = juce::jlimit (0.05f, 1.0f, nt.vel * p.velocity);
                if (p.humanize > 0) { hs ^= hs << 13; hs ^= hs >> 17; hs ^= hs << 5; v = juce::jlimit (0.05f, 1.0f, v * (1.0f - p.humanize * 0.25f * ((float) (hs & 0xffff) / 65535.0f))); }
                const double lenBeats = nt.len * 0.25 * (bass ? p.bassGate : p.leadGate);
                const int64_t offAt = now + std::max<int64_t> (1, (int64_t) std::llround (lenBeats / bps));
                noteOn (out, pos, part, pitch, v);
                offs.push_back ({ offAt, part, pitch });
            }
        }
    }

    void noteOn (juce::MidiBuffer& out, int pos, int ch, int note, float v)
    {
        if (count[ch][note] > 0)
        {
            out.addEvent (juce::MidiMessage::noteOff (ch + 1, note), pos);
            count[ch][note] = 0;
            offs.erase (std::remove_if (offs.begin(), offs.end(), [&] (const Off& o) { return o.ch == ch && o.note == note; }), offs.end());
        }
        out.addEvent (juce::MidiMessage::noteOn (ch + 1, note, v), pos);
        count[ch][note] = 1;
    }
    void noteOff (juce::MidiBuffer& out, int pos, int ch, int note)
    {
        if (count[ch][note] <= 0) return;
        count[ch][note] = 0;
        out.addEvent (juce::MidiMessage::noteOff (ch + 1, note), pos);
    }

    double fs = 48000;
    PlayParams p;
    Pattern leadP, bassP;
    std::vector<Off> offs;
    int count[2][128] {};
    std::vector<int> held; int inputDeg = 0;
    int64_t now = 0, nextStep = -1; double freeBeat = 0; bool wasPlaying = false;
    uint32_t hs = 12345;
    double anchorBeat = 0, anchorBps = 0; int64_t anchorS = 0; bool anchorOk = false;
};

} // namespace lumen::melody
