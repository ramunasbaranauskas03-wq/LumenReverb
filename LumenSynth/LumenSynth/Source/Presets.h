#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <utility>

// =====================================================================================
// Factory presets (values in real parameter units; anything not listed = default).
// Tables: 0 Basic(0 sine,.333 tri,.667 saw,1 square) 1 SawStack 2 PWM 3 Vox 4 HardSync
//         5 FMGrowl 6 PhaseRes 7 Organ 8 Fold
// Filter: 0 Ladder 1 LP12 2 LP24 3 HP12 4 HP24 5 Band 6 Notch
// Div: 0 4/1 1 2/1 2 1/1 3 1/2 4 1/2D 5 1/2T 6 1/4 7 1/4D 8 1/4T 9 1/8 10 1/8D 11 1/8T 12 1/16 13 1/16D 14 1/16T 15 1/32
// Mod src: 1-4 LFO1-4 5 Env1 6 Env2 7 Env3 8 Vel 9 MW 10 AT 11 Key 12-15 Macro1-4 16 Rnd 17 Bend
// Mod dst: osc A 1-6 (pos,pitch,level,det,warp,pan) B 7-12 C 13-18; 19 Pitch 20 Cutoff 21 Res 22 Drive 23 F2Cut 24 F2Res
//          25 Sub 26 Noise 27 Pan 28 Volume 29-32 LFO rates 33 Width 34 FltEnv 35.. FX
// =====================================================================================
namespace lumen::synth
{

struct Preset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> v;
};

inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> p
    {
        { "Init Saw", "Lead", { { "vol", -4.0f }, { "a_table", 1 }, { "e1_r", 0.2f } } },

        // ------------------------------------------------------------------ BASS
        { "Electro Saw Bass", "Bass", { { "vol", -1.0f },
            { "a_table", 1 }, { "a_uni", 2 }, { "a_det", 0.12f }, { "a_level", 0.85f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 1 }, { "b_oct", -1 }, { "b_level", 0.7f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.035f },
            { "flt_type", 0 }, { "flt_cut", 700 }, { "flt_res", 0.35f }, { "flt_drive", 0.6f }, { "flt_env", 0.32f }, { "flt_key", 0.4f },
            { "e2_a", 0 }, { "e2_d", 0.28f }, { "e2_s", 0.3f }, { "e1_r", 0.08f },
            { "dist_on", 1 }, { "dist_mode", 1 }, { "dist_drive", 0.5f }, { "ott_on", 1 }, { "ott_depth", 0.3f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", 0.4f }, { "m2_src", 8 }, { "m2_dst", 20 }, { "m2_amt", 0.15f } } },

        { "Distorted Fidget Bass", "Bass", { { "vol", 1.0f },
            { "a_table", 4 }, { "a_pos", 0.25f }, { "a_level", 0.9f }, { "a_uni", 2 }, { "a_det", 0.08f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.02f },
            { "l1_sync", 1 }, { "l1_div", 12 }, { "l1_shape", 3 },
            { "m1_src", 1 }, { "m1_dst", 1 }, { "m1_amt", 0.35f }, { "m2_src", 1 }, { "m2_dst", 20 }, { "m2_amt", 0.2f },
            { "m3_src", 12 }, { "m3_dst", 29 }, { "m3_amt", 0.25f },
            { "flt_type", 2 }, { "flt_cut", 1400 }, { "flt_res", 0.4f }, { "flt_drive", 0.3f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.6f }, { "ott_on", 1 }, { "ott_depth", 0.45f } } },

        { "Reese Bass", "Bass", { { "vol", -7.5f },
            { "a_table", 1 }, { "a_uni", 4 }, { "a_det", 0.2f }, { "a_width", 0.3f },
            { "b_on", 1 }, { "b_table", 1 }, { "b_uni", 4 }, { "b_det", 0.28f }, { "b_fine", 14 }, { "b_width", 0.4f },
            { "sub_on", 1 }, { "sub_oct", 1 }, { "sub_level", 0.55f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.06f },
            { "flt_type", 0 }, { "flt_cut", 420 }, { "flt_res", 0.22f }, { "flt_drive", 0.35f },
            { "l1_sync", 0 }, { "l1_rate", 0.18f }, { "l1_mode", 0 },
            { "m1_src", 1 }, { "m1_dst", 10 }, { "m1_amt", 0.25f }, { "m2_src", 12 }, { "m2_dst", 20 }, { "m2_amt", 0.5f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.25f }, { "e1_r", 0.12f } } },

        { "Sub 808", "Bass", { { "vol", -8.5f },
            { "a_table", 0 }, { "a_pos", 0.08f }, { "a_level", 1 }, { "a_rand", 0 },
            { "voices", 1 }, { "glide", 0.05f },
            { "flt_on", 0 }, { "e1_a", 0.001f }, { "e1_d", 2.6f }, { "e1_s", 0 }, { "e1_r", 0.25f },
            { "e3_a", 0 }, { "e3_d", 0.07f }, { "e3_s", 0 },
            { "m1_src", 7 }, { "m1_dst", 19 }, { "m1_amt", 0.5f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.35f } } },

        { "Growl Bass", "Bass", { { "vol", 1.5f },
            { "a_table", 5 }, { "a_pos", 0.35f }, { "a_level", 0.9f }, { "a_uni", 2 }, { "a_det", 0.05f },
            { "b_on", 1 }, { "b_table", 3 }, { "b_pos", 0.2f }, { "b_level", 0.55f },
            { "sub_on", 1 }, { "sub_oct", 1 }, { "sub_level", 0.5f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.04f },
            { "l1_sync", 1 }, { "l1_div", 9 }, { "l1_shape", 1 },
            { "m1_src", 1 }, { "m1_dst", 1 }, { "m1_amt", 0.4f }, { "m2_src", 1 }, { "m2_dst", 7 }, { "m2_amt", 0.7f },
            { "m3_src", 1 }, { "m3_dst", 20 }, { "m3_amt", 0.2f }, { "m4_src", 9 }, { "m4_dst", 29 }, { "m4_amt", 0.5f },
            { "flt_type", 0 }, { "flt_cut", 1500 }, { "flt_res", 0.3f }, { "flt_drive", 0.4f },
            { "dist_on", 1 }, { "dist_mode", 2 }, { "dist_drive", 0.35f }, { "ott_on", 1 }, { "ott_depth", 0.6f } } },

        { "Wobble Bass", "Bass", { { "vol", 1.5f },
            { "a_table", 1 }, { "a_uni", 3 }, { "a_det", 0.12f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 1 }, { "b_oct", -1 }, { "b_level", 0.6f },
            { "sub_on", 1 }, { "sub_oct", 1 }, { "sub_level", 0.45f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.03f },
            { "l1_sync", 1 }, { "l1_div", 9 }, { "l1_shape", 0 },
            { "flt_type", 0 }, { "flt_cut", 300 }, { "flt_res", 0.5f }, { "flt_drive", 0.45f },
            { "m1_src", 1 }, { "m1_dst", 20 }, { "m1_amt", 0.42f }, { "m2_src", 9 }, { "m2_dst", 29 }, { "m2_amt", 0.5f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.45f }, { "ott_on", 1 }, { "ott_depth", 0.4f } } },

        { "Acid 303 Line", "Bass", { { "vol", -7.5f },
            { "a_table", 0 }, { "a_pos", 0.667f }, { "a_level", 0.9f }, { "a_rand", 0 },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.06f },
            { "flt_type", 0 }, { "flt_cut", 380 }, { "flt_res", 0.82f }, { "flt_drive", 0.35f }, { "flt_env", 0.5f },
            { "e2_a", 0 }, { "e2_d", 0.22f }, { "e2_s", 0 }, { "e1_s", 0.8f }, { "e1_r", 0.05f },
            { "m1_src", 8 }, { "m1_dst", 20 }, { "m1_amt", 0.22f }, { "m2_src", 12 }, { "m2_dst", 20 }, { "m2_amt", 0.45f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.3f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.14f }, { "dly_fb", 0.3f } } },

        { "Deep House Organ Bass", "Bass", { { "vol", -1.5f },
            { "a_table", 7 }, { "a_pos", 0.3f }, { "a_level", 0.9f }, { "a_rand", 0 },
            { "voices", 1 }, { "glide", 0.0f },
            { "flt_type", 1 }, { "flt_cut", 1800 }, { "flt_res", 0.1f }, { "flt_env", 0.2f },
            { "e2_d", 0.25f }, { "e2_s", 0 }, { "e1_a", 0.001f }, { "e1_d", 0.35f }, { "e1_s", 0.25f }, { "e1_r", 0.08f } } },

        { "Slap House Bass", "Bass", { { "vol", 0.0f },
            { "a_table", 4 }, { "a_pos", 0.3f }, { "a_level", 0.85f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 0.1f }, { "b_oct", -1 }, { "b_level", 0.6f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.03f },
            { "e3_a", 0 }, { "e3_d", 0.12f }, { "e3_s", 0 },
            { "m1_src", 7 }, { "m1_dst", 1 }, { "m1_amt", 0.45f },
            { "flt_type", 0 }, { "flt_cut", 650 }, { "flt_res", 0.3f }, { "flt_env", 0.45f }, { "e2_d", 0.2f }, { "e2_s", 0.2f },
            { "e1_d", 0.5f }, { "e1_s", 0.55f }, { "e1_r", 0.07f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.3f }, { "ott_on", 1 }, { "ott_depth", 0.3f } } },

        { "Bounce Bass", "Bass", { { "vol", 1.0f },
            { "a_table", 0 }, { "a_pos", 0.5f }, { "a_level", 0.9f },
            { "voices", 1 }, { "e3_a", 0 }, { "e3_d", 0.06f }, { "e3_s", 0 },
            { "m1_src", 7 }, { "m1_dst", 19 }, { "m1_amt", 0.25f },
            { "flt_type", 0 }, { "flt_cut", 900 }, { "flt_res", 0.25f }, { "flt_env", 0.3f }, { "e2_d", 0.15f }, { "e2_s", 0 },
            { "e1_d", 0.22f }, { "e1_s", 0 }, { "e1_r", 0.08f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.4f } } },

        { "Big Room Kick", "FX", { { "vol", -1.0f },
            { "a_table", 0 }, { "a_pos", 0.0f }, { "a_level", 1 }, { "a_rand", 0 },
            { "voices", 1 }, { "flt_on", 0 },
            { "e1_a", 0.0006f }, { "e1_d", 0.45f }, { "e1_s", 0 }, { "e1_r", 0.1f },
            { "e3_a", 0 }, { "e3_d", 0.055f }, { "e3_s", 0 },
            { "noise_on", 1 }, { "noise_level", 0.0f }, { "noise_color", 0.7f },
            { "m1_src", 7 }, { "m1_dst", 19 }, { "m1_amt", 1.0f }, { "m2_src", 7 }, { "m2_dst", 26 }, { "m2_amt", 0.5f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.55f }, { "ott_on", 1 }, { "ott_depth", 0.25f } } },

        // ------------------------------------------------------------------ LEADS
        { "Anthem Supersaw", "Lead", { { "vol", -0.5f },
            { "a_table", 1 }, { "a_uni", 9 }, { "a_det", 0.32f }, { "a_blend", 0.8f }, { "a_width", 1 },
            { "b_on", 1 }, { "b_table", 1 }, { "b_pos", 0.25f }, { "b_oct", 1 }, { "b_uni", 7 }, { "b_det", 0.22f }, { "b_level", 0.5f },
            { "voices", 8 }, { "flt_type", 2 }, { "flt_cut", 9000 }, { "flt_res", 0.08f },
            { "e1_a", 0.004f }, { "e1_d", 0.6f }, { "e1_s", 0.85f }, { "e1_r", 0.35f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", -0.4f }, { "m2_src", 9 }, { "m2_dst", 4 }, { "m2_amt", 0.3f },
            { "ott_on", 1 }, { "ott_depth", 0.35f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.14f }, { "rev_on", 1 }, { "rev_mix", 0.24f }, { "rev_size", 0.7f }, { "rev_decay", 3.0f } } },

        { "Trance Supersaw", "Lead", { { "vol", -6.5f },
            { "a_table", 1 }, { "a_uni", 9 }, { "a_det", 0.38f }, { "a_blend", 0.85f }, { "a_width", 1 },
            { "b_on", 1 }, { "b_table", 2 }, { "b_pos", 0.2f }, { "b_uni", 5 }, { "b_det", 0.3f }, { "b_level", 0.45f },
            { "voices", 8 }, { "flt_type", 2 }, { "flt_cut", 6500 }, { "flt_res", 0.12f },
            { "e1_a", 0.006f }, { "e1_d", 0.5f }, { "e1_s", 0.9f }, { "e1_r", 0.4f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", 0.35f },
            { "pump_on", 1 }, { "pump_div", 2 }, { "pump_depth", 0.6f }, { "pump_curve", 0.55f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.16f }, { "dly_fb", 0.4f },
            { "rev_on", 1 }, { "rev_mix", 0.3f }, { "rev_size", 0.8f }, { "rev_decay", 4.0f } } },

        { "Future Rave Lead", "Lead", { { "vol", 0.0f },
            { "a_table", 1 }, { "a_pos", 0.4f }, { "a_uni", 7 }, { "a_det", 0.3f },
            { "b_on", 1 }, { "b_table", 4 }, { "b_pos", 0.45f }, { "b_uni", 3 }, { "b_det", 0.2f }, { "b_level", 0.65f },
            { "voices", 6 }, { "glide", 0.02f },
            { "flt_type", 0 }, { "flt_cut", 3200 }, { "flt_res", 0.25f }, { "flt_drive", 0.3f }, { "flt_env", 0.22f },
            { "e2_d", 0.3f }, { "e2_s", 0.4f }, { "e1_d", 0.4f }, { "e1_s", 0.8f }, { "e1_r", 0.2f },
            { "m1_src", 12 }, { "m1_dst", 7 }, { "m1_amt", 0.5f }, { "m2_src", 12 }, { "m2_dst", 20 }, { "m2_amt", 0.25f },
            { "m3_src", 9 }, { "m3_dst", 4 }, { "m3_amt", 0.4f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.35f }, { "ott_on", 1 }, { "ott_depth", 0.5f },
            { "pump_on", 1 }, { "pump_depth", 0.4f }, { "rev_on", 1 }, { "rev_mix", 0.18f } } },

        { "Big Room Lead", "Lead", { { "vol", -0.5f },
            { "a_table", 0 }, { "a_pos", 1 }, { "a_uni", 5 }, { "a_det", 0.25f },
            { "b_on", 1 }, { "b_table", 1 }, { "b_uni", 7 }, { "b_det", 0.35f }, { "b_level", 0.7f },
            { "voices", 8 }, { "flt_type", 2 }, { "flt_cut", 7000 },
            { "e1_a", 0.001f }, { "e1_d", 0.35f }, { "e1_s", 0.6f }, { "e1_r", 0.2f },
            { "e3_a", 0 }, { "e3_d", 0.08f }, { "e3_s", 0 }, { "m1_src", 7 }, { "m1_dst", 19 }, { "m1_amt", 0.04f },
            { "dist_on", 1 }, { "dist_mode", 1 }, { "dist_drive", 0.25f }, { "dist_mix", 0.6f }, { "ott_on", 1 }, { "ott_depth", 0.55f },
            { "rev_on", 1 }, { "rev_mix", 0.2f } } },

        { "Melbourne Bounce Lead", "Lead", { { "vol", 4.0f },
            { "a_table", 0 }, { "a_pos", 0.55f }, { "a_uni", 2 }, { "a_det", 0.06f },
            { "voices", 4 }, { "e3_a", 0 }, { "e3_d", 0.05f }, { "e3_s", 0 },
            { "m1_src", 7 }, { "m1_dst", 19 }, { "m1_amt", 0.3f },
            { "flt_type", 1 }, { "flt_cut", 1200 }, { "flt_res", 0.55f }, { "flt_env", 0.35f }, { "e2_d", 0.12f }, { "e2_s", 0 },
            { "e1_d", 0.18f }, { "e1_s", 0 }, { "e1_r", 0.1f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.4f }, { "rev_on", 1 }, { "rev_mix", 0.15f } } },

        { "Sync Screech Lead", "Lead", { { "vol", -1.5f },
            { "a_table", 4 }, { "a_pos", 0.1f }, { "a_uni", 3 }, { "a_det", 0.12f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.05f },
            { "e3_a", 0.35f }, { "e3_d", 0.6f }, { "e3_s", 0.5f },
            { "m1_src", 7 }, { "m1_dst", 1 }, { "m1_amt", 0.6f }, { "m2_src", 9 }, { "m2_dst", 1 }, { "m2_amt", 0.3f },
            { "flt_type", 2 }, { "flt_cut", 8000 },
            { "dist_on", 1 }, { "dist_mode", 1 }, { "dist_drive", 0.3f }, { "ott_on", 1 }, { "ott_depth", 0.5f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.18f } } },

        { "Euphoric Saw Lead", "Lead", { { "vol", -5.0f },
            { "a_table", 1 }, { "a_uni", 7 }, { "a_det", 0.28f }, { "a_width", 0.9f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 0.4f }, { "b_semi", 7 }, { "b_uni", 3 }, { "b_level", 0.35f },
            { "voices", 8 }, { "flt_type", 2 }, { "flt_cut", 5000 },
            { "l1_sync", 0 }, { "l1_rate", 5.5f }, { "l1_mode", 1 }, { "e3_a", 0.6f }, { "e3_d", 1.0f }, { "e3_s", 1 },
            { "m1_src", 1 }, { "m1_dst", 19 }, { "m1_amt", 0.006f }, { "m2_src", 9 }, { "m2_dst", 19 }, { "m2_amt", 0.01f },
            { "e1_a", 0.01f }, { "e1_s", 0.9f }, { "e1_r", 0.4f },
            { "cho_on", 1 }, { "cho_mix", 0.3f }, { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.18f },
            { "rev_on", 1 }, { "rev_mix", 0.28f }, { "rev_size", 0.75f } } },

        { "Vox Chop Lead", "Lead", { { "vol", 2.0f },
            { "a_table", 3 }, { "a_pos", 0.15f }, { "a_uni", 3 }, { "a_det", 0.1f },
            { "voices", 6 }, { "l1_sync", 1 }, { "l1_div", 9 }, { "l1_shape", 3 },
            { "m1_src", 1 }, { "m1_dst", 1 }, { "m1_amt", 0.35f }, { "m2_src", 12 }, { "m2_dst", 1 }, { "m2_amt", 0.5f },
            { "flt_type", 2 }, { "flt_cut", 9000 }, { "e1_d", 0.3f }, { "e1_s", 0.7f }, { "e1_r", 0.15f },
            { "ott_on", 1 }, { "ott_depth", 0.3f }, { "rev_on", 1 }, { "rev_mix", 0.22f } } },

        { "Hoover", "Lead", { { "vol", -5.0f },
            { "a_table", 1 }, { "a_pos", 0.5f }, { "a_uni", 7 }, { "a_det", 0.45f },
            { "b_on", 1 }, { "b_table", 2 }, { "b_pos", 0.6f }, { "b_oct", -1 }, { "b_uni", 4 }, { "b_det", 0.35f }, { "b_level", 0.6f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.12f },
            { "e3_a", 0 }, { "e3_d", 0.25f }, { "e3_s", 0 }, { "m1_src", 7 }, { "m1_dst", 19 }, { "m1_amt", -0.08f },
            { "l1_sync", 0 }, { "l1_rate", 0.4f }, { "m2_src", 1 }, { "m2_dst", 7 }, { "m2_amt", 0.3f },
            { "flt_type", 2 }, { "flt_cut", 5000 }, { "cho_on", 1 }, { "cho_mix", 0.5f }, { "cho_depth", 0.7f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.2f } } },

        // ------------------------------------------------------------------ PLUCKS
        { "Progressive House Pluck", "Pluck", { { "vol", 3.5f },
            { "a_table", 1 }, { "a_uni", 5 }, { "a_det", 0.2f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 0.333f }, { "b_oct", 1 }, { "b_level", 0.5f },
            { "voices", 8 }, { "flt_type", 0 }, { "flt_cut", 550 }, { "flt_res", 0.2f }, { "flt_env", 0.55f }, { "flt_key", 0.3f },
            { "e2_a", 0 }, { "e2_d", 0.35f }, { "e2_s", 0 }, { "e1_d", 0.45f }, { "e1_s", 0 }, { "e1_r", 0.3f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", 0.35f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.25f }, { "dly_fb", 0.4f }, { "rev_on", 1 }, { "rev_mix", 0.3f } } },

        { "Big Room Pluck", "Pluck", { { "vol", 6.0f },
            { "a_table", 1 }, { "a_uni", 7 }, { "a_det", 0.3f },
            { "voices", 8 }, { "flt_type", 2 }, { "flt_cut", 1400 }, { "flt_env", 0.42f },
            { "e2_a", 0 }, { "e2_d", 0.25f }, { "e2_s", 0 }, { "e1_d", 0.45f }, { "e1_s", 0.18f }, { "e1_r", 0.2f },
            { "ott_on", 1 }, { "ott_depth", 0.35f }, { "rev_on", 1 }, { "rev_mix", 0.25f }, { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.16f }, { "dly_fb", 0.35f } } },

        { "Trance Pluck", "Pluck", { { "vol", 6.0f },
            { "a_table", 1 }, { "a_uni", 5 }, { "a_det", 0.25f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 1 }, { "b_oct", 1 }, { "b_level", 0.4f },
            { "voices", 8 }, { "flt_type", 0 }, { "flt_cut", 350 }, { "flt_res", 0.35f }, { "flt_env", 0.65f },
            { "e2_a", 0 }, { "e2_d", 0.15f }, { "e2_s", 0 }, { "e1_d", 0.25f }, { "e1_s", 0 }, { "e1_r", 0.25f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_fb", 0.45f }, { "dly_mix", 0.3f },
            { "rev_on", 1 }, { "rev_mix", 0.3f }, { "rev_size", 0.8f } } },

        { "Pluck Bell", "Pluck", { { "vol", -4.0f },
            { "a_table", 5 }, { "a_pos", 0.15f }, { "a_uni", 2 }, { "a_det", 0.05f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 0 }, { "b_oct", 2 }, { "b_level", 0.35f },
            { "voices", 8 }, { "flt_on", 0 }, { "e1_d", 0.9f }, { "e1_s", 0 }, { "e1_r", 0.6f },
            { "e3_a", 0 }, { "e3_d", 0.3f }, { "e3_s", 0 }, { "m1_src", 7 }, { "m1_dst", 1 }, { "m1_amt", 0.3f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.2f }, { "rev_on", 1 }, { "rev_mix", 0.3f } } },

        // ------------------------------------------------------------------ KEYS / CHORDS
        { "Chord Stab", "Keys", { { "vol", 3.5f },
            { "a_table", 1 }, { "a_uni", 5 }, { "a_det", 0.22f },
            { "b_on", 1 }, { "b_table", 2 }, { "b_pos", 0.3f }, { "b_uni", 3 }, { "b_level", 0.5f },
            { "voices", 12 }, { "flt_type", 2 }, { "flt_cut", 2400 }, { "flt_env", 0.3f }, { "e2_d", 0.3f }, { "e2_s", 0 },
            { "e1_d", 0.4f }, { "e1_s", 0 }, { "e1_r", 0.15f },
            { "ott_on", 1 }, { "ott_depth", 0.4f }, { "rev_on", 1 }, { "rev_mix", 0.2f } } },

        { "House Organ", "Keys", { { "vol", -3.0f },
            { "a_table", 7 }, { "a_pos", 0.75f }, { "a_rand", 0 },
            { "voices", 12 }, { "flt_type", 1 }, { "flt_cut", 6000 },
            { "e1_d", 0.4f }, { "e1_s", 0.35f }, { "e1_r", 0.12f },
            { "cho_on", 1 }, { "cho_mix", 0.25f }, { "rev_on", 1 }, { "rev_mix", 0.18f } } },

        { "Future Bass Chords", "Keys", { { "vol", -7.5f },
            { "a_table", 1 }, { "a_uni", 7 }, { "a_det", 0.35f }, { "a_width", 1 },
            { "b_on", 1 }, { "b_table", 3 }, { "b_pos", 0.4f }, { "b_uni", 3 }, { "b_level", 0.4f },
            { "voices", 12 }, { "flt_type", 2 }, { "flt_cut", 4500 }, { "e1_a", 0.01f }, { "e1_s", 0.9f }, { "e1_r", 0.3f },
            { "m2_src", 12 }, { "m2_dst", 20 }, { "m2_amt", 0.35f }, { "m3_src", 9 }, { "m3_dst", 4 }, { "m3_amt", 0.3f },
            { "cho_on", 1 }, { "cho_mix", 0.35f }, { "pump_on", 1 }, { "pump_depth", 0.75f }, { "pump_curve", 0.6f },
            { "rev_on", 1 }, { "rev_mix", 0.25f } } },

        // ------------------------------------------------------------------ PADS
        { "Uplifting Pad", "Pad", { { "vol", -12.0f },
            { "a_table", 1 }, { "a_uni", 7 }, { "a_det", 0.3f }, { "a_width", 1 },
            { "b_on", 1 }, { "b_table", 3 }, { "b_pos", 0.5f }, { "b_uni", 3 }, { "b_level", 0.4f },
            { "voices", 12 }, { "flt_type", 2 }, { "flt_cut", 3000 },
            { "e1_a", 0.8f }, { "e1_d", 1.0f }, { "e1_s", 0.9f }, { "e1_r", 2.5f },
            { "l1_sync", 0 }, { "l1_rate", 0.15f }, { "l1_mode", 0 }, { "m1_src", 1 }, { "m1_dst", 20 }, { "m1_amt", 0.12f },
            { "l2_sync", 0 }, { "l2_rate", 0.09f }, { "l2_mode", 0 }, { "m2_src", 2 }, { "m2_dst", 7 }, { "m2_amt", 0.3f },
            { "cho_on", 1 }, { "cho_mix", 0.4f }, { "rev_on", 1 }, { "rev_mix", 0.42f }, { "rev_decay", 6 }, { "rev_size", 0.85f } } },

        { "Supersaw Pump Pad", "Pad", { { "vol", -11.0f },
            { "a_table", 1 }, { "a_uni", 9 }, { "a_det", 0.33f }, { "a_width", 1 },
            { "voices", 12 }, { "flt_type", 2 }, { "flt_cut", 4200 },
            { "e1_a", 0.15f }, { "e1_s", 1 }, { "e1_r", 0.8f },
            { "pump_on", 1 }, { "pump_depth", 0.85f }, { "pump_curve", 0.55f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", 0.4f },
            { "cho_on", 1 }, { "cho_mix", 0.3f }, { "rev_on", 1 }, { "rev_mix", 0.3f } } },

        { "Dark Atmos Pad", "Pad", { { "vol", -9.5f },
            { "a_table", 6 }, { "a_pos", 0.3f }, { "a_uni", 4 }, { "a_det", 0.15f },
            { "b_on", 1 }, { "b_table", 5 }, { "b_pos", 0.2f }, { "b_oct", -1 }, { "b_uni", 3 }, { "b_level", 0.5f },
            { "voices", 12 }, { "flt_type", 2 }, { "flt_cut", 1600 }, { "flt_res", 0.2f },
            { "e1_a", 1.5f }, { "e1_s", 1 }, { "e1_r", 3.5f },
            { "l1_sync", 0 }, { "l1_rate", 0.07f }, { "l1_mode", 0 }, { "m1_src", 1 }, { "m1_dst", 1 }, { "m1_amt", 0.3f },
            { "l2_sync", 0 }, { "l2_rate", 0.11f }, { "l2_mode", 0 }, { "m2_src", 2 }, { "m2_dst", 20 }, { "m2_amt", 0.15f },
            { "rev_on", 1 }, { "rev_mix", 0.5f }, { "rev_decay", 8 }, { "rev_size", 0.9f } } },

        // ------------------------------------------------------------------ FX
        { "Laser Zap", "FX", { { "vol", 3.5f },
            { "a_table", 0 }, { "a_pos", 0.4f }, { "a_uni", 2 }, { "a_det", 0.1f },
            { "voices", 4 }, { "flt_on", 0 }, { "e1_d", 0.4f }, { "e1_s", 0 }, { "e1_r", 0.2f },
            { "e3_a", 0 }, { "e3_d", 0.3f }, { "e3_s", 0 }, { "m1_src", 7 }, { "m1_dst", 19 }, { "m1_amt", 1.0f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.3f }, { "dly_fb", 0.5f } } },

        { "White Noise Riser", "FX", { { "vol", -4.5f },
            { "a_on", 1 }, { "a_table", 1 }, { "a_uni", 7 }, { "a_det", 0.4f }, { "a_level", 0.35f },
            { "noise_on", 1 }, { "noise_level", 0.8f }, { "noise_color", 0.6f },
            { "voices", 4 }, { "flt_type", 3 }, { "flt_cut", 250 }, { "flt_res", 0.3f },
            { "e1_a", 0.05f }, { "e1_s", 1 }, { "e1_r", 1.5f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", 0.75f }, { "m2_src", 12 }, { "m2_dst", 19 }, { "m2_amt", 0.5f },
            { "m3_src", 12 }, { "m3_dst", 33 }, { "m3_amt", 0.5f },
            { "rev_on", 1 }, { "rev_mix", 0.4f }, { "rev_decay", 5 }, { "dly_on", 1 }, { "dly_mix", 0.2f } } },

        { "Downlifter Sweep", "FX", { { "vol", 2.0f },
            { "a_on", 0 }, { "noise_on", 1 }, { "noise_level", 0.9f }, { "noise_color", 0.5f },
            { "voices", 2 }, { "flt_type", 5 }, { "flt_cut", 400 }, { "flt_res", 0.5f }, { "flt_env", 0.65f },
            { "e2_a", 0 }, { "e2_d", 4.0f }, { "e2_s", 0 }, { "e1_a", 0.002f }, { "e1_d", 4.0f }, { "e1_s", 0 }, { "e1_r", 1.0f },
            { "rev_on", 1 }, { "rev_mix", 0.45f }, { "rev_decay", 6 } } },

        // ------------------------------------------------------------------ 2.0 SHOWCASE
        { "Hyper Anthem Lead", "Lead", { { "vol", 1.5f },
            { "a_table", 1 }, { "a_uni", 7 }, { "a_det", 0.3f }, { "a_width", 1 },
            { "c_on", 1 }, { "c_table", 9 }, { "c_pos", 0.6f }, { "c_oct", 1 }, { "c_level", 0.35f },
            { "voices", 8 }, { "flt_type", 2 }, { "flt_cut", 8000 }, { "e1_a", 0.004f }, { "e1_s", 0.85f }, { "e1_r", 0.35f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", -0.4f }, { "m2_src", 12 }, { "m2_dst", 13 }, { "m2_amt", 0.4f },
            { "hyp_on", 1 }, { "hyp_detune", 0.4f }, { "hyp_dim", 0.65f }, { "hyp_mix", 0.45f },
            { "ott_on", 1 }, { "ott_depth", 0.35f }, { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.14f },
            { "rev_on", 1 }, { "rev_mix", 0.24f }, { "rev_size", 0.75f } } },

        { "Comb Pluck", "Pluck", { { "vol", 1.5f },
            { "a_table", 0 }, { "a_pos", 0.667f }, { "a_uni", 3 }, { "a_det", 0.12f },
            { "noise_on", 1 }, { "noise_level", 0.35f }, { "noise_color", 0.7f },
            { "voices", 8 }, { "flt_type", 7 }, { "flt_cut", 220 }, { "flt_res", 0.75f }, { "flt_env", 0.25f }, { "flt_key", 1.0f },
            { "f2_on", 1 }, { "f2_type", 2 }, { "f2_cut", 5500 }, { "f2_env", 0.2f },
            { "e2_a", 0 }, { "e2_d", 0.3f }, { "e2_s", 0 }, { "e1_d", 0.6f }, { "e1_s", 0 }, { "e1_r", 0.4f },
            { "m1_src", 12 }, { "m1_dst", 21 }, { "m1_amt", 0.25f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.2f }, { "rev_on", 1 }, { "rev_mix", 0.3f } } },

        { "Formant Talk Bass", "Bass", { { "vol", 3.5f },
            { "a_table", 10 }, { "a_pos", 0.4f }, { "a_uni", 2 }, { "a_det", 0.08f }, { "a_level", 0.9f },
            { "sub_on", 1 }, { "sub_oct", 1 }, { "sub_level", 0.5f },
            { "voices", 1 }, { "legato", 1 }, { "glide", 0.04f },
            { "flt_type", 9 }, { "flt_cut", 300 }, { "flt_res", 0.5f },
            { "l1_sync", 1 }, { "l1_div", 9 }, { "l1_shape", 1 },
            { "m1_src", 1 }, { "m1_dst", 20 }, { "m1_amt", 0.35f }, { "m2_src", 9 }, { "m2_dst", 29 }, { "m2_amt", 0.5f },
            { "m3_src", 12 }, { "m3_dst", 20 }, { "m3_amt", 0.3f },
            { "dist_on", 1 }, { "dist_mode", 0 }, { "dist_drive", 0.3f }, { "ott_on", 1 }, { "ott_depth", 0.4f } } },

        { "Phaser Dream Pad", "Pad", { { "vol", -11.5f },
            { "a_table", 7 }, { "a_pos", 0.6f }, { "a_uni", 5 }, { "a_det", 0.2f }, { "a_width", 1 },
            { "b_on", 1 }, { "b_table", 9 }, { "b_pos", 0.3f }, { "b_uni", 3 }, { "b_level", 0.45f }, { "b_oct", 1 },
            { "voices", 12 }, { "flt_type", 2 }, { "flt_cut", 3500 },
            { "e1_a", 0.9f }, { "e1_s", 1 }, { "e1_r", 3.0f },
            { "l1_sync", 0 }, { "l1_rate", 0.08f }, { "l1_mode", 0 }, { "m1_src", 1 }, { "m1_dst", 7 }, { "m1_amt", 0.4f },
            { "pha_on", 1 }, { "pha_rate", 0.12f }, { "pha_mix", 0.5f }, { "pha_fb", 0.6f },
            { "cho_on", 1 }, { "cho_mix", 0.3f }, { "rev_on", 1 }, { "rev_mix", 0.45f }, { "rev_decay", 7 }, { "rev_size", 0.9f } } },

        { "Trance Arp", "Pluck", { { "vol", 4.0f },
            { "a_table", 1 }, { "a_uni", 5 }, { "a_det", 0.22f },
            { "b_on", 1 }, { "b_table", 0 }, { "b_pos", 1 }, { "b_oct", 1 }, { "b_level", 0.35f },
            { "voices", 8 }, { "flt_type", 0 }, { "flt_cut", 600 }, { "flt_res", 0.3f }, { "flt_env", 0.5f },
            { "e2_a", 0 }, { "e2_d", 0.15f }, { "e2_s", 0 }, { "e1_d", 0.25f }, { "e1_s", 0 }, { "e1_r", 0.2f },
            { "arp_on", 1 }, { "arp_mode", 2 }, { "arp_rate", 3 }, { "arp_oct", 2 }, { "arp_gate", 0.5f },
            { "m1_src", 12 }, { "m1_dst", 20 }, { "m1_amt", 0.4f },
            { "dly_on", 1 }, { "dly_div", 10 }, { "dly_mix", 0.22f }, { "dly_fb", 0.4f }, { "rev_on", 1 }, { "rev_mix", 0.28f } } },

        { "Minor Chord Stab", "Keys", { { "vol", 0.5f },
            { "a_table", 1 }, { "a_uni", 5 }, { "a_det", 0.22f },
            { "b_on", 1 }, { "b_table", 2 }, { "b_pos", 0.3f }, { "b_uni", 3 }, { "b_level", 0.45f },
            { "chord", 2 }, { "voices", 16 }, { "flt_type", 2 }, { "flt_cut", 2200 }, { "flt_env", 0.3f }, { "e2_d", 0.3f }, { "e2_s", 0 },
            { "e1_d", 0.4f }, { "e1_s", 0 }, { "e1_r", 0.15f },
            { "ott_on", 1 }, { "ott_depth", 0.4f }, { "rev_on", 1 }, { "rev_mix", 0.2f } } },

        { "Jet Flanger Lead", "Lead", { { "vol", 0.5f },
            { "a_table", 1 }, { "a_uni", 5 }, { "a_det", 0.25f },
            { "b_on", 1 }, { "b_table", 11 }, { "b_pos", 0.3f }, { "b_level", 0.4f },
            { "voices", 6 }, { "flt_type", 2 }, { "flt_cut", 7000 }, { "e1_s", 0.85f }, { "e1_r", 0.3f },
            { "fla_on", 1 }, { "fla_rate", 0.15f }, { "fla_depth", 0.7f }, { "fla_fb", 0.7f }, { "fla_mix", 0.6f },
            { "cmp_on", 1 }, { "cmp_thresh", -20 }, { "cmp_ratio", 4 }, { "cmp_gain", 4 },
            { "rev_on", 1 }, { "rev_mix", 0.2f } } },
    };
    return p;
}

} // namespace lumen::synth
