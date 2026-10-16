#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String age = "age",
        satOn = "sat_on", satType = "sat_type", drive = "drive", bias = "bias",
        noiseOn = "noise_on", noiseType = "noise_type", noiseLvl = "noise_level", noiseTone = "noise_tone", noiseDuck = "noise_duck",
        wobOn = "wobble_on", wow = "wow", flutter = "flutter", wobRate = "wow_rate", drift = "drift", wobStereo = "wobble_stereo",
        magOn = "magnetic_on", wear = "wear", bump = "head_bump", dropouts = "dropouts",
        digOn = "digital_on", rate = "sample_rate", bits = "bits", digFilter = "vintage_filter",
        spaceOn = "space_on", size = "space_size", decay = "space_decay", spaceMix = "space_mix",
        lowCut = "low_cut", highCut = "high_cut", width = "width", monoBass = "mono_bass",
        flux = "flux", mix = "mix", out = "out";
}

inline juce::StringArray satNames()   { return { "Tape", "Tube", "Transistor", "Fuzz" }; }
inline juce::StringArray noiseNames() { return { "Tape Hiss", "Vinyl Crackle", "Vinyl Dust", "Hum 50 Hz", "Hum 60 Hz" }; }
inline juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) std::round (v)) + " Hz"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hzText (v); });
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v * 100.0f)) + "%"; });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };
    auto f = [&] (const String& id, const String& nm, NormalisableRange<float> r, float d, AudioParameterFloatAttributes at) { l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, at)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };

    f (IDs::age, "Age", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct);
    b (IDs::satOn, "Saturate On", true); c (IDs::satType, "Saturation Type", satNames(), 0);
    f (IDs::drive, "Drive", NormalisableRange<float> (0.0f, 36.0f, 0.1f), 6.0f, db);
    f (IDs::bias, "Bias", NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct);
    b (IDs::noiseOn, "Noise On", true); c (IDs::noiseType, "Noise Type", noiseNames(), 0);
    f (IDs::noiseLvl, "Noise Level", NormalisableRange<float> (-80.0f, -10.0f, 0.1f), -42.0f, db);
    f (IDs::noiseTone, "Noise Tone", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    f (IDs::noiseDuck, "Noise Duck / Follow", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip);
    b (IDs::wobOn, "Wobble On", true);
    f (IDs::wow, "Wow", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct);
    f (IDs::flutter, "Flutter", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct);
    f (IDs::wobRate, "Wow Rate", range (0.05f, 6.0f, 0.8f), 0.6f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; }));
    f (IDs::drift, "Drift", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct);
    f (IDs::wobStereo, "Wobble Stereo", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct);
    b (IDs::magOn, "Magnetic On", true);
    f (IDs::wear, "Tape Wear", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct);
    f (IDs::bump, "Head Bump", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct);
    f (IDs::dropouts, "Dropouts", NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct);
    b (IDs::digOn, "Digital On", false);
    f (IDs::rate, "Sample Rate", range (1000.0f, 48000.0f, 12000.0f), 26040.0f, hz);
    f (IDs::bits, "Bit Depth", NormalisableRange<float> (2.0f, 16.0f, 0.1f), 12.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " bit"; }));
    f (IDs::digFilter, "Vintage Filter", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct);
    b (IDs::spaceOn, "Space On", false);
    f (IDs::size, "Space Size", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct);
    f (IDs::decay, "Space Decay", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    f (IDs::spaceMix, "Space Mix", NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct);
    f (IDs::lowCut, "Low Cut", range (20.0f, 1000.0f, 150.0f), 20.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 21.0f ? String ("Off") : hzText (v); }));
    f (IDs::highCut, "High Cut", range (1000.0f, 20000.0f, 6000.0f), 20000.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v > 19500.0f ? String ("Off") : hzText (v); }));
    f (IDs::width, "Width", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct);
    b (IDs::monoBass, "Mono Bass", false);
    f (IDs::flux, "Flux", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip);
    f (IDs::mix, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct);
    f (IDs::out, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db);
    return l;
}

