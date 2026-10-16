#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String target = "target", preview = "platform_preview", refAB = "reference_ab", refMatch = "reference_match",
        slope = "spectrum_slope", autoReset = "auto_reset", hold = "hold";
}

// streaming / delivery targets. policy: 0 none, 1 turn down only, 2 down + up while the true peak stays under the limit, 3 both ways
struct Platform { const char* name; float lufs; float tpLimit; int policy; const char* note; };
inline const std::vector<Platform>& platforms()
{
    static const std::vector<Platform> p {
        { "Spotify",        -14.0f, -1.0f, 2, "turns loud masters down, quiet ones up (limited by peaks)" },
        { "Apple Music",    -16.0f, -1.0f, 3, "Sound Check: both ways" },
        { "YouTube",        -14.0f, -1.0f, 1, "turns loud masters down only" },
        { "Amazon Music",   -14.0f, -2.0f, 1, "turns down, recommends -2 dBTP" },
        { "Tidal",          -14.0f, -1.0f, 1, "turns down only" },
        { "Deezer",         -15.0f, -1.0f, 1, "turns down only" },
        { "SoundCloud",     -14.0f, -1.0f, 0, "no normalisation - loudness is up to you" },
        { "Club / Beatport", -8.0f, -0.3f, 0, "no normalisation, typical club masters -6..-9 LUFS" },
        { "Broadcast EBU R128", -23.0f, -1.0f, 3, "TV / radio delivery" },
    };
    return p;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    StringArray names; for (auto& p : platforms()) names.add (p.name);
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::target), "Target", names, 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::preview), "Platform Preview", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::refAB), "Reference A/B", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::refMatch), "Reference Level Match", true));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::slope), "Spectrum 4.5 dB Slope", true));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoReset), "Auto Reset on Play", true));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::hold), "Hold (freeze history)", false));
    return l;
}

// gain a platform would apply to a master (dB)
inline float platformGain (const Platform& p, float integrated, float truePeakDb)
{
    if (integrated < -69.0f) return 0.0f;
    const float g = p.lufs - integrated;
    switch (p.policy)
    {
        case 1: return std::min (0.0f, g);
        case 2: return g < 0.0f ? g : std::max (0.0f, std::min (g, p.tpLimit - truePeakDb));
        case 3: return g;
        default: return 0.0f;
    }
}
