#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/EqEngine2.h"

// =====================================================================================
// Lumen EQ 2 — parameters (24 bands) and EDM instrument presets
// =====================================================================================
namespace IDs
{
    inline juce::String band (int b, const char* what) { return "b" + juce::String (b + 1) + "_" + what; }
    inline juce::String used (int b)  { return band (b, "used"); }
    inline juce::String on (int b)    { return band (b, "on"); }
    inline juce::String type (int b)  { return band (b, "type"); }
    inline juce::String freq (int b)  { return band (b, "freq"); }
    inline juce::String gain (int b)  { return band (b, "gain"); }
    inline juce::String q (int b)     { return band (b, "q"); }
    inline juce::String slope (int b) { return band (b, "slope"); }
    inline juce::String chan (int b)  { return band (b, "chan"); }
    inline juce::String dyn (int b)   { return band (b, "dyn"); }
    inline juce::String thr (int b)   { return band (b, "thr"); }
    inline juce::String sc (int b)    { return band (b, "sc"); }
    inline const juce::String mode = "mode", quality = "quality", scale = "scale", out = "out", autoGain = "auto_gain", attack = "attack", release = "release";
}

inline juce::StringArray typeNames()  { return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf" }; }
inline juce::StringArray slopeNames() { return { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" }; }
inline juce::StringArray chanNames()  { return { "Stereo", "Mid", "Side", "Left", "Right" }; }
inline juce::StringArray modeNames()  { return { "Zero Latency", "Linear Phase" }; }
inline juce::StringArray qualityNames() { return { "Low", "Medium", "High" }; }

inline juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (v, v < 100 ? 1 : 0) + " Hz"; }
inline juce::String noteName (float f)
{
    if (f <= 0) return {};
    static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::round (69.0 + 12.0 * std::log2 (f / 440.0));
    const float cents = (float) (100.0 * (69.0 + 12.0 * std::log2 (f / 440.0) - midi));
    return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1) + (std::abs (cents) >= 1 ? juce::String (" ") + (cents > 0 ? "+" : "") + juce::String ((int) std::round (cents)) + "c" : juce::String());
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 2 }; };
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hzText (v); })
                                             .withValueFromStringFunction ([] (const String& s) { return s.containsIgnoreCase ("k") ? s.getFloatValue() * 1000.0f : s.getFloatValue(); });
    auto qa = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10 ? 2 : 1); });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };
    for (int b = 0; b < lumen::eq2::kBands; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), "|");
        grp->addChild (std::make_unique<AudioParameterBool> (pid (IDs::used (b)), n + "Used", false));
        grp->addChild (std::make_unique<AudioParameterBool> (pid (IDs::on (b)), n + "On", true));
        grp->addChild (std::make_unique<AudioParameterChoice> (pid (IDs::type (b)), n + "Shape", typeNames(), 0));
        grp->addChild (std::make_unique<AudioParameterFloat> (pid (IDs::freq (b)), n + "Frequency", range (10.0f, 30000.0f, 1000.0f), 1000.0f, hz));
        grp->addChild (std::make_unique<AudioParameterFloat> (pid (IDs::gain (b)), n + "Gain", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f, db));
        grp->addChild (std::make_unique<AudioParameterFloat> (pid (IDs::q (b)), n + "Q", range (0.025f, 40.0f, 1.0f), 1.0f, qa));
        grp->addChild (std::make_unique<AudioParameterChoice> (pid (IDs::slope (b)), n + "Slope", slopeNames(), 3));
        grp->addChild (std::make_unique<AudioParameterChoice> (pid (IDs::chan (b)), n + "Placement", chanNames(), 0));
        grp->addChild (std::make_unique<AudioParameterFloat> (pid (IDs::dyn (b)), n + "Dynamic Range", NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f, db));
        grp->addChild (std::make_unique<AudioParameterFloat> (pid (IDs::thr (b)), n + "Threshold", NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -30.0f, db));
        grp->addChild (std::make_unique<AudioParameterBool> (pid (IDs::sc (b)), n + "External Side-chain", false));
        l.add (std::move (grp));
    }
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Processing Mode", modeNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::quality), "Linear Phase Quality", qualityNames(), 1));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::scale), "Gain Scale", NormalisableRange<float> (-2.0f, 2.0f, 0.001f), 1.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-36.0f, 36.0f, 0.01f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoGain), "Auto Gain", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::attack), "Dynamic Attack", range (0.5f, 200.0f, 10.0f), 10.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10 ? 1 : 0) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::release), "Dynamic Release", range (5.0f, 2000.0f, 150.0f), 150.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " ms"; })));
    return l;
}

