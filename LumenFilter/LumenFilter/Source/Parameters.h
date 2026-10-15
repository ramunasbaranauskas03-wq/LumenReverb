#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String type = "type", cutoff = "cutoff", res = "res", drive = "drive", mix = "mix", out = "out", spread = "spread",
        keyMode = "key_mode", root = "root", lfoOn = "lfo_on", lfoShape = "lfo_shape", lfoSync = "lfo_sync", lfoDiv = "lfo_div", lfoRate = "lfo_rate",
        lfoDepth = "lfo_depth", lfoStereo = "lfo_stereo", envOn = "env_on", envAmt = "env_amt", envAtt = "env_att", envRel = "env_rel", envSrc = "env_src",
        rise = "rise", riseMode = "rise_mode", os = "os";
}

inline const juce::StringArray& filterTypes() { static const juce::StringArray s { "Ladder LP24", "LP12", "LP24", "HP12", "HP24", "Band", "Notch", "Comb +", "Comb -", "Formant", "Phaser" }; return s; }
inline const juce::StringArray& divNames()    { static const juce::StringArray s { "4/1", "2/1", "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32" }; return s; }
inline double divBeats (int i) { static const double b[] = { 16, 8, 4, 2, 3, 4.0 / 3.0, 1, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 15, i)]; }
inline const juce::StringArray& noteNames()   { static const juce::StringArray s { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return s; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v * 100.0f, 0) + "%"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 0) + " Hz"; });
    NormalisableRange<float> cut (20.0f, 20000.0f); cut.setSkewForCentre (800.0f);
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::type), "Filter Type", filterTypes(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::cutoff), "Cutoff", cut, 20000.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::res), "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.1f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::drive), "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::spread), "Stereo Spread", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::keyMode), "Key Lock", StringArray { "Off", "Chromatic", "Major", "Minor" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::root), "Key Root", noteNames(), 9));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::lfoOn), "LFO On", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::lfoShape), "LFO Shape", StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pump", "S&H", "Smooth Rnd" }, 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::lfoSync), "LFO Sync", true));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::lfoDiv), "LFO Rate (Sync)", divNames(), 9));
    NormalisableRange<float> rate (0.02f, 30.0f); rate.setSkewForCentre (2.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lfoRate), "LFO Rate", rate, 2.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lfoDepth), "LFO Depth", NormalisableRange<float> (-1.0f, 1.0f), 0.3f, bip));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lfoStereo), "LFO Stereo Phase", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 180.0f, 0) + " deg"; })));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::envOn), "Envelope On", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::envAmt), "Envelope Amount", NormalisableRange<float> (-1.0f, 1.0f), 0.5f, bip));
    NormalisableRange<float> att (0.1f, 100.0f); att.setSkewForCentre (8.0f);
    NormalisableRange<float> rel (10.0f, 1000.0f); rel.setSkewForCentre (150.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::envAtt), "Envelope Attack", att, 5.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::envRel), "Envelope Release", rel, 150.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::envSrc), "Envelope Source", StringArray { "Input", "Sidechain" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::rise), "RISE", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::riseMode), "RISE Mode", StringArray { "HP Riser", "LP Down", "Band Squeeze" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::os), "Oversampling", StringArray { "1x", "2x", "4x" }, 1));
    return l;
}

struct FilterPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<FilterPreset>& factoryPresets()
{
    static const std::vector<FilterPreset> p {
        { "Init", {} },
        { "Wobble 1/8", { { "type", 0 }, { "cutoff", 320 }, { "res", 0.55f }, { "drive", 0.3f }, { "lfo_on", 1 }, { "lfo_shape", 0 }, { "lfo_div", 9 }, { "lfo_depth", 0.65f } } },
        { "Wobble Triplet", { { "type", 0 }, { "cutoff", 280 }, { "res", 0.6f }, { "drive", 0.4f }, { "lfo_on", 1 }, { "lfo_shape", 1 }, { "lfo_div", 11 }, { "lfo_depth", 0.7f } } },
        { "Pumping Filter 1/4", { { "type", 2 }, { "cutoff", 700 }, { "res", 0.2f }, { "lfo_on", 1 }, { "lfo_shape", 5 }, { "lfo_div", 6 }, { "lfo_depth", 0.55f } } },
        { "Build-Up HP Riser", { { "type", 0 }, { "rise_mode", 0 }, { "lfo_on", 1 }, { "lfo_shape", 3 }, { "lfo_div", 12 }, { "lfo_depth", 0.0f } } },
        { "Breakdown LP Down", { { "type", 0 }, { "rise_mode", 1 } } },
        { "Telephone Squeeze", { { "type", 0 }, { "rise_mode", 2 }, { "rise", 0.8f } } },
        { "Talking Formant", { { "type", 9 }, { "cutoff", 300 }, { "res", 0.5f }, { "lfo_on", 1 }, { "lfo_shape", 1 }, { "lfo_div", 6 }, { "lfo_depth", 0.45f } } },
        { "Phaser Sweep", { { "type", 10 }, { "cutoff", 900 }, { "res", 0.65f }, { "lfo_on", 1 }, { "lfo_shape", 0 }, { "lfo_div", 1 }, { "lfo_depth", 0.5f }, { "lfo_stereo", 0.5f } } },
        { "Envelope Funk", { { "type", 5 }, { "cutoff", 500 }, { "res", 0.5f }, { "env_on", 1 }, { "env_amt", 0.6f }, { "env_att", 3 }, { "env_rel", 120 } } },
        { "Stereo Motion", { { "type", 1 }, { "cutoff", 2500 }, { "res", 0.3f }, { "spread", 0.4f }, { "lfo_on", 1 }, { "lfo_div", 3 }, { "lfo_depth", 0.3f }, { "lfo_stereo", 0.5f } } },
        { "Resonant Key Lock", { { "type", 0 }, { "cutoff", 600 }, { "res", 0.78f }, { "key_mode", 3 }, { "lfo_on", 1 }, { "lfo_shape", 6 }, { "lfo_div", 12 }, { "lfo_depth", 0.35f } } },
        { "Comb Metallic", { { "type", 7 }, { "cutoff", 220 }, { "res", 0.7f }, { "mix", 0.7f }, { "lfo_on", 1 }, { "lfo_div", 2 }, { "lfo_depth", 0.3f } } },
        { "Sidechain Duck Filter", { { "type", 2 }, { "cutoff", 6000 }, { "env_on", 1 }, { "env_src", 1 }, { "env_amt", -0.7f }, { "env_att", 2 }, { "env_rel", 180 } } },
    };
    return p;
}
