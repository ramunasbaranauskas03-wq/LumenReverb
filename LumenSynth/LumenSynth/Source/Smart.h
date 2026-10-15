#pragma once
#include "Parameters.h"
#include <functional>

// =====================================================================================
// Smart Assist for Lumen Synth
//   GENERATE: builds a complete, musically-sane patch for a category from sound-design rules
//             (table choice, unison, envelope shapes, filter moves, FX) with controlled randomness.
//   MUTATE:   nudges the current patch by `amount`, keeping structural choices mostly intact.
// =====================================================================================
namespace lumen::synth
{

using Values = std::vector<std::pair<juce::String, float>>;

inline float rr (juce::Random& r, float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); }
inline float gauss (juce::Random& r) { float s = 0; for (int i = 0; i < 4; ++i) s += r.nextFloat() - 0.5f; return s * 1.732f; }
inline bool chance (juce::Random& r, float p) { return r.nextFloat() < p; }
template <typename T> T pick (juce::Random& r, std::initializer_list<T> l) { return *(l.begin() + r.nextInt ((int) l.size())); }

inline Values generatePatch (int category, juce::Random& r)
{
    Values v;
    auto set = [&] (const char* id, float x) { v.push_back ({ id, x }); };
    int slot = 1;
    auto mod = [&] (int src, int dst, float amt)
    {
        if (slot > 8) return;
        const juce::String p = "m" + juce::String (slot++) + "_";
        v.push_back ({ p + "src", (float) src }); v.push_back ({ p + "dst", (float) dst }); v.push_back ({ p + "amt", amt });
    };
    auto fxSpace = [&] (float dlyP, float revP, float revMix)
    {
        if (chance (r, dlyP)) { set ("dly_on", 1); set ("dly_div", (float) pick (r, { 10, 9, 11, 7 })); set ("dly_mix", rr (r, 0.1f, 0.25f)); set ("dly_fb", rr (r, 0.25f, 0.5f)); set ("dly_tone", rr (r, 0.35f, 0.75f)); }
        if (chance (r, revP)) { set ("rev_on", 1); set ("rev_mix", revMix * rr (r, 0.8f, 1.2f)); set ("rev_size", rr (r, 0.5f, 0.9f)); set ("rev_decay", rr (r, 1.8f, 4.5f)); set ("rev_damp", rr (r, 0.3f, 0.7f)); }
    };

    switch (category)
    {
        case 0: // Bass
        {
            set ("voices", 1); set ("legato", 1); set ("glide", chance (r, 0.6f) ? rr (r, 0.02f, 0.07f) : 0.0f);
            const int t = pick (r, { 1, 0, 4, 5, 7, 8, 1, 4 });
            set ("a_table", (float) t); set ("a_pos", t == 0 ? pick (r, { 0.667f, 1.0f, 0.55f }) : rr (r, 0.0f, 0.6f));
            set ("a_uni", (float) pick (r, { 1, 2, 2, 3 })); set ("a_det", rr (r, 0.05f, 0.18f)); set ("a_width", rr (r, 0.1f, 0.4f)); set ("a_level", 0.9f);
            if (chance (r, 0.5f)) { set ("b_on", 1); set ("b_table", (float) pick (r, { 0, 1, 2 })); set ("b_pos", pick (r, { 0.0f, 1.0f, 0.667f })); set ("b_oct", -1); set ("b_level", rr (r, 0.45f, 0.7f)); }
            if (chance (r, 0.6f)) { set ("sub_on", 1); set ("sub_oct", 1); set ("sub_level", rr (r, 0.4f, 0.6f)); }
            set ("flt_type", (float) pick (r, { 0, 0, 2, 1 })); set ("flt_cut", rr (r, 300.0f, 1500.0f)); set ("flt_res", rr (r, 0.05f, 0.5f));
            set ("flt_drive", rr (r, 0.2f, 0.6f)); set ("flt_env", rr (r, 0.0f, 0.5f)); set ("flt_key", rr (r, 0.2f, 0.6f));
            set ("e2_a", 0); set ("e2_d", rr (r, 0.1f, 0.4f)); set ("e2_s", rr (r, 0.0f, 0.4f));
            set ("e1_a", 0.001f); set ("e1_d", rr (r, 0.3f, 1.0f)); set ("e1_s", rr (r, 0.4f, 1.0f)); set ("e1_r", rr (r, 0.05f, 0.15f));
            if (chance (r, 0.45f))
            {
                set ("l1_sync", 1); set ("l1_div", (float) pick (r, { 9, 12, 11, 6, 14 })); set ("l1_shape", (float) pick (r, { 0, 1, 3, 5 }));
                if (chance (r, 0.5f)) mod (1, 12, rr (r, 0.2f, 0.45f)); else mod (1, 1, rr (r, 0.25f, 0.6f));
                mod (6, 19, 0.5f);
            }
            if (chance (r, 0.35f)) { set ("e3_a", 0); set ("e3_d", rr (r, 0.05f, 0.15f)); set ("e3_s", 0); mod (4, pick (r, { 1, 11 }), rr (r, 0.1f, 0.4f)); }
            mod (9, 12, 0.4f);
            if (chance (r, 0.8f)) { set ("dist_on", 1); set ("dist_mode", (float) pick (r, { 0, 0, 1, 2 })); set ("dist_drive", rr (r, 0.2f, 0.6f)); }
            if (chance (r, 0.5f)) { set ("ott_on", 1); set ("ott_depth", rr (r, 0.2f, 0.5f)); }
            break;
        }
        case 1: // Lead
        {
            set ("voices", (float) pick (r, { 6, 8, 1 })); set ("glide", chance (r, 0.3f) ? rr (r, 0.02f, 0.08f) : 0.0f);
            const int t = pick (r, { 1, 1, 0, 4, 2, 3 });
            set ("a_table", (float) t); set ("a_pos", t == 0 ? pick (r, { 0.667f, 1.0f }) : rr (r, 0.0f, 0.5f));
            set ("a_uni", (float) pick (r, { 5, 7, 9 })); set ("a_det", rr (r, 0.2f, 0.4f)); set ("a_width", rr (r, 0.7f, 1.0f));
            if (chance (r, 0.7f)) { set ("b_on", 1); set ("b_table", (float) pick (r, { 1, 0, 2, 4 })); set ("b_pos", rr (r, 0.0f, 1.0f)); set ("b_oct", (float) pick (r, { 0, 1, -1 }));
                                     set ("b_uni", (float) pick (r, { 1, 3, 5 })); set ("b_det", rr (r, 0.15f, 0.3f)); set ("b_level", rr (r, 0.3f, 0.6f)); }
            set ("flt_type", 2); set ("flt_cut", rr (r, 3000.0f, 9000.0f)); set ("flt_res", rr (r, 0.0f, 0.2f));
            set ("e1_a", rr (r, 0.001f, 0.01f)); set ("e1_d", rr (r, 0.3f, 0.7f)); set ("e1_s", rr (r, 0.7f, 0.95f)); set ("e1_r", rr (r, 0.2f, 0.4f));
            set ("l1_sync", 0); set ("l1_rate", rr (r, 4.5f, 6.0f)); mod (6, 11, 0.01f);
            mod (9, 12, -0.35f);
            if (chance (r, 0.4f)) { set ("dist_on", 1); set ("dist_mode", 0); set ("dist_drive", rr (r, 0.15f, 0.4f)); }
            if (chance (r, 0.6f)) { set ("ott_on", 1); set ("ott_depth", rr (r, 0.25f, 0.55f)); }
            if (chance (r, 0.3f)) { set ("pump_on", 1); set ("pump_depth", rr (r, 0.35f, 0.6f)); }
            fxSpace (0.6f, 0.75f, 0.22f);
            break;
        }
        case 2: // Pluck
        {
            set ("voices", 8);
            set ("a_table", (float) pick (r, { 1, 1, 0, 5, 2 })); set ("a_pos", rr (r, 0.0f, 0.4f));
            set ("a_uni", (float) pick (r, { 3, 5, 7 })); set ("a_det", rr (r, 0.15f, 0.3f));
            if (chance (r, 0.6f)) { set ("b_on", 1); set ("b_table", 0); set ("b_pos", pick (r, { 0.333f, 1.0f, 0.0f })); set ("b_oct", 1); set ("b_level", rr (r, 0.3f, 0.5f)); }
            set ("flt_type", (float) pick (r, { 0, 2 })); set ("flt_cut", rr (r, 300.0f, 800.0f)); set ("flt_res", rr (r, 0.1f, 0.4f));
            set ("flt_env", rr (r, 0.45f, 0.7f)); set ("flt_key", 0.3f);
            set ("e2_a", 0); set ("e2_d", rr (r, 0.1f, 0.4f)); set ("e2_s", 0);
            set ("e1_a", 0.001f); set ("e1_d", rr (r, 0.2f, 0.6f)); set ("e1_s", 0); set ("e1_r", rr (r, 0.2f, 0.4f));
            mod (9, 12, 0.35f); mod (5, 12, 0.15f);
            if (chance (r, 0.4f)) { set ("ott_on", 1); set ("ott_depth", rr (r, 0.3f, 0.5f)); }
            fxSpace (0.75f, 0.85f, 0.28f);
            break;
        }
        case 3: // Pad
        {
            set ("voices", 12);
            set ("a_table", (float) pick (r, { 1, 3, 6, 7, 2 })); set ("a_pos", rr (r, 0.1f, 0.7f));
            set ("a_uni", (float) pick (r, { 5, 7, 9 })); set ("a_det", rr (r, 0.2f, 0.35f)); set ("a_width", 1);
            set ("b_on", 1); set ("b_table", (float) pick (r, { 3, 1, 6, 8 })); set ("b_pos", rr (r, 0.2f, 0.8f)); set ("b_uni", 3); set ("b_level", rr (r, 0.3f, 0.5f));
            set ("b_oct", (float) pick (r, { 0, 1, -1 }));
            set ("flt_type", 2); set ("flt_cut", rr (r, 1500.0f, 5000.0f)); set ("flt_res", rr (r, 0.05f, 0.25f));
            set ("e1_a", rr (r, 0.3f, 1.5f)); set ("e1_d", 1.0f); set ("e1_s", rr (r, 0.8f, 1.0f)); set ("e1_r", rr (r, 1.5f, 4.0f));
            set ("l1_sync", 0); set ("l1_rate", rr (r, 0.05f, 0.3f)); set ("l1_retrig", 0); mod (1, 1, rr (r, 0.15f, 0.35f));
            set ("l2_sync", 0); set ("l2_rate", rr (r, 0.05f, 0.2f)); set ("l2_retrig", 0); mod (2, 12, rr (r, 0.08f, 0.2f));
            mod (9, 12, 0.4f);
            if (chance (r, 0.6f)) { set ("cho_on", 1); set ("cho_mix", rr (r, 0.25f, 0.45f)); }
            if (chance (r, 0.35f)) { set ("pump_on", 1); set ("pump_depth", rr (r, 0.6f, 0.85f)); }
            set ("rev_on", 1); set ("rev_mix", rr (r, 0.35f, 0.5f)); set ("rev_decay", rr (r, 4.0f, 8.0f)); set ("rev_size", rr (r, 0.75f, 0.95f));
            break;
        }
        case 4: // Keys
        {
            set ("voices", 12);
            set ("a_table", (float) pick (r, { 7, 1, 2, 0 })); set ("a_pos", rr (r, 0.2f, 0.8f));
            set ("a_uni", (float) pick (r, { 1, 3, 5 })); set ("a_det", rr (r, 0.1f, 0.25f));
            if (chance (r, 0.5f)) { set ("b_on", 1); set ("b_table", (float) pick (r, { 2, 0, 3 })); set ("b_pos", rr (r, 0.2f, 0.6f)); set ("b_level", rr (r, 0.3f, 0.5f)); }
            set ("flt_type", (float) pick (r, { 1, 2 })); set ("flt_cut", rr (r, 1800.0f, 6000.0f)); set ("flt_env", rr (r, 0.1f, 0.35f));
            set ("e2_d", rr (r, 0.2f, 0.5f)); set ("e2_s", 0);
            set ("e1_a", 0.002f); set ("e1_d", rr (r, 0.3f, 0.8f)); set ("e1_s", rr (r, 0.0f, 0.4f)); set ("e1_r", rr (r, 0.1f, 0.3f));
            mod (9, 12, 0.35f);
            if (chance (r, 0.5f)) { set ("ott_on", 1); set ("ott_depth", rr (r, 0.25f, 0.45f)); }
            if (chance (r, 0.5f)) { set ("pump_on", 1); set ("pump_depth", rr (r, 0.4f, 0.7f)); }
            if (chance (r, 0.4f)) { set ("cho_on", 1); set ("cho_mix", rr (r, 0.2f, 0.35f)); }
            fxSpace (0.3f, 0.8f, 0.2f);
            break;
        }
        default: // FX
        {
            set ("voices", 4);
            const int kind = r.nextInt (3);
            set ("a_table", (float) pick (r, { 0, 1, 5, 8 })); set ("a_pos", rr (r, 0.0f, 0.6f)); set ("a_uni", (float) pick (r, { 1, 3, 7 })); set ("a_det", rr (r, 0.1f, 0.4f));
            if (kind == 0)      { set ("e3_a", 0); set ("e3_d", rr (r, 0.1f, 0.5f)); set ("e3_s", 0); mod (4, 11, rr (r, 0.5f, 1.0f)); set ("e1_d", 0.5f); set ("e1_s", 0); set ("flt_on", 0); }
            else if (kind == 1) { set ("noise_on", 1); set ("noise_level", rr (r, 0.5f, 0.9f)); set ("flt_type", 3); set ("flt_cut", 250); mod (9, 12, 0.75f); mod (9, 11, 0.5f); set ("e1_s", 1); set ("e1_r", 1.5f); }
            else                { set ("l1_sync", 1); set ("l1_div", (float) pick (r, { 12, 14, 15 })); set ("l1_shape", 6); mod (1, 11, rr (r, 0.2f, 0.5f)); mod (1, 12, 0.3f); set ("flt_type", 5); set ("flt_cut", 1500); set ("flt_res", 0.5f); }
            fxSpace (0.8f, 0.9f, 0.35f);
            break;
        }
    }
    return v;
}

