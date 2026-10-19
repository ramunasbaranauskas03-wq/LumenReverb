#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Theory.h"
#include <vector>
#include <array>
#include <cstdint>
#include <map>

// =====================================================================================
// Lumen Chord — chord + arp MIDI engine (sample accurate)
//   modes: ONE-FINGER (a note -> the scale chord on it), PADS (C2..G2 -> 8 progression slots), AUTO (progression
//   plays itself with the song, 1/2/4 bars per chord), THROUGH (your own chords -> arp)
//   voicing: inversion, spread (close / open / wide), voice leading (smoothest path), bass note, octave, strum, humanize
//   arp: 10 modes, rate, octaves, gate, swing, latch, 16-step lane (velocity, rests, ratchets)
// =====================================================================================
namespace lumen::chord
{

enum Mode { OneFinger, Pads, Auto, Through };
enum ArpMode { Up, Down, UpDown, DownUp, Random, Order, Converge, Diverge, ChordStab, Thumb, kArpModes };
inline juce::StringArray arpModeNames() { return { "Up", "Down", "Up-Down", "Down-Up", "Random", "As Played", "Converge", "Diverge", "Chord (gate)", "Thumb" }; }
inline juce::StringArray arpRateNames() { return { "1/4", "1/8", "1/8 T", "1/8 D", "1/16", "1/16 T", "1/16 D", "1/32" }; }
inline double arpRateBeats (int i) { static const double b[] { 1.0, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.375, 0.125 }; return b[std::clamp (i, 0, 7)]; }

struct Slot { int degree = 0, shift = 0, quality = QAuto, inversion = -1, octave = 0; bool on = true; };

struct Params
{
    int key = 9, scale = 0, type = Seventh, mode = OneFinger;
    int inversion = 0, spread = 0, octave = 0; bool voiceLead = true, bass = true;
    float strumMs = 0, humanize = 0.1f, velocity = 0.8f; bool useInputVel = true;
    double chordBeats = 4.0;
    bool arpOn = false; int arpMode = Up, arpRate = 4, arpOct = 1; float arpGate = 0.6f, arpSwing = 0; bool latch = false;
    int steps = 16;
    float stepLevel[16] { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    uint32_t ratchet = 0;
    Slot slots[8];
    int numSlots = 4;
};

class ChordEngine
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset()
    {
        now = 0; offs.clear(); std::fill (std::begin (count), std::end (count), 0);
        held.clear(); chordFor.clear(); arpSet.clear(); arpPos = 0; arpStep = -1; autoIdx = -1; prevVoicing.clear();
        anchorOk = false;
    }
    void setParams (const Params& np) { p = np; }

    // builds the voiced chord of a slot (or of a raw degree when slot == nullptr)
    std::vector<int> voiceSlot (const Slot& s, int baseOctave, juce::String* name = nullptr)
    {
        const auto& sc = scales()[(size_t) std::clamp (p.scale, 0, (int) scales().size() - 1)];
        const int rootPc = ((p.key + degreeSemis (sc, s.degree) + s.shift) % 12 + 12) % 12;
        const int root = 12 * (baseOctave + 1) + rootPc + 12 * s.octave;      // baseOctave 3 -> C3 = 48
        auto iv = chordIntervals (sc, s.degree, p.type, s.quality);
        if (s.shift != 0 && s.quality == QAuto)
        {
            // borrowed chord: keep the shape of the parallel scale degree
            iv = chordIntervals (sc, s.degree, p.type, QAuto);
        }
        std::vector<int> notes; for (int i : iv) notes.push_back (root + i);
        if (name != nullptr) *name = chordName (rootPc, notes, useFlats (p.key, sc.iv));
        return voice (notes, root, s.inversion);
    }

    std::vector<int> voiceDegree (int absDegree)
    {
        const auto& sc = scales()[(size_t) std::clamp (p.scale, 0, (int) scales().size() - 1)];
        const int root = p.key + degreeSemis (sc, absDegree);
        const auto iv = chordIntervals (sc, ((absDegree % 7) + 7) % 7, p.type, QAuto);
        std::vector<int> notes; for (int i : iv) notes.push_back (root + i);
        return voice (notes, root, -1);
    }

