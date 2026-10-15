#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <algorithm>

// =====================================================================================
// Chord memory + tempo-synced arpeggiator (MIDI in -> MIDI out, sample accurate).
// =====================================================================================
namespace lumen::synth
{

class Arp
{
public:
    struct Params
    {
        bool on = false; int mode = 0; double beats = 0.25; int octaves = 1; float gate = 0.6f; float swing = 0.0f; int chord = 0;
    };

    static const std::vector<int>& chordIntervals (int c)
    {
        static const std::vector<int> t[] = { { 0 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 2, 7 },
                                              { 0, 5, 7 }, { 0, 7, 12 }, { -12, 0, 12 }, { 0, 4, 7, 14 }, { 0, 3, 7, 10, 14 } };
        return t[juce::jlimit (0, 10, c)];
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        held.clear(); held.reserve (128);
        pending.clear(); pending.reserve (256);
        sounding.clear(); sounding.reserve (128);
        for (auto& c : chordOf) { c.clear(); c.reserve (8); }
        out.ensureSize (8192);
        now = 0; clock = 0.0; lastStep = -1; counter = 0; dir = 1; wasOn = false;
    }

    // Transforms `midi` in place.
    void process (juce::MidiBuffer& midi, int n, const Params& p, double bpm, bool playing, double ppq)
    {
        out.clear();
        if (! p.on)
        {
            if (wasOn) { flushSounding (0); held.clear(); pending.clear(); wasOn = false; }
            for (const auto m : midi)
            {
                const auto msg = m.getMessage();
                if (msg.isNoteOn())
                {
                    auto& list = chordOf[msg.getNoteNumber() & 127];
                    list.clear();
                    for (int iv : chordIntervals (p.chord))
                    {
                        const int nn = msg.getNoteNumber() + iv;
                        if (nn < 0 || nn > 127) continue;
                        list.push_back (nn);
                        out.addEvent (juce::MidiMessage::noteOn (msg.getChannel(), nn, msg.getVelocity()), m.samplePosition);
                    }
                }
                else if (msg.isNoteOff())
                {
                    auto& list = chordOf[msg.getNoteNumber() & 127];
                    if (list.empty()) out.addEvent (msg, m.samplePosition);
                    for (int nn : list) out.addEvent (juce::MidiMessage::noteOff (msg.getChannel(), nn), m.samplePosition);
                    list.clear();
                }
                else out.addEvent (msg, m.samplePosition);
            }
            midi.swapWith (out);
            return;
        }

        const double stepSamples = std::max (1.0, p.beats * 60.0 / std::max (1.0, bpm) * fs);
        const bool first = ! wasOn;
        if (first) { wasOn = true; lastStep = -1; clock = 0.0; }
        if (playing)
        {
            const double c = ppq / p.beats;
            // (re)sync to the host grid: the next step is the next grid line (or this one if we're exactly on it)
            if (first || std::abs (c - clock) > 0.5 || c < clock - 1e-6) lastStep = gridStep (c);
            clock = c;
        }
        auto it = midi.begin();
        for (int i = 0; i < n; ++i)
        {
            while (it != midi.end() && (*it).samplePosition <= i)
            {
                handleInput ((*it).getMessage(), i, p, playing);
                ++it;
            }
            const long long step = (long long) std::floor (clock);
            if (step > lastStep && ! held.empty())
            {
                lastStep = step;
                const long long delay = (step & 1) ? (long long) (p.swing * 0.5 * stepSamples) : 0;
                doStep (now + i + delay, p, stepSamples);
            }
            else if (step > lastStep) lastStep = step;
            emitDue (now + i, i);
            clock += 1.0 / stepSamples;
        }
        while (it != midi.end()) { handleInput ((*it).getMessage(), n - 1, p, playing); ++it; }
        now += n;
        midi.swapWith (out);
    }

private:
    struct Note { int note; juce::uint8 vel; };
    struct Pend { long long when; bool on; int note; juce::uint8 vel; };