// ---------------------------------------------------------------------------------------------
// Presets. The first 12 after Init are the MODELS (model buttons). sat_type 0 tape 1 tube 2 transistor 3 fuzz
// noise_type 0 hiss 1 crackle 2 dust 3 hum50 4 hum60
struct LPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<LPreset>& factoryPresets()
{
    static const std::vector<LPreset> p {
        { "Init (clean)", { { "sat_on", 0 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "magnetic_on", 0 } } },
        // ---------------- MODELS ----------------
        { "MODEL Cassette", { { "drive", 7 }, { "bias", 0.25f }, { "noise_level", -44 }, { "noise_tone", 0.55f }, { "wow", 0.35f }, { "flutter", 0.35f }, { "wow_rate", 0.7f }, { "wear", 0.45f }, { "head_bump", 0.4f }, { "dropouts", 0.15f }, { "high_cut", 14000 } } },
        { "MODEL Reel-to-Reel", { { "drive", 5 }, { "bias", 0.1f }, { "noise_level", -58 }, { "wow", 0.08f }, { "flutter", 0.1f }, { "drift", 0.1f }, { "wear", 0.1f }, { "head_bump", 0.6f }, { "dropouts", 0 } } },
        { "MODEL VHS", { { "drive", 9 }, { "noise_level", -38 }, { "noise_tone", 0.35f }, { "wow", 0.5f }, { "flutter", 0.25f }, { "wow_rate", 0.35f }, { "drift", 0.6f }, { "wear", 0.75f }, { "head_bump", 0.3f }, { "dropouts", 0.3f }, { "width", 0.7f } } },
        { "MODEL Worn Walkman", { { "drive", 11 }, { "bias", 0.4f }, { "noise_level", -36 }, { "wow", 0.85f }, { "flutter", 0.6f }, { "wow_rate", 1.1f }, { "drift", 0.8f }, { "wear", 0.7f }, { "head_bump", 0.3f }, { "dropouts", 0.5f }, { "wobble_stereo", 0.6f } } },
        { "MODEL Vinyl 45", { { "sat_type", 1 }, { "drive", 4 }, { "noise_type", 1 }, { "noise_level", -40 }, { "noise_tone", 0.6f }, { "wow", 0.15f }, { "flutter", 0.05f }, { "wow_rate", 0.75f }, { "drift", 0.05f }, { "magnetic_on", 0 }, { "low_cut", 30 }, { "mono_bass", 1 } } },
        { "MODEL Shellac 78", { { "sat_type", 1 }, { "drive", 10 }, { "noise_type", 1 }, { "noise_level", -30 }, { "noise_tone", 0.3f }, { "wow", 0.3f }, { "flutter", 0.1f }, { "wow_rate", 1.3f }, { "wear", 0.6f }, { "head_bump", 0 }, { "dropouts", 0 }, { "low_cut", 250 }, { "high_cut", 5000 }, { "width", 0 } } },
        { "MODEL AM Radio", { { "sat_type", 2 }, { "drive", 14 }, { "noise_type", 0 }, { "noise_level", -34 }, { "noise_tone", 0.2f }, { "wobble_on", 0 }, { "magnetic_on", 0 }, { "low_cut", 300 }, { "high_cut", 4500 }, { "width", 0 } } },
        { "MODEL Telephone", { { "sat_type", 2 }, { "drive", 18 }, { "noise_level", -46 }, { "wobble_on", 0 }, { "magnetic_on", 0 }, { "digital_on", 1 }, { "sample_rate", 8000 }, { "bits", 8 }, { "vintage_filter", 1 }, { "low_cut", 400 }, { "high_cut", 3400 }, { "width", 0 } } },
        { "MODEL SP-1200", { { "sat_on", 1 }, { "sat_type", 0 }, { "drive", 4 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "magnetic_on", 0 }, { "digital_on", 1 }, { "sample_rate", 26040 }, { "bits", 12 }, { "vintage_filter", 0.2f } } },
        { "MODEL MPC60", { { "drive", 3 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "magnetic_on", 0 }, { "digital_on", 1 }, { "sample_rate", 40000 }, { "bits", 12 }, { "vintage_filter", 0.8f }, { "head_bump", 0 } } },
        { "MODEL 8-Bit Game", { { "sat_type", 3 }, { "drive", 6 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "magnetic_on", 0 }, { "digital_on", 1 }, { "sample_rate", 11025 }, { "bits", 5 }, { "vintage_filter", 0 } } },
        { "MODEL Lo-Fi Hip-Hop", { { "drive", 6 }, { "noise_type", 1 }, { "noise_level", -42 }, { "noise_duck", -0.3f }, { "wow", 0.3f }, { "flutter", 0.2f }, { "wear", 0.55f }, { "head_bump", 0.5f }, { "dropouts", 0.1f }, { "digital_on", 1 }, { "sample_rate", 22050 }, { "bits", 12 }, { "high_cut", 9000 } } },
        // ---------------- DRUMS ----------------
        { "DRUMS Dusty Break", { { "sat_type", 0 }, { "drive", 10 }, { "noise_type", 2 }, { "noise_level", -40 }, { "wow", 0.1f }, { "flutter", 0.1f }, { "digital_on", 1 }, { "sample_rate", 26040 }, { "bits", 12 }, { "high_cut", 11000 } } },
        { "DRUMS Crunchy Tape Bus", { { "drive", 14 }, { "bias", 0.3f }, { "noise_on", 0 }, { "wow", 0.05f }, { "flutter", 0.08f }, { "wear", 0.35f }, { "head_bump", 0.7f }, { "dropouts", 0 } } },
        { "DRUMS 12-Bit Punch", { { "drive", 6 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "magnetic_on", 0 }, { "digital_on", 1 }, { "sample_rate", 30000 }, { "bits", 12 }, { "vintage_filter", 0.3f } } },
        { "DRUMS Phonk Cowbell Grit", { { "sat_type", 3 }, { "drive", 18 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "digital_on", 1 }, { "sample_rate", 18000 }, { "bits", 9 } } },
        // ---------------- KEYS / SYNTHS ----------------
        { "KEYS Warm Tape Piano", { { "drive", 5 }, { "noise_level", -50 }, { "wow", 0.25f }, { "flutter", 0.2f }, { "wear", 0.4f }, { "head_bump", 0.3f } } },
        { "KEYS Seasick Rhodes", { { "wow", 0.8f }, { "flutter", 0.3f }, { "wow_rate", 0.45f }, { "drift", 0.7f }, { "noise_level", -48 }, { "wear", 0.5f } } },
        { "KEYS Old Record Piano", { { "sat_type", 1 }, { "noise_type", 1 }, { "noise_level", -38 }, { "wow", 0.2f }, { "magnetic_on", 0 }, { "low_cut", 120 }, { "high_cut", 7000 }, { "space_on", 1 }, { "space_mix", 0.15f } } },
        { "SYNTH Detuned Pad Wobble", { { "wow", 0.6f }, { "flutter", 0.15f }, { "wow_rate", 0.3f }, { "drift", 0.5f }, { "wobble_stereo", 0.9f }, { "noise_level", -55 } } },
        { "SYNTH Lead Grit", { { "sat_type", 2 }, { "drive", 16 }, { "noise_on", 0 }, { "wow", 0.1f }, { "flutter", 0.2f } } },
        { "SYNTH Chiptune Lead", { { "sat_type", 3 }, { "drive", 3 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "magnetic_on", 0 }, { "digital_on", 1 }, { "sample_rate", 16000 }, { "bits", 4 }, { "vintage_filter", 0 } } },
        { "SYNTH Analog Drift", { { "sat_on", 0 }, { "noise_on", 0 }, { "magnetic_on", 0 }, { "wow", 0.15f }, { "flutter", 0.05f }, { "drift", 0.9f }, { "wow_rate", 0.2f } } },
        // ---------------- VOCALS ----------------
        { "VOX Old Radio Interlude", { { "sat_type", 2 }, { "drive", 12 }, { "noise_level", -36 }, { "wobble_on", 0 }, { "low_cut", 350 }, { "high_cut", 4000 }, { "width", 0 } } },
        { "VOX Tape Warmth", { { "drive", 5 }, { "noise_on", 0 }, { "wow", 0.08f }, { "flutter", 0.1f }, { "wear", 0.2f }, { "head_bump", 0.2f }, { "dropouts", 0 } } },
        { "VOX Voicemail", { { "sat_type", 2 }, { "drive", 15 }, { "noise_level", -44 }, { "wobble_on", 0 }, { "digital_on", 1 }, { "sample_rate", 8000 }, { "bits", 7 }, { "low_cut", 350 }, { "high_cut", 3300 } } },
        { "VOX Dreamy Memory", { { "wow", 0.5f }, { "flutter", 0.2f }, { "wear", 0.6f }, { "noise_level", -46 }, { "space_on", 1 }, { "space_size", 0.7f }, { "space_decay", 0.7f }, { "space_mix", 0.3f } } },
        // ---------------- MIX / MASTER ----------------
        { "MIX Subtle Analog Glue", { { "age", 0.5f }, { "drive", 4 }, { "noise_level", -60 }, { "wow", 0.05f }, { "flutter", 0.05f }, { "wear", 0.1f }, { "dropouts", 0 }, { "mix", 0.7f } } },
        { "MIX Lo-Fi Beat Bus", { { "drive", 8 }, { "noise_type", 1 }, { "noise_level", -44 }, { "noise_duck", -0.5f }, { "wow", 0.25f }, { "wear", 0.5f }, { "head_bump", 0.5f }, { "high_cut", 10000 }, { "mono_bass", 1 } } },
        { "MIX Parallel Grit (50%)", { { "sat_type", 3 }, { "drive", 20 }, { "noise_on", 0 }, { "wobble_on", 0 }, { "mix", 0.5f } } },
        { "MASTER Gentle Cassette Master", { { "age", 0.35f }, { "drive", 6 }, { "noise_level", -54 }, { "wow", 0.2f }, { "flutter", 0.2f }, { "wear", 0.3f }, { "dropouts", 0 }, { "mono_bass", 1 } } },
        // ---------------- FX / TRANSITIONS ----------------
        { "FX Broken Tape Machine", { { "age", 1.8f }, { "wow", 1 }, { "flutter", 0.8f }, { "wow_rate", 2.5f }, { "drift", 1 }, { "dropouts", 0.9f }, { "drive", 14 } } },
        { "FX Intro Filter Radio", { { "sat_type", 2 }, { "drive", 10 }, { "noise_level", -40 }, { "wobble_on", 0 }, { "low_cut", 500 }, { "high_cut", 2500 }, { "width", 0 } } },
        { "FX Ghost Hum (60 Hz)", { { "noise_type", 4 }, { "noise_level", -38 }, { "noise_duck", 0.6f }, { "wobble_on", 0 } } },
        { "FX Rain on Vinyl (Dust)", { { "noise_type", 2 }, { "noise_level", -34 }, { "noise_tone", 0.7f }, { "wow", 0.15f }, { "magnetic_on", 0 } } },
        { "FX Underwater Memory", { { "wow", 0.7f }, { "wow_rate", 0.25f }, { "high_cut", 1800 }, { "space_on", 1 }, { "space_size", 0.8f }, { "space_mix", 0.4f } } },
        { "FX Dynamic Flux Wobble", { { "flux", 0.9f }, { "wow", 0.7f }, { "flutter", 0.4f }, { "dropouts", 0.4f } } },
        { "FX Bitcrushed Ruin", { { "age", 1.6f }, { "sat_type", 3 }, { "drive", 20 }, { "digital_on", 1 }, { "sample_rate", 6000 }, { "bits", 4 }, { "vintage_filter", 0 } } },
        { "FX Noise Only Follows Music", { { "noise_type", 0 }, { "noise_level", -30 }, { "noise_duck", 1.0f }, { "wobble_on", 0 }, { "sat_on", 0 }, { "magnetic_on", 0 } } },
        { "FX Mono Shellac Ending", { { "noise_type", 1 }, { "noise_level", -28 }, { "low_cut", 300 }, { "high_cut", 4000 }, { "width", 0 }, { "wow", 0.4f }, { "age", 1.4f } } },
    };
    return p;
}