    // in: input MIDI, out: generated MIDI; beat = host beat of sample 0, bps = beats per sample, playing = transport
    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int n, double beat, double bps, bool playing)
    {
        out.clear();
        // switching mode (e.g. AUTO -> THROUGH while playing) must not leave the old chord hanging
        if (p.mode != lastMode) { if (lastMode >= 0) allNotesOff (out, 0); lastMode = p.mode; }
        const double predicted = anchorBeat + (double) (now - anchorS) * anchorBps;
        if (! anchorOk || std::abs (bps - anchorBps) > 1e-12 || std::abs (beat - predicted) > 1e-4) { anchorBeat = beat; anchorS = now; anchorBps = bps; anchorOk = true; }
        auto it = in.begin();
        for (int i = 0; i < n; ++i)
        {
            // scheduled note-offs
            for (size_t k = 0; k < offs.size();)
                if (offs[k].at <= now) { noteOff (out, i, offs[k].note); offs[k] = offs.back(); offs.pop_back(); }
                else ++k;
            // pending (strummed / humanized) note-ons
            for (size_t k = 0; k < pend.size();)
                if (pend[k].at <= now) { noteOn (out, i, pend[k].note, pend[k].vel); if (pend[k].off > 0) offs.push_back ({ pend[k].off, pend[k].note }); pend[k] = pend.back(); pend.pop_back(); }
                else ++k;
            // input events at this sample
            while (it != in.end() && (*it).samplePosition <= i) { handleInput ((*it).getMessage(), out, i); ++it; }
            const double b = anchorBeat + (double) (now - anchorS) * anchorBps;
            if (p.mode == Auto && playing) autoStep (out, i, b, bps);
            else if (p.mode == Auto && ! playing && autoIdx >= 0) { setChordSource (out, i, {}); autoIdx = -1; }
            if (auditionEnd > 0 && now >= auditionEnd) { auditionEnd = 0; if (held.empty() && p.mode != Auto) arpSet.clear(); }
            if (p.arpOn) arpTick (out, i, b, bps);
            ++now;
        }
        while (it != in.end()) { handleInput ((*it).getMessage(), out, n - 1); ++it; }
    }

    void allNotesOff (juce::MidiBuffer& out, int pos)
    {
        for (int nt = 0; nt < 128; ++nt) if (count[nt] > 0) { out.addEvent (juce::MidiMessage::noteOff (1, nt), pos); count[nt] = 0; }
        offs.clear(); pend.clear(); held.clear(); chordFor.clear(); arpSet.clear(); autoIdx = -1;
    }

    // audition a slot from the UI (plays the chord for lenSamples, through the arp when it is on)
    void auditionSlot (juce::MidiBuffer& out, int pos, int slot, int64_t lenSamples)
    {
        if (! juce::isPositiveAndBelow (slot, 8)) return;
        lastSlot = slot;
        auto ch = voiceSlot (p.slots[slot], 3);
        if (p.arpOn) { arpSet = ch; arpPos = 0; arpVel = p.velocity; auditionEnd = now + lenSamples; return; }
        playChord (out, pos, ch, p.velocity, now + lenSamples);
    }

    // UI
    std::vector<int> chordNotes() const { return arpSet; }
    std::vector<int> soundingNotes() const { std::vector<int> v; for (int i = 0; i < 128; ++i) if (count[i] > 0) v.push_back (i); return v; }
    int currentSlot() const { return lastSlot; }

private:
    struct Off { int64_t at; int note; };
    struct Pend { int64_t at; int note; float vel; int64_t off; };

