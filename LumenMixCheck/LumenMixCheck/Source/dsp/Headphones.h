#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// =====================================================================================
// Headphone correction profiles
//   Each profile is the inverse of the model's typical deviation from a neutral target
//   (average published measurements; real units vary +-2..4 dB, so for exact results
//   load your own AutoEQ / Sonarworks-style "ParametricEQ.txt").
// =====================================================================================
namespace lumen::mixcheck
{
struct PeqBand { int type; float f, g, q; };   // 0 peak, 1 low shelf, 2 high shelf, 3 low pass, 4 high pass

struct HeadphoneModel
{
    const char* name; const char* brand; bool open;
    std::vector<PeqBand> eq;
    const char* note;
};

inline const std::vector<HeadphoneModel>& headphoneModels()
{
    static const std::vector<HeadphoneModel> m {
        { "No Correction", "", false, {}, "Headphones are not corrected." },
        // ---- Neumann
        { "Neumann NDH 30", "Neumann", true, { { 1, 50, 2.5f, 0.7f }, { 0, 2200, 2.0f, 0.9f }, { 0, 10000, 2.5f, 2.0f } },
          "Open reference: lifts the slightly shy sub, the 1.5-3 kHz dip and the 10 kHz dip." },
        { "Neumann NDH 20", "Neumann", false, { { 1, 100, -1.5f, 0.7f }, { 0, 3000, 2.0f, 1.0f }, { 0, 8500, -2.0f, 3.0f } },
          "Closed: tames the warm low end and the 8.5 kHz peak." },
        // ---- Beyerdynamic
        { "Beyerdynamic DT 770 Pro", "Beyerdynamic", false, { { 0, 90, -3.5f, 0.8f }, { 0, 3000, 2.0f, 1.2f }, { 0, 8500, -5.0f, 3.0f }, { 2, 12000, -1.5f, 0.7f } },
          "V-shape: bass hump and the strong 8-9 kHz treble peak are pulled back." },
        { "Beyerdynamic DT 990 Pro", "Beyerdynamic", true, { { 0, 80, -2.5f, 0.8f }, { 0, 3000, 1.5f, 1.2f }, { 0, 8500, -6.0f, 3.0f }, { 2, 12000, -2.0f, 0.7f } },
          "Bright open-back: the sharp 8.5 kHz peak is the main fix." },
        { "Beyerdynamic DT 880 Pro", "Beyerdynamic", true, { { 1, 50, 1.5f, 0.7f }, { 0, 3000, 1.0f, 1.2f }, { 0, 9000, -3.5f, 3.0f } },
          "Semi-open: milder treble peak around 9 kHz." },
        { "Beyerdynamic DT 1990 Pro", "Beyerdynamic", true, { { 1, 60, 1.0f, 0.7f }, { 0, 2500, 1.0f, 1.0f }, { 0, 8000, -4.5f, 3.0f } },
          "Open planar-like tuning with an 8 kHz peak." },
        { "Beyerdynamic DT 700 Pro X", "Beyerdynamic", false, { { 0, 100, -2.0f, 0.8f }, { 0, 6000, -2.0f, 2.5f } },
          "Closed, close to neutral: small bass and 6 kHz trims." },
        { "Beyerdynamic DT 900 Pro X", "Beyerdynamic", true, { { 1, 50, 2.0f, 0.7f }, { 0, 6500, -2.5f, 2.5f } },
          "Open, close to neutral: sub lift and a 6.5 kHz trim." },
        // ---- Audio-Technica
        { "Audio-Technica ATH-M50x", "Audio-Technica", false, { { 1, 30, 2.0f, 0.7f }, { 0, 110, -3.0f, 0.8f }, { 0, 4500, -2.0f, 2.0f }, { 0, 9000, -3.0f, 3.0f } },
          "Mid-bass hump and treble peaks at 4.5 / 9 kHz." },
        { "Audio-Technica ATH-M40x", "Audio-Technica", false, { { 1, 50, 2.0f, 0.7f }, { 0, 7000, -3.0f, 3.0f } },
          "Flatter than the M50x: sub lift and a 7 kHz trim." },
        { "Audio-Technica ATH-R70x", "Audio-Technica", true, { { 1, 60, 3.5f, 0.7f }, { 0, 3500, -1.0f, 1.5f }, { 0, 9000, 1.5f, 2.0f } },
          "Open reference: needs sub-bass." },
        // ---- Sony
        { "Sony MDR-7506 / V6", "Sony", false, { { 1, 40, 2.0f, 0.7f }, { 0, 5000, -3.0f, 2.5f }, { 0, 10000, -4.0f, 3.0f } },
          "Bright classic: 5 kHz and 10 kHz peaks are reduced." },
        { "Sony MDR-MV1", "Sony", true, { { 1, 40, 2.0f, 0.7f }, { 0, 6000, -2.0f, 2.0f } },
          "Open studio: sub lift and a gentle 6 kHz trim." },
        { "Sony MDR-M1", "Sony", false, { { 0, 120, -1.5f, 0.8f }, { 0, 7000, -2.0f, 2.5f } },
          "Closed studio: small trims." },
        // ---- Sennheiser
        { "Sennheiser HD 600", "Sennheiser", true, { { 1, 60, 4.0f, 0.7f }, { 0, 5000, 1.5f, 2.0f }, { 2, 11000, 2.0f, 0.7f } },
          "Classic open: rolled-off sub and a relaxed top end." },
        { "Sennheiser HD 650 / HD 6XX", "Sennheiser", true, { { 1, 60, 4.5f, 0.7f }, { 0, 6000, 2.0f, 2.0f }, { 2, 10000, 2.5f, 0.7f } },
          "Warm and dark: sub and treble are lifted." },
        { "Sennheiser HD 660S2", "Sennheiser", true, { { 1, 50, 3.0f, 0.7f }, { 0, 6000, 1.5f, 2.0f } },
          "Open: sub extension and a little presence." },
        { "Sennheiser HD 490 Pro", "Sennheiser", true, { { 1, 50, 2.0f, 0.7f }, { 0, 3000, 1.0f, 1.0f }, { 0, 7500, -1.5f, 2.5f } },
          "Open studio: close to neutral." },
        { "Sennheiser HD 280 Pro", "Sennheiser", false, { { 0, 4500, 3.0f, 1.5f }, { 2, 10000, 3.0f, 0.7f } },
          "Closed: dull top end and a 4-5 kHz hole." },
        { "Sennheiser HD 25", "Sennheiser", false, { { 0, 120, -3.0f, 1.0f }, { 0, 3500, -2.0f, 2.0f } },
          "DJ classic: mid-bass hump and forward upper mids." },
        // ---- AKG
        { "AKG K371", "AKG", false, { { 0, 5500, -2.0f, 3.0f } },
          "Close to the Harman curve: only a small 5.5 kHz trim." },
        { "AKG K702 / K712", "AKG", true, { { 1, 60, 5.0f, 0.7f }, { 0, 3000, -1.5f, 1.5f } },
          "Open, lean bass: needs a lot of sub." },
        { "AKG K240 MKII", "AKG", true, { { 1, 80, 5.0f, 0.7f }, { 0, 3000, 2.0f, 1.2f }, { 2, 10000, 2.0f, 0.7f } },
          "Semi-open: weak bass and treble." },
        // ---- others
        { "Focal Clear MG Pro", "Focal", true, { { 0, 3000, 2.0f, 1.0f }, { 0, 7000, -2.0f, 2.5f } },
          "Open: small presence dip and a 7 kHz peak." },
        { "Focal Listen Pro", "Focal", false, { { 0, 150, -1.5f, 0.8f }, { 0, 3000, 2.0f, 1.0f }, { 0, 8000, -2.0f, 3.0f } },
          "Closed: forward mid-bass, recessed presence." },
        { "Audeze LCD-X", "Audeze", true, { { 0, 3500, 2.0f, 1.2f }, { 2, 10000, 2.0f, 0.7f } },
          "Planar: flat bass, recessed 3-5 kHz and top end." },
        { "HiFiMan Sundara", "HiFiMan", true, { { 1, 40, 1.5f, 0.7f }, { 0, 3000, 1.5f, 1.2f }, { 0, 6000, -2.0f, 2.5f } },
          "Planar: flat bass, small dip and peak in the treble." },
        { "Shure SRH840A", "Shure", false, { { 1, 40, 2.0f, 0.7f }, { 0, 6000, -3.0f, 2.0f } },
          "Closed: 6 kHz peak." },
        { "Yamaha HPH-MT8", "Yamaha", false, { { 0, 100, -2.0f, 0.8f }, { 0, 6000, -3.0f, 2.0f } },
          "Closed: mid-bass and 6 kHz peaks." },
        { "KRK KNS 8400", "KRK", false, { { 1, 40, 1.5f, 0.7f }, { 0, 9000, -3.0f, 3.0f } },
          "Closed: 9 kHz peak." },
        { "Slate VSX / Steven Slate", "Slate", false, { { 0, 6000, -1.5f, 2.0f } },
          "Already tuned for virtual rooms: tiny trim." },
        // ---- consumer
        { "Apple AirPods Pro 2", "Apple", false, { { 1, 100, -1.5f, 0.7f }, { 0, 5000, 2.0f, 1.5f } },
          "Harman-like with a bass lift; presence slightly low." },
        { "Apple AirPods Max", "Apple", false, { { 1, 100, -2.0f, 0.7f }, { 0, 5000, 2.0f, 1.5f } },
          "Bass lift, a little dark in the presence." },
        { "Sony WH-1000XM5", "Sony", false, { { 1, 120, -5.0f, 0.7f }, { 0, 3000, 2.0f, 1.2f }, { 0, 7000, -2.0f, 2.5f } },
          "Strong bass boost: use the Sony app 'flat' setting too." },
        { "Beats Studio Pro", "Beats", false, { { 1, 100, -6.0f, 0.7f }, { 0, 2500, 2.0f, 1.0f } },
          "Heavy bass tuning." },
        { "Generic Open-Back", "", true, { { 1, 50, 3.0f, 0.7f } }, "Typical open-back: sub lift only." },
        { "Generic Closed-Back", "", false, { { 0, 100, -2.0f, 0.8f }, { 0, 8000, -2.0f, 3.0f } }, "Typical closed-back: mid-bass and 8 kHz trims." },
        { "Custom (Loaded EQ File)", "", false, {}, "Your own correction from an AutoEQ / ParametricEQ.txt file." },
    };
    return m;
}
inline int numHeadphones() { return (int) headphoneModels().size(); }
inline int customHeadphoneIndex() { return numHeadphones() - 1; }

// AutoEQ / Equalizer APO "ParametricEQ.txt":  "Filter 1: ON PK Fc 105 Hz Gain -3.1 dB Q 0.70",  "Preamp: -6.3 dB"
inline std::vector<PeqBand> parseParametricEq (const juce::String& text, juce::String& error)
{
    std::vector<PeqBand> out;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (! line.startsWithIgnoreCase ("Filter")) continue;
        auto tok = juce::StringArray::fromTokens (line.fromFirstOccurrenceOf (":", false, false), " \t", "");
        tok.removeEmptyStrings();
        if (tok.size() < 2 || ! tok[0].equalsIgnoreCase ("ON")) continue;
        const auto type = tok[1].toUpperCase();
        float fc = 0, g = 0, q = 0.707f;
        for (int i = 2; i + 1 < tok.size(); ++i)
        {
            if (tok[i].equalsIgnoreCase ("Fc")) fc = tok[i + 1].getFloatValue();
            else if (tok[i].equalsIgnoreCase ("Gain")) g = tok[i + 1].getFloatValue();
            else if (tok[i].equalsIgnoreCase ("Q")) q = tok[i + 1].getFloatValue();
        }
        if (fc <= 0) continue;
        int t = -1;
        if (type == "PK" || type == "PEQ") t = 0;
        else if (type.startsWith ("LS")) t = 1;
        else if (type.startsWith ("HS")) t = 2;
        else if (type == "LP" || type == "LPQ") t = 3;
        else if (type == "HP" || type == "HPQ") t = 4;
        if (t < 0) continue;
        out.push_back ({ t, fc, g, q > 0 ? q : 0.707f });
        if (out.size() >= 16) break;
    }
    if (out.empty()) error = "No filters found (expected lines like 'Filter 1: ON PK Fc 100 Hz Gain -3 dB Q 0.7')";
    return out;
}
}
