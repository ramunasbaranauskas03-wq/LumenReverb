#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String ab = "ab", match = "match", trim = "trim", mono = "mono", slot = "slot", sync = "sync", preview = "preview",
                              loopS = "loop_s", loopE = "loop_e", meqOn = "meq_on", meqAmt = "meq_amt", avg = "avg";
    inline juce::String meqGain (int i) { return "meq_g" + juce::String (i + 1); }
}

constexpr int kSlots = 4, kMeqBands = 8;
inline const float kMeqFreq[kMeqBands] = { 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f };

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::ab), "Listen Reference", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::match), "Level Match", true));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::trim), "Reference Trim", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::mono), "Mono", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::slot), "Reference Slot", StringArray { "REF 1", "REF 2", "REF 3", "REF 4" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::sync), "Playback", StringArray { "Free", "Sync to DAW" }, 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::preview), "Play When Stopped", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::loopS), "Loop Start", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::loopE), "Loop End", NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::meqOn), "Match EQ", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::meqAmt), "Match EQ Amount", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; })));
    for (int i = 0; i < kMeqBands; ++i)
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::meqGain (i)), "Match EQ " + String (kMeqFreq[i], 0) + " Hz",
                                                      NormalisableRange<float> (-9.0f, 9.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::avg), "Analysis Average", StringArray { "1 s", "4 s", "10 s", "Infinite" }, 1));
    return l;
}
