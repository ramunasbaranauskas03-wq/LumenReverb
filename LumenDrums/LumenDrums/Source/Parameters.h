#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Sequencer.h"

namespace IDs
{
    inline const juce::String pattern = "pattern", seqOn = "seq_on", swing = "swing", humanize = "humanize", glue = "glue", clip = "clip", volume = "volume",
        genre = "genre", complexity = "complexity";
    inline juce::String pad (int p, const char* what) { return "p" + juce::String (p) + "_" + what; }
}
inline juce::StringArray patternNames() { return { "A", "B", "C", "D", "E", "F", "G", "H" }; }
inline juce::StringArray sectionNames() { return { "Intro", "Groove", "Drop", "Break", "Build", "Fill", "Drop + Fill", "Variation" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using namespace lumen::drums;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.01f ? String ("0") : (v > 0 ? "+" : "") + String (roundToInt (v * 100.0f)); });
    auto db  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -47.9f ? String ("-inf") : String (v, 1) + " dB"; });
    auto st  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " st"; });
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto f = [&] (const String& id, const String& nm, float lo, float hi, float d, AudioParameterFloatAttributes a) { l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, NormalisableRange<float> (lo, hi), d, a)); };

    c (IDs::pattern, "Pattern", patternNames(), 1);
    b (IDs::seqOn, "Sequencer On", true);
    f (IDs::swing, "Swing", 0.0f, 1.0f, 0.0f, pct);
    f (IDs::humanize, "Humanize", 0.0f, 1.0f, 0.15f, pct);
    f (IDs::glue, "Glue", 0.0f, 1.0f, 0.3f, pct);
    f (IDs::clip, "Clip", 0.0f, 1.0f, 0.2f, pct);
    f (IDs::volume, "Volume", -24.0f, 6.0f, -3.0f, db);
    StringArray gn; for (auto& g : genres()) gn.add (g.name);
    c (IDs::genre, "Genre", gn, 0);
    f (IDs::complexity, "Complexity", 0.0f, 1.0f, 0.5f, pct);
    for (int p = 0; p < kPads; ++p)
    {
        const String n = padNames()[p].toLowerCase().replace (" ", "") + " ";
        const String N = padNames()[p] + " ";
        c (IDs::pad (p, "model"), N + "Model", modelNames (p), 0);
        f (IDs::pad (p, "level"), N + "Level", -48.0f, 6.0f, 0.0f, db);
        f (IDs::pad (p, "pan"), N + "Pan", -1.0f, 1.0f, 0.0f, bip);
        f (IDs::pad (p, "tune"), N + "Tune", -24.0f, 24.0f, 0.0f, st);
        f (IDs::pad (p, "decay"), N + "Decay", 0.0f, 1.0f, 0.5f, pct);
        f (IDs::pad (p, "tone"), N + "Tone", -1.0f, 1.0f, 0.0f, bip);
        f (IDs::pad (p, "snap"), N + "Snap", 0.0f, 1.0f, 0.5f, pct);
        f (IDs::pad (p, "drive"), N + "Drive", 0.0f, 1.0f, 0.0f, pct);
        f (IDs::pad (p, "start"), N + "Sample Start", 0.0f, 0.9f, 0.0f, pct);
        b (IDs::pad (p, "rev"), N + "Reverse", false);
        b (IDs::pad (p, "mute"), N + "Mute", false);
        b (IDs::pad (p, "solo"), N + "Solo", false);
        c (IDs::pad (p, "choke"), N + "Choke Group", StringArray { "Off", "1", "2", "3", "4" }, (p == RCHat || p == ROHat) ? 1 : 0);
        c (IDs::pad (p, "out"), N + "Output", StringArray { "Main", "Out 2 (Kick)", "Out 3 (Snare/Clap)", "Out 4 (Hats)", "Out 5 (Perc/FX)" }, 0);
        (void) n;
    }
    return l;
}

// presets: one kit + 8-section song per genre (patterns are generated from the genre with a fixed seed)
struct DPreset { juce::String name; int genre; float complexity; uint32_t seed; };
inline const std::vector<DPreset>& factoryPresets()
{
    static const std::vector<DPreset> p = []
    {
        std::vector<DPreset> v;
        v.push_back ({ "Init (empty kit)", -1, 0.5f, 1 });
        const auto& g = lumen::drums::genres();
        for (int i = 0; i < (int) g.size(); ++i) v.push_back ({ juce::String (g[(size_t) i].name).toUpperCase() + " Kit", i, 0.5f, 1 });
        for (int i = 0; i < (int) g.size(); ++i) v.push_back ({ juce::String (g[(size_t) i].name).toUpperCase() + " Busy", i, 0.85f, 7 });
        return v;
    }();
    return p;
}