// ---------------------------------------------------------------------------- EDM instrument presets
struct PB { int type; float freq, gain, q; int slope = 3, chan = 0; float dyn = 0, thr = -30; bool sc = false; };
struct EqPreset { const char* name; std::vector<PB> bands; int mode = 0; };
inline const std::vector<EqPreset>& factoryPresets()
{
    using namespace lumen::eq2;
    static const std::vector<EqPreset> p {
        { "Init (Flat)", {} },
        { "UTIL Clean Low Cut 30 Hz", { { LowCut, 30, 0, 0.71f, 3 } } },
        // ---- kick
        { "KICK Punch & Click", { { LowCut, 25, 0, 0.71f, 3 }, { Bell, 55, 2.5f, 1.2f }, { Bell, 300, -4, 1.4f }, { Bell, 3500, 3, 1.2f }, { HighShelf, 10000, -2, 0.7f, 1 } } },
        { "KICK Sub Weight", { { LowCut, 22, 0, 0.71f, 3 }, { LowShelf, 60, 3, 0.7f, 1 }, { Bell, 220, -3, 1.6f }, { Bell, 2500, 1.5f, 1.0f } } },
        { "KICK Tight for the Bass", { { LowCut, 30, 0, 0.71f, 3 }, { Bell, 80, 2, 1.5f }, { Bell, 150, -3, 2.0f }, { Bell, 400, -3, 1.2f } } },
        // ---- bass
        { "SUB Clean Mono Sub", { { LowCut, 25, 0, 0.71f, 3 }, { Bell, 50, 1.5f, 1.0f }, { HighCut, 180, 0, 0.71f, 3 }, { LowCut, 150, 0, 0.71f, 3, Side } } },
        { "BASS Reese Mid Growl", { { LowCut, 30, 0, 0.71f, 3 }, { Bell, 90, 1.5f, 1.0f }, { Bell, 250, -3, 1.0f }, { Bell, 900, 3, 0.8f }, { Bell, 2500, 2, 1.0f }, { HighCut, 12000, 0, 0.71f, 1 }, { LowCut, 150, 0, 0.71f, 3, Side } } },
        { "BASS 808 Phone-Audible", { { LowCut, 25, 0, 0.71f, 3 }, { Bell, 45, 2, 1.0f }, { Bell, 400, -3, 1.2f }, { Bell, 1800, 2.5f, 0.9f } } },
        { "BASS Duck Under Kick (Side-chain)", { { LowCut, 25, 0, 0.71f, 3 }, { Bell, 60, 0, 1.2f, 3, Stereo, -8, -32, true }, { Bell, 300, -2, 1.0f } } },
        { "BASS Future Bass Wobble", { { LowCut, 30, 0, 0.71f, 3 }, { Bell, 120, 1.5f, 1.0f }, { Bell, 500, -3, 1.0f }, { Bell, 3000, 0, 1.2f, 3, Stereo, -4, -24 }, { HighShelf, 6000, 2, 0.7f, 1 } } },
        // ---- leads / synths
        { "LEAD Supersaw Bright", { { LowCut, 150, 0, 0.71f, 3 }, { Bell, 350, -3, 1.0f }, { Bell, 2500, 0, 1.0f, 3, Stereo, -4, -20 }, { Bell, 3000, 1.5f, 0.8f }, { HighShelf, 10000, 3, 0.7f, 1 } } },
        { "LEAD Festival (Cuts Through)", { { LowCut, 200, 0, 0.71f, 3 }, { Bell, 500, -2.5f, 1.0f }, { Bell, 2000, 3, 0.9f }, { Bell, 5000, 1.5f, 1.0f }, { HighShelf, 12000, 2, 0.7f, 1 } } },
        { "PLUCK Clean & Snappy", { { LowCut, 180, 0, 0.71f, 3 }, { Bell, 400, -3, 1.0f }, { Bell, 2500, 2, 1.0f }, { HighShelf, 8000, 2.5f, 0.7f, 1 } } },
        { "PAD Wide & Out of the Way", { { LowCut, 250, 0, 0.71f, 1 }, { Bell, 600, -2, 0.9f }, { Bell, 2500, -3, 1.0f, 3, Mid }, { HighShelf, 10000, 2, 0.7f, 1, Side }, { HighCut, 16000, 0, 0.71f, 1 } } },
        { "CHORDS Stab Clean", { { LowCut, 200, 0, 0.71f, 3 }, { Bell, 300, -3, 1.0f }, { Bell, 1200, 2, 1.0f }, { HighShelf, 9000, 2, 0.7f, 1 } } },
        { "ARP Sparkle", { { LowCut, 300, 0, 0.71f, 3 }, { Bell, 800, -2, 1.0f }, { HighShelf, 7000, 3, 0.7f, 1 } } },
        { "KEYS House Piano", { { LowCut, 80, 0, 0.71f, 3 }, { Bell, 250, -2, 1.0f }, { Bell, 2500, 2, 1.0f }, { HighShelf, 10000, 1.5f, 0.7f, 1 } } },
        // ---- vocals
        { "VOX Lead Pop Air", { { LowCut, 90, 0, 0.71f, 2 }, { Bell, 250, -2.5f, 1.2f }, { Bell, 3000, 0, 1.2f, 3, Stereo, -4, -24 }, { Bell, 7000, 0, 3.0f, 3, Stereo, -6, -28 }, { HighShelf, 12000, 3, 0.7f, 1 } } },
        { "VOX Backing & Chops", { { LowCut, 200, 0, 0.71f, 3 }, { Bell, 500, -3, 1.0f }, { Bell, 3000, 2, 1.0f }, { Bell, 7000, 0, 3.0f, 3, Stereo, -6, -28 }, { HighShelf, 10000, 2, 0.7f, 1 } } },
        { "VOX De-Esser (Dynamic)", { { Bell, 6500, 0, 2.5f, 3, Stereo, -8, -30 } } },
        { "VOX Telephone FX", { { LowCut, 400, 0, 0.71f, 3 }, { Bell, 1200, 4, 1.0f }, { HighCut, 3500, 0, 0.71f, 3 } } },
        // ---- drums
        { "SNARE Crack", { { LowCut, 100, 0, 0.71f, 3 }, { Bell, 200, 2, 1.2f }, { Bell, 900, -3, 1.5f }, { Bell, 5000, 3, 1.0f }, { HighShelf, 10000, 2, 0.7f, 1 } } },
        { "CLAP Wide Top", { { LowCut, 250, 0, 0.71f, 3 }, { Bell, 1000, 2, 1.0f }, { HighShelf, 8000, 2, 0.7f, 1 }, { HighShelf, 6000, 2, 0.7f, 1, Side } } },
        { "HATS Silky (Dynamic)", { { LowCut, 400, 0, 0.71f, 3 }, { Bell, 3000, -2, 1.0f }, { Bell, 8000, 0, 1.5f, 3, Stereo, -4, -26 }, { HighShelf, 12000, 2, 0.7f, 1 } } },
        { "PERC Clean", { { LowCut, 200, 0, 0.71f, 3 }, { Bell, 500, -2, 1.0f }, { HighShelf, 8000, 2, 0.7f, 1 } } },
        { "DRUM BUS Punch", { { LowCut, 25, 0, 0.71f, 3 }, { Bell, 60, 1.5f, 1.0f }, { Bell, 350, -2, 0.9f }, { Bell, 4000, 1.5f, 1.0f }, { HighShelf, 12000, 1.5f, 0.7f, 1 } } },
        // ---- fx
        { "FX Riser Band-Pass Sweep", { { BandPass, 1000, 0, 1.0f } } },
        { "FX Downlifter Air", { { LowCut, 300, 0, 0.71f, 3 }, { HighShelf, 8000, 4, 0.7f, 1 } } },
        // ---- bus / master
        { "MASTER Gentle Polish (Linear)", { { LowCut, 20, 0, 0.71f, 1 }, { Tilt, 1000, 1, 0.71f }, { Bell, 300, -1, 0.8f }, { HighShelf, 14000, 1, 0.7f, 1 } }, 1 },
        { "MASTER Club Low End (Linear)", { { LowCut, 28, 0, 0.71f, 2 }, { LowCut, 120, 0, 0.71f, 3, Side }, { Bell, 55, 1, 0.9f }, { Bell, 250, -1.5f, 1.0f } }, 1 },
        { "MASTER Air & Clarity", { { Bell, 3000, 0, 1.0f, 3, Stereo, -2, -18 }, { HighShelf, 12000, 2, 0.7f, 1 } }, 1 },
        { "MIX BUS Mud Remover", { { Bell, 250, -2, 0.9f }, { Bell, 300, 0, 1.0f, 3, Stereo, -3, -22 }, { Bell, 500, -1.5f, 1.2f } } },
    };
    return p;
}