    void handleInput (const juce::MidiMessage& msg, int pos, const Params& p, bool playing)
    {
        if (msg.isNoteOn())
        {
            const bool wasEmpty = held.empty();
            for (int iv : chordIntervals (p.chord))
            {
                const int nn = msg.getNoteNumber() + iv;
                if (nn < 0 || nn > 127) continue;
                held.erase (std::remove_if (held.begin(), held.end(), [nn] (const Note& x) { return x.note == nn; }), held.end());
                held.push_back ({ nn, msg.getVelocity() });
            }
            chordOf[msg.getNoteNumber() & 127].clear();
            for (int iv : chordIntervals (p.chord)) chordOf[msg.getNoteNumber() & 127].push_back (msg.getNoteNumber() + iv);
            if (wasEmpty && ! playing) { clock = 0.0; lastStep = -1; counter = 0; dir = 1; }
            else if (wasEmpty) { counter = 0; lastStep = gridStep (clock); }
        }
        else if (msg.isNoteOff())
        {
            for (int nn : chordOf[msg.getNoteNumber() & 127])
                held.erase (std::remove_if (held.begin(), held.end(), [nn] (const Note& x) { return x.note == nn; }), held.end());
            chordOf[msg.getNoteNumber() & 127].clear();
            if (held.empty()) { pending.erase (std::remove_if (pending.begin(), pending.end(), [] (const Pend& pd) { return pd.on; }), pending.end());
                                flushSounding (pos); }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { held.clear(); pending.clear(); flushSounding (pos); out.addEvent (msg, pos); }
        else out.addEvent (msg, pos);
    }

    static long long gridStep (double c) { return (long long) std::floor (c) - (c - std::floor (c) < 1e-6 ? 1 : 0); }

    std::vector<Note> pattern (const Params& p) const
    {
        std::vector<Note> base = held;
        if (p.mode != 4) std::sort (base.begin(), base.end(), [] (const Note& a, const Note& b) { return a.note < b.note; });
        std::vector<Note> seq;
        for (int o = 0; o < p.octaves; ++o)
            for (auto& nt : base) if (nt.note + 12 * o <= 127) seq.push_back ({ nt.note + 12 * o, nt.vel });
        return seq;
    }

    void doStep (long long when, const Params& p, double stepSamples)
    {
        const auto seq = pattern (p);
        if (seq.empty()) return;
        const long long len = std::max ((long long) (0.004 * fs), (long long) (p.gate * stepSamples));
        // close whatever is still sounding from the previous step
        for (int nn : sounding) pending.push_back ({ when, false, nn, 0 });
        sounding.clear();
        auto play = [&] (const Note& nt)
        {
            pending.push_back ({ when, true, nt.note, nt.vel });
            pending.push_back ({ when + len, false, nt.note, 0 });
        };
        const int N = (int) seq.size();
        switch (p.mode)
        {
            case 0: case 4: play (seq[(size_t) (counter % N)]); break;
            case 1: play (seq[(size_t) (N - 1 - (counter % N))]); break;
            case 2:
            {
                if (N == 1) { play (seq[0]); break; }
                const int period = 2 * N - 2;
                const int k = (int) (counter % period);
                play (seq[(size_t) (k < N ? k : period - k)]);
                break;
            }
            case 3: play (seq[(size_t) rnd.nextInt (N)]); break;
            default: for (auto& nt : seq) play (nt); break;
        }
        ++counter;
        std::stable_sort (pending.begin(), pending.end(), [] (const Pend& a, const Pend& b) { return a.when < b.when || (a.when == b.when && ! a.on && b.on); });
    }

    void emitDue (long long t, int pos)
    {
        size_t k = 0;
        while (k < pending.size() && pending[k].when <= t)
        {
            const auto& pd = pending[k];
            if (pd.on)
            {
                out.addEvent (juce::MidiMessage::noteOn (1, pd.note, pd.vel), pos);
                sounding.push_back (pd.note);
            }
            else
            {
                auto f = std::find (sounding.begin(), sounding.end(), pd.note);
                if (f != sounding.end()) { sounding.erase (f); out.addEvent (juce::MidiMessage::noteOff (1, pd.note), pos); }
            }
            ++k;
        }
        if (k > 0) pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) k);
    }

    void flushSounding (int pos)
    {
        for (int nn : sounding) out.addEvent (juce::MidiMessage::noteOff (1, nn), pos);
        sounding.clear();
        pending.erase (std::remove_if (pending.begin(), pending.end(), [] (const Pend& pd) { return ! pd.on; }), pending.end());
    }

    double fs = 48000;
    std::vector<Note> held;
    std::vector<Pend> pending;
    std::vector<int> sounding;
    std::vector<int> chordOf[128];
    juce::MidiBuffer out;
    juce::Random rnd;
    long long now = 0, lastStep = -1, counter = 0;
    double clock = 0.0;
    int dir = 1;
    bool wasOn = false;
};

} // namespace lumen::synth