inline juce::String generatedName (int category, juce::Random& r)
{
    static const char* adj[] = { "Neon", "Laser", "Solar", "Prism", "Hyper", "Cyber", "Crystal", "Midnight", "Electric", "Quantum", "Velvet", "Chrome", "Arcade", "Nova", "Ultra" };
    static const char* noun[6][5] = { { "Bass", "Sub", "Growl", "Wobble", "Reese" }, { "Lead", "Anthem", "Saw", "Screech", "Hook" },
                                      { "Pluck", "Stab", "Drop", "Ping", "Spark" }, { "Pad", "Atmos", "Dream", "Haze", "Cloud" },
                                      { "Keys", "Chords", "Organ", "Stab", "Piano" }, { "Riser", "Zap", "Sweep", "Impact", "Glitch" } };
    return juce::String (adj[r.nextInt (15)]) + " " + noun[juce::jlimit (0, 5, category)][r.nextInt (5)] + " " + juce::String (r.nextInt (90) + 10);
}

inline float mutateValue (const Spec& s, float cur, float norm, float amount, juce::Random& r, const std::function<float (float)>& fromNorm)
{
    const juce::String& id = s.id;
    // never touched: structure, routing, level and performance settings
    if (id == "vol" || id.startsWith ("macro") || id.endsWith ("_src") || id.endsWith ("_dst") || id.endsWith ("_on")
        || id == "voices" || id == "legato" || id == "bend" || id == "velsens" || id == "clip" || id.endsWith ("_sync")
        || id.endsWith ("_retrig") || id.endsWith ("_oct") || id.endsWith ("_semi") || id == "sub_oct" || id.endsWith ("_div")
        || id == "dly_ping" || id == "dist_mode")
        return cur;
    switch (s.kind)
    {
        case Kind::Float:
        {
            float n = norm + gauss (r) * 0.16f * amount;
            if (id.endsWith ("_amt") && std::abs (cur) < 1.0e-6f) return cur;   // don't wake unused slots
            if (id.endsWith ("_mix") || id == "rev_decay") n = norm + gauss (r) * 0.08f * amount;
            return fromNorm (juce::jlimit (0.0f, 1.0f, n));
        }
        case Kind::Int:
            if (id.endsWith ("_uni") && cur > 1.5f && chance (r, 0.4f * amount))
                return juce::jlimit (2.0f, 16.0f, cur + (float) (r.nextBool() ? 2 : -2));
            return cur;
        case Kind::Choice:
            if ((id.endsWith ("_table") || id.endsWith ("_warpm") || id.endsWith ("_shape")) && chance (r, 0.18f * amount))
                return (float) r.nextInt ((int) s.hi + 1);
            return cur;
        case Kind::Bool:
        default: return cur;
    }
}

} // namespace lumen::synth
