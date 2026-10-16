#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/GlitchEngine.h"

namespace IDs
{
    inline const juce::String steps = "steps", rate = "step_rate", swing = "swing", play = "play_mode", slot = "pattern_slot", midi = "midi_control",
        xfade = "crossfade", repRate = "repeat_rate", repRamp = "repeat_roll", stopTime = "stop_time", stopCurve = "stop_curve",
        scratch = "scratch_depth", shuf = "shuffle_range", gateDuty = "gate_length", gateSmooth = "gate_smooth", bits = "crush_bits", down = "crush_downsample",
        fltType = "filter_type", fltAmt = "filter_sweep", fltRes = "filter_res", echoTime = "echo_time", echoFb = "echo_feedback", echoLvl = "echo_level",
        mix = "mix", out = "out";
    inline juce::String prob (int l) { return "prob_" + juce::String (l + 1); }
}

inline juce::StringArray stepRateNames() { return { "1/4", "1/8", "1/16", "1/32" }; }
inline juce::StringArray repeatNames()   { return { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/8 T", "1/16 T" }; }
inline juce::StringArray playNames()     { return { "Always", "Every 2nd Bar", "Last Bar of 4", "Last Bar of 8", "MIDI Only" }; }
inline juce::StringArray echoNames()     { return { "1/16", "1/8", "1/8 D", "1/4", "1/4 D" }; }
inline juce::StringArray slotNames()     { return { "A", "B", "C", "D", "E", "F", "G", "H" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; });
    auto one = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); });

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::steps), "Steps", StringArray { "16", "32" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::rate), "Step Rate", stepRateNames(), 2));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::swing), "Swing", NormalisableRange<float> (0.0f, 0.6f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::play), "Play Mode", playNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::slot), "Pattern Slot", slotNames(), 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::midi), "MIDI Control", true));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::xfade), "Crossfade", NormalisableRange<float> (0.5f, 20.0f, 0.1f), 3.0f, ms));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::repRate), "Repeat Size", repeatNames(), 3));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::repRamp), "Repeat Roll", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::stopTime), "Stop Time", NormalisableRange<float> (0.1f, 1.0f), 0.8f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::stopCurve), "Stop Curve", NormalisableRange<float> (0.5f, 3.0f), 1.5f, one));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::scratch), "Scratch Depth", NormalisableRange<float> (0.0f, 1.0f), 0.7f, pct));
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::shuf), "Shuffle Range", 1, 4, 2));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::gateDuty), "Gate Length", NormalisableRange<float> (0.05f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::gateSmooth), "Gate Smooth", NormalisableRange<float> (0.3f, 30.0f, 0.1f), 2.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bits), "Crush Bits", NormalisableRange<float> (2.0f, 16.0f, 0.1f), 8.0f, one));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::down), "Crush Downsample", NormalisableRange<float> (1.0f, 40.0f, 1.0f), 6.0f, one));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::fltType), "Filter Type", StringArray { "Low-Pass", "High-Pass", "Band-Pass" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::fltAmt), "Filter Sweep", NormalisableRange<float> (0.0f, 1.0f), 0.7f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::fltRes), "Filter Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::echoTime), "Echo Time", echoNames(), 2));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::echoFb), "Echo Feedback", NormalisableRange<float> (0.0f, 0.95f), 0.45f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::echoLvl), "Echo Level", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    for (int i = 0; i < lumen::glitch::kLanes; ++i)
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::prob (i)), "Chance " + String (lumen::glitch::laneName (i)),
                                                      NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    return l;
}