    std::vector<int> voice (std::vector<int> notes, int root, int slotInversion)
    {
        std::sort (notes.begin(), notes.end());
        const int inv = slotInversion >= 0 ? slotInversion : p.inversion;
        for (int k = 0; k < inv && ! notes.empty(); ++k) { int lo = notes.front(); notes.erase (notes.begin()); notes.push_back (lo + 12); std::sort (notes.begin(), notes.end()); }
        if (p.spread == 1 && notes.size() >= 3) { notes[notes.size() - 2] -= 12; std::sort (notes.begin(), notes.end()); }   // drop-2 (open)
        else if (p.spread == 2) { for (size_t k = 1; k < notes.size(); k += 2) notes[k] += 12; std::sort (notes.begin(), notes.end()); } // wide
        for (auto& v : notes) v += 12 * p.octave;
        // voice leading: move the whole shape (inversions / octaves) to sit closest to the previous chord
        if (p.voiceLead && ! prevVoicing.empty() && slotInversion < 0)
        {
            std::vector<int> best = notes; double bestCost = 1e9;
            std::vector<int> cand = notes;
            const double prevC = centre (prevVoicing);
            for (int r = 0; r < (int) notes.size(); ++r)
            {
                for (int o = -2; o <= 2; ++o)
                {
                    std::vector<int> c2 = cand; for (auto& v : c2) v += 12 * o;
                    double cost = 0;
                    for (int v : c2) { int m = 1000; for (int q : prevVoicing) m = std::min (m, std::abs (v - q)); cost += m; }
                    cost += 0.5 * std::abs (centre (c2) - prevC);
                    if (c2.front() < 40 || c2.back() > 96) cost += 100;
                    if (cost < bestCost) { bestCost = cost; best = c2; }
                }
                // next inversion
                int lo = cand.front(); cand.erase (cand.begin()); cand.push_back (lo + 12); std::sort (cand.begin(), cand.end());
            }
            notes = best;
        }
        prevVoicing = notes;
        if (p.bass)
        {
            int b = root + 12 * p.octave - 12;
            while (b >= notes.front()) b -= 12;
            while (b < 28) b += 12;
            notes.insert (notes.begin(), b);
        }
        for (auto& v : notes) v = std::clamp (v, 0, 127);
        return notes;
    }
    static double centre (const std::vector<int>& v) { double s = 0; for (int x : v) s += x; return v.empty() ? 60.0 : s / (double) v.size(); }

    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }

    void noteOn (juce::MidiBuffer& out, int pos, int note, float vel)
    {
        if (note < 0 || note > 127) return;
        if (count[note] > 0) out.addEvent (juce::MidiMessage::noteOff (1, note), pos);   // retrigger cleanly
        out.addEvent (juce::MidiMessage::noteOn (1, note, std::clamp (vel, 0.01f, 1.0f)), pos);
        ++count[note];
    }
    void noteOff (juce::MidiBuffer& out, int pos, int note)
    {
        if (note < 0 || note > 127 || count[note] <= 0) return;
        if (--count[note] == 0) out.addEvent (juce::MidiMessage::noteOff (1, note), pos);
    }

    float velFor (float inVel)
    {
        float v = p.useInputVel && inVel > 0 ? inVel : p.velocity;
        v *= 1.0f + (rnd() * 2.0f - 1.0f) * p.humanize * 0.25f;
        return std::clamp (v, 0.05f, 1.0f);
    }

    // play a chord directly (no arp): strum + humanized timing, notes stay on until released
    void playChord (juce::MidiBuffer& out, int pos, const std::vector<int>& notes, float vel, int64_t offAt = 0)
    {
        for (size_t k = 0; k < notes.size(); ++k)
        {
            const double d = (p.strumMs * 0.001 * fs) * (double) k + rnd() * p.humanize * 0.012 * fs;
            const int64_t at = now + (int64_t) d;
            if (at <= now) { noteOn (out, pos, notes[k], velFor (vel)); if (offAt > 0) offs.push_back ({ offAt, notes[k] }); }
            else pend.push_back ({ at, notes[k], velFor (vel), offAt });
        }
    }
    void releaseChord (juce::MidiBuffer& out, int pos, const std::vector<int>& notes)
    {
        for (int nt : notes)
        {
            bool pending = false;
            for (size_t k = 0; k < pend.size(); ++k) if (pend[k].note == nt) { pend[k] = pend.back(); pend.pop_back(); pending = true; break; }
            if (! pending) noteOff (out, pos, nt);
        }
    }

    // the chord the arp / the direct player should use now
    void setChordSource (juce::MidiBuffer& out, int pos, const std::vector<int>& notes, float vel = 0.8f)
    {
        if (p.arpOn)
        {
            if (notes.empty() && p.latch) return;
            if (arpSet.empty()) { arpPos = 0; }
            arpSet = notes; arpVel = vel;
            return;
        }
        releaseChord (out, pos, current);
        current = notes;
        if (! notes.empty()) playChord (out, pos, notes, vel);
    }

    void handleInput (const juce::MidiMessage& m, juce::MidiBuffer& out, int pos)
    {
        if (m.isAllNotesOff() || m.isAllSoundOff()) { allNotesOff (out, pos); current.clear(); return; }
        if (! m.isNoteOnOrOff()) { out.addEvent (m, pos); return; }
        const int nt = m.getNoteNumber();
        const bool on = m.isNoteOn();
        switch (p.mode)
        {
            case OneFinger:
            {
                if (on)
                {
                    const auto& sc = scales()[(size_t) std::clamp (p.scale, 0, (int) scales().size() - 1)];
                    const int d = snapToDegree (sc, p.key, nt);
                    auto ch = voiceDegree (d);
                    // keep the chord where the finger is
                    while (! ch.empty() && ch[p.bass ? 1 : 0] > nt + 7) for (auto& v : ch) v -= 12;
                    while (! ch.empty() && ch[p.bass ? 1 : 0] < nt - 7) for (auto& v : ch) v += 12;
                    chordFor[nt] = ch;
                    held.push_back (nt);
                    if (p.arpOn) setChordSource (out, pos, unionHeld(), m.getFloatVelocity());
                    else playChord (out, pos, ch, m.getFloatVelocity());
                }
                else
                {
                    held.erase (std::remove (held.begin(), held.end(), nt), held.end());
                    if (p.arpOn) setChordSource (out, pos, unionHeld(), 0.8f);
                    else if (chordFor.count (nt)) releaseChord (out, pos, chordFor[nt]);
                    chordFor.erase (nt);
                }
                break;
            }
            case Pads:
            {
                if (nt >= 48 && nt < 56)
                {
                    const int s = nt - 48;
                    if (on)
                    {
                        lastSlot = s;
                        auto ch = voiceSlot (p.slots[s], 3);
                        chordFor[nt] = ch; held.push_back (nt);
                        if (p.arpOn) setChordSource (out, pos, unionHeld(), m.getFloatVelocity());
                        else playChord (out, pos, ch, m.getFloatVelocity());
                    }
                    else
                    {
                        held.erase (std::remove (held.begin(), held.end(), nt), held.end());
                        if (p.arpOn) setChordSource (out, pos, unionHeld(), 0.8f);
                        else if (chordFor.count (nt)) releaseChord (out, pos, chordFor[nt]);
                        chordFor.erase (nt);
                    }
                }
                else out.addEvent (m, pos);                      // melody keys pass through
                break;
            }
            case Through:
            {
                if (! p.arpOn) { if (on) noteOn (out, pos, nt, m.getFloatVelocity()); else noteOff (out, pos, nt); break; }
                if (on) { if (held.empty() && p.latch) arpSet.clear(); held.push_back (nt); }
                else held.erase (std::remove (held.begin(), held.end(), nt), held.end());
                std::vector<int> s (held.begin(), held.end());
                if (! s.empty() || ! p.latch) { if (arpSet.empty()) arpPos = 0; arpSet = s; arpVel = m.getFloatVelocity() > 0 ? m.getFloatVelocity() : arpVel; }
                break;
            }
            default: out.addEvent (m, pos); break;   // AUTO: input passes through (play a melody over it)
        }
    }

    std::vector<int> unionHeld()
    {
        std::vector<int> u;
        for (int h : held) if (chordFor.count (h)) for (int v : chordFor[h]) if (std::find (u.begin(), u.end(), v) == u.end()) u.push_back (v);
        return u;
    }

    void autoStep (juce::MidiBuffer& out, int pos, double b, double /*bps*/)
    {
        const int ns = std::clamp (p.numSlots, 1, 8);
        const double barBeats = std::max (0.5, p.chordBeats);
        const int idx = (int) (((int64_t) std::floor (b / barBeats)) % ns);
        if (idx == autoIdx || b < 0) return;
        autoIdx = idx; lastSlot = idx;
        if (! p.slots[idx].on) { setChordSource (out, pos, {}); return; }
        auto ch = voiceSlot (p.slots[idx], 3);
        setChordSource (out, pos, ch, p.velocity);
    }

    void arpTick (juce::MidiBuffer& out, int pos, double b, double bps)
    {
        const double sb = arpRateBeats (p.arpRate);
        const double sf = b / sb;
        const double pair = std::floor (sf * 0.5);
        const double u = sf - 2.0 * pair, sw = std::clamp ((double) p.arpSwing, 0.0, 0.6);
        const bool second = u >= 1.0 + sw;
        const int64_t k = (int64_t) pair * 2 + (second ? 1 : 0);
        if (k == arpStep) return;
        arpStep = k;
        if (arpSet.empty()) return;
        const double stepLenS = (second ? 1.0 - sw : 1.0 + sw) * sb / bps;
        const int si = (int) (((k % p.steps) + p.steps) % p.steps);
        const float lvl = p.stepLevel[si];
        if (lvl <= 0.01f) { ++arpPos; return; }
        const bool rat = (p.ratchet >> si) & 1u;
        // ordered sequence
        std::vector<int> base = arpSet;
        std::vector<int> sorted = base; std::sort (sorted.begin(), sorted.end());
        std::vector<int> seq;
        for (int o = 0; o < std::clamp (p.arpOct, 1, 4); ++o) for (int v : (p.arpMode == Order ? base : sorted)) seq.push_back (v + 12 * o);
        std::vector<int> play;
        const int L = (int) seq.size();
        auto at = [&] (int i) { return seq[(size_t) (((i % L) + L) % L)]; };
        switch (p.arpMode)
        {
            case Up: case Order: play = { at (arpPos) }; break;
            case Down: play = { at (L - 1 - (arpPos % L)) }; break;
            case UpDown: { const int per = std::max (1, 2 * L - 2); const int q = arpPos % per; play = { at (q < L ? q : per - q) }; break; }
            case DownUp: { const int per = std::max (1, 2 * L - 2); const int q = arpPos % per; play = { at (L - 1 - (q < L ? q : per - q)) }; break; }
            case Random: play = { at ((int) (rnd() * (float) L)) }; break;
            case Converge: { const int q = arpPos % L; play = { (q % 2 == 0) ? at (q / 2) : at (L - 1 - q / 2) }; break; }
            case Diverge: { const int q = arpPos % L, mid = L / 2; play = { (q % 2 == 0) ? at (mid + q / 2) : at (mid - 1 - q / 2) }; break; }
            case ChordStab: play = sorted; break;
            case Thumb: { const int q = arpPos % std::max (1, 2 * (L - 1)); play = { (q % 2 == 0) ? at (0) : at (1 + q / 2) }; break; }
            default: play = { at (arpPos) }; break;
        }
        ++arpPos;
        const float vel = std::clamp (arpVel * (0.35f + 0.65f * lvl), 0.05f, 1.0f);
        const int hits = rat ? 2 : 1;
        for (int h = 0; h < hits; ++h)
        {
            const double start = stepLenS * h / hits, len = stepLenS / hits * std::clamp ((double) p.arpGate, 0.05, 1.0);
            for (int nt : play)
            {
                const int64_t onAt = now + (int64_t) start, offAt = now + (int64_t) std::max (8.0, start + len - 1.0);
                if (onAt <= now) { noteOn (out, pos, nt, h == 0 ? vel : vel * 0.8f); offs.push_back ({ offAt, nt }); }
                else pend.push_back ({ onAt, nt, h == 0 ? vel : vel * 0.8f, offAt });
            }
        }
    }

    Params p;
    double fs = 48000;
    int lastMode = -1;
    int64_t now = 0;
    std::vector<Off> offs;
    std::vector<Pend> pend;
    int count[128] {};
    std::vector<int> held, current, arpSet, prevVoicing;
    std::map<int, std::vector<int>> chordFor;
    float arpVel = 0.8f; int64_t auditionEnd = 0;
    int arpPos = 0; int64_t arpStep = -1; int autoIdx = -1, lastSlot = 0;
    uint32_t seed = 0xC0FFEEu;
    double anchorBeat = 0, anchorBps = 0; int64_t anchorS = 0; bool anchorOk = false;
};

} // namespace lumen::chord
