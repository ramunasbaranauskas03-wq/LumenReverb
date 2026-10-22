#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Devices.h"
#include "dsp/Headphones.h"

namespace IDs
{
    inline const juce::String device = "device", room = "room", noise = "noise", drive = "drive", volume = "volume",
        headphones = "headphones", levelMatch = "level_match", out = "out", renderBypass = "render_bypass", cycle = "cycle",
        hpModel = "hp_model", hpAmount = "hp_amount";
}

inline juce::StringArray deviceNames()
{
    juce::StringArray a; for (auto& d : lumen::mixcheck::devices()) a.add (d.name); return a;
}
inline juce::StringArray cycleNames() { return { "Off", "Every Bar", "Every 2 Bars", "Every 4 Bars", "Every 8 Bars" }; }
// the devices CYCLE walks through
inline const std::vector<int>& cycleList()
{
    static const std::vector<int> v { 1, 2, 4, 6, 9, 12, 13, 16, 19, 20 };   // studio, club, car, phone, earbuds, laptop, bt speaker, fm, next room, mixcube
    return v;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::device), "Device", deviceNames(), 1));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::room), "Room", NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::noise), "Environment Noise", NormalisableRange<float> (0.0f, 2.0f, 0.001f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::drive), "Speaker Distortion", NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::volume), "Listening Level", NormalisableRange<float> (40.0f, 100.0f, 0.1f), 83.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " dB SPL"; })));
    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::headphones), "Virtual Speakers", true));
    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::levelMatch), "Level Match", true));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::renderBypass), "Bypass On Export", true));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::cycle), "Cycle Devices", cycleNames(), 0));
    juce::StringArray hp; for (auto& m : lumen::mixcheck::headphoneModels()) hp.add (m.name);
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::hpModel), "Your Headphones", hp, 1));          // Neumann NDH 30
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::hpAmount), "Headphone Correction", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, pct));
    return l;
}