// ---------------------------------------------------------------------------------------------
// Presets. Lanes: 0 Repeat 1 Reverse 2 TapeStop 3 HalfSpeed 4 Scratch 5 Shuffle 6 Gate 7 Crush 8 Filter 9 Echo
// Pattern strings: 'x' = on, '.' = off (16 or 32 characters)
struct GlPreset { const char* name; std::vector<std::pair<const char*, float>> v; std::vector<std::pair<int, const char*>> lanes; };
inline const std::vector<GlPreset>& factoryPresets()
{
    // step_rate 0 1/4 1/8 2 1/16 3 1/32   repeat_rate 0 1/2 1 1/4 2 1/8 3 1/16 4 1/32 5 1/64 6 1/8T 7 1/16T
    // play_mode 0 always 1 every 2nd bar 2 last bar of 4 3 last bar of 8 4 MIDI only
    static const std::vector<GlPreset> p {
        { "Init (empty)", {}, {} },
        // ---------------- STUTTER / ROLLS ----------------
        { "ROLL 1/16 End of Bar", { { "repeat_rate", 3 } }, { { 0, "............xxxx" } } },
        { "ROLL Accelerating Snare Roll", { { "repeat_rate", 2 }, { "repeat_roll", 0.8f } }, { { 0, "........xxxxxxxx" } } },
        { "ROLL 1/32 Machine Gun", { { "repeat_rate", 4 } }, { { 0, "....xx......xxxx" } } },
        { "ROLL Triplet Stutter", { { "repeat_rate", 7 } }, { { 0, "..xx..xx..xx.xxx" } } },
        { "ROLL Build-up 4 Bars", { { "steps", 1 }, { "step_rate", 0 }, { "repeat_rate", 1 }, { "repeat_roll", 0.6f } }, { { 0, "............xxxxxxxxxxxxxxxxxxxx" }, { 8, "........................xxxxxxxx" } } },
        { "ROLL Vocal Chop Stutter", { { "repeat_rate", 3 }, { "crossfade", 2 } }, { { 0, "x.x.....x.x.xxx." } } },
        { "ROLL Half-Bar Freeze", { { "repeat_rate", 0 } }, { { 0, "........xxxxxxxx" } } },
        { "ROLL 1/64 Buzz Fill", { { "repeat_rate", 5 } }, { { 0, "..............xx" } } },
        { "ROLL Stutter + Echo Tail", { { "repeat_rate", 3 } }, { { 0, "............xxx." }, { 9, "...............x" } } },
        // ---------------- TAPE / SPEED ----------------
        { "TAPE Stop at Bar End", { { "stop_time", 0.95f } }, { { 2, "............xxxx" } } },
        { "TAPE Quick Brake", { { "stop_time", 0.5f }, { "stop_curve", 0.8f } }, { { 2, "..............xx" } } },
        { "TAPE Slow Power Down (Drop)", { { "steps", 1 }, { "step_rate", 1 }, { "stop_time", 1.0f }, { "stop_curve", 2.2f }, { "play_mode", 2 } }, { { 2, "........................xxxxxxxx" } } },
        { "TAPE Half-Time Bar 2", { { "play_mode", 1 } }, { { 3, "xxxxxxxxxxxxxxxx" } } },
        { "TAPE Half-Time Groove", {}, { { 3, "xxxx....xxxx...." } } },
        { "TAPE Half Speed + Low-Pass", { { "filter_sweep", 0.4f } }, { { 3, "xxxxxxxxxxxxxxxx" }, { 8, "xxxxxxxxxxxxxxxx" } } },
        { "TAPE Wobble Stops", { { "stop_time", 0.4f }, { "step_rate", 1 } }, { { 2, ".x.x.x.x.x.x.x.x" } } },
        // ---------------- REVERSE ----------------
        { "REV Last Beat Reverse", {}, { { 1, "............xxxx" } } },
        { "REV Offbeat Reverses", {}, { { 1, "..x...x...x...x." } } },
        { "REV Reverse Swell + Echo", { { "echo_feedback", 0.6f } }, { { 1, "........xxxx...." }, { 9, "...........x...." } } },
        { "REV Backwards Vocal Bits", { { "step_rate", 1 } }, { { 1, "...x.......x..x." } } },
        // ---------------- SCRATCH / SHUFFLE ----------------
        { "SCR DJ Scratch Fill", { { "scratch_depth", 0.9f } }, { { 4, "............xxxx" } } },
        { "SCR Baby Scratches", { { "scratch_depth", 0.75f } }, { { 4, "..x...x...xx..x." } } },
        { "SCR Scratch + Crush", { { "scratch_depth", 0.85f }, { "crush_bits", 6 } }, { { 4, "....xx......xx.." }, { 7, "....xx......xx.." } } },
        { "SHUF Beat Rearrange", { { "shuffle_range", 4 } }, { { 5, "..x..x...x..x..x" } } },
        { "SHUF Chopped Breaks", { { "shuffle_range", 2 }, { "step_rate", 2 } }, { { 5, "x.x.x...x.x.x..." }, { 0, "..............xx" } } },
        { "SHUF Random Every Bar", { { "shuffle_range", 3 }, { "prob_6", 0.5f } }, { { 5, "xxxxxxxxxxxxxxxx" } } },
        // ---------------- GATE / TRANCE ----------------
        { "GATE Trance 1/16", { { "gate_length", 0.55f } }, { { 6, "xxxxxxxxxxxxxxxx" } } },
        { "GATE Offbeat Chop", { { "gate_length", 0.5f } }, { { 6, "x.x.x.x.x.x.x.x." } } },
        { "GATE Stutter Gate 1/32", { { "step_rate", 3 }, { "steps", 1 }, { "gate_length", 0.45f } }, { { 6, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" } } },
        { "GATE Smooth Pumping", { { "gate_length", 0.7f }, { "gate_smooth", 18 } }, { { 6, "xxxxxxxxxxxxxxxx" } } },
        { "GATE Choppy Pads", { { "gate_length", 0.35f }, { "swing", 0.2f } }, { { 6, "xx.xx.xx.xx.xx.x" } } },
        // ---------------- CRUSH / FILTER / ECHO ----------------
        { "CRUSH Lo-Fi Hits", { { "crush_bits", 5 }, { "crush_downsample", 10 } }, { { 7, "x...x...x...x..." } } },
        { "CRUSH 8-Bit Fill", { { "crush_bits", 4 }, { "crush_downsample", 16 } }, { { 7, "............xxxx" }, { 0, "..............xx" } } },
        { "FILTER LP Sweep Down Fill", { { "filter_sweep", 0.85f }, { "filter_res", 0.6f } }, { { 8, "........xxxxxxxx" } } },
        { "FILTER HP Riser", { { "filter_type", 1 }, { "filter_sweep", 0.9f }, { "steps", 1 }, { "step_rate", 1 } }, { { 8, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" } } },
        { "FILTER Band-Pass Wah Steps", { { "filter_type", 2 }, { "filter_sweep", 0.8f }, { "filter_res", 0.7f } }, { { 8, "x.x.x.x.x.x.x.x." } } },
        { "ECHO Dub Throws", { { "echo_time", 2 }, { "echo_feedback", 0.65f } }, { { 9, "...x.......x...." } } },
        { "ECHO Last-Note Throw", { { "echo_time", 3 }, { "echo_feedback", 0.7f } }, { { 9, "..............x." } } },
        // ---------------- GENRE COMBOS ----------------
        { "EDM Drop Fill (Last Bar of 4)", { { "play_mode", 2 }, { "repeat_rate", 3 }, { "repeat_roll", 0.5f } }, { { 0, "........xxxxxx.." }, { 2, "..............xx" }, { 8, "........xxxxxxxx" } } },
        { "EDM Pre-Drop Silence Stop", { { "play_mode", 2 }, { "stop_time", 0.9f } }, { { 0, "........xxxx...." }, { 2, "............xxxx" } } },
        { "DUBSTEP Wobble Chops", { { "repeat_rate", 6 }, { "filter_type", 0 }, { "filter_sweep", 0.6f } }, { { 0, "x..x..x...x..x.." }, { 8, "x.x.x.x.x.x.x.x." }, { 3, "............xxxx" } } },
        { "DUBSTEP Half-Time Crush", { { "crush_bits", 6 }, { "crush_downsample", 4 } }, { { 3, "xxxxxxxx........" }, { 7, "....xxxx........" } } },
        { "TRAP Hi-Hat Rolls", { { "repeat_rate", 4 }, { "repeat_roll", 0.3f } }, { { 0, "...x...x..xx.xxx" } } },
        { "TRAP Triplet Rolls", { { "repeat_rate", 7 } }, { { 0, "..x.....x.x...xx" } } },
        { "HOUSE Swing Gate", { { "swing", 0.25f }, { "gate_length", 0.6f } }, { { 6, "xxxxxxxxxxxxxxxx" }, { 9, "...............x" } } },
        { "TECHNO Hypnotic Repeats", { { "repeat_rate", 2 }, { "prob_1", 0.5f } }, { { 0, "..x...x...x...x." }, { 8, "xxxxxxxxxxxxxxxx" } } },
        { "DNB Break Mangler", { { "shuffle_range", 3 }, { "repeat_rate", 4 } }, { { 5, "...x...x...x..x." }, { 0, "......x.......xx" }, { 1, "..........x....." } } },
        { "FUTURE BASS Vocal Chops", { { "repeat_rate", 3 }, { "gate_length", 0.6f } }, { { 0, "x..x..x...x.x..." }, { 6, "x.xx.xx.x.xx.x.x" }, { 1, "...............x" } } },
        { "LOFI Dusty Stutters", { { "crush_bits", 9 }, { "crush_downsample", 3 }, { "repeat_rate", 2 }, { "swing", 0.3f } }, { { 7, "xxxxxxxxxxxxxxxx" }, { 0, "......x.......x." }, { 2, "..............xx" } } },
        { "IDM Chaos Generator", { { "prob_1", 0.4f }, { "prob_2", 0.4f }, { "prob_5", 0.4f }, { "prob_6", 0.4f }, { "prob_8", 0.4f } }, { { 0, "x.x.x.x.x.x.x.x." }, { 1, ".x.x.x.x.x.x.x.x" }, { 4, "..x...x...x...x." }, { 5, "x...x...x...x..." }, { 7, ".x...x...x...x.." } } },
        { "GLITCH Every 8th Bar Surprise", { { "play_mode", 3 }, { "repeat_roll", 0.7f } }, { { 0, "....xxxxxxxx...." }, { 1, "..xx............" }, { 2, "............xxxx" } } },
        // ---------------- PERFORMANCE ----------------
        { "LIVE MIDI Only (play C1-A1)", { { "play_mode", 4 } }, {} },
        { "LIVE MIDI + Chance 50%", { { "play_mode", 0 }, { "prob_1", 0.5f }, { "prob_7", 0.5f } }, { { 0, "............xxxx" }, { 6, "xxxxxxxxxxxxxxxx" } } },
    };
    return p;
}

inline uint32_t patternBits (const char* s)
{
    uint32_t b = 0;
    int n = 0;
    for (int i = 0; s[i] != 0 && i < 32; ++i, ++n) if (s[i] == 'x' || s[i] == 'X') b |= 1u << i;
    if (n <= 16) b |= b << 16;          // 16-step patterns tile into 32 steps
    return b;
}
