#pragma once
#include "SmartAnalyzer.h"
#include <algorithm>

namespace lumen::SmartAssist
{
    // Values in parameter units. style is a choice index (Clean, Opto, FET, Vari-Mu).
    struct CompSuggestion
    {
        int   style;
        float threshold, ratio, knee, attack, release;
        bool  autoRelease;
        float scHp, drive, mix;
        float targetGr;   // dB of average gain reduction we aim for
    };

    // Threshold is placed relative to the measured level so the result is the
    // same whether the track is recorded hot or quiet.
    inline CompSuggestion suggestComp (Material m, const Features& f)
    {
        CompSuggestion s {};
        switch (m)
        {
            case Material::Percussive: s = { 2,  0, 4.0f,  3, 12.0f,  80, false,  20, 15, 100, 5.0f }; break; // FET, let transients through
            case Material::Bass:       s = { 1,  0, 4.0f,  6, 15.0f, 150, true,   20,  5, 100, 5.0f }; break; // Opto, even level
            case Material::Pad:        s = { 3,  0, 2.0f, 12, 30.0f, 300, true,   20,  0, 100, 2.5f }; break; // Vari-Mu, gentle
            case Material::FullMix:    s = { 0,  0, 2.0f,  6, 30.0f, 200, true,   90,  0, 100, 2.0f }; break; // Clean glue, SC HP
            case Material::VocalLead:
            case Material::Silence:
            default:                   s = { 1,  0, 3.0f,  6,  5.0f, 120, true,   80, 10, 100, 5.0f }; break; // Opto vocal
        }

        // Estimate what the detector will see: RMS styles read ~RMS+3 dB (sine-calibrated),
        // peak styles read closer to the short-term peak.
        const bool rmsStyle = (s.style == 1 || s.style == 3);
        // Reference level leans toward the loud passages so very dynamic material isn't over-compressed.
        const float spread = f.loudDb > -99.0f ? std::clamp (f.loudDb - f.levelDb, 0.0f, 20.0f) : 0.0f;
        const float refDb  = f.levelDb + spread * std::clamp (spread / 16.0f, 0.0f, 0.85f);
        const float detDb = refDb + (rmsStyle ? 3.0f : 3.0f + 0.5f * std::clamp (f.crestDb - 3.0f, 0.0f, 12.0f));

        // Signal must sit `over` dB above threshold to produce targetGr of reduction
        const float over = s.targetGr * s.ratio / std::max (0.5f, s.ratio - 1.0f);
        s.threshold = std::clamp (detDb - over, -60.0f, 0.0f);
        return s;
    }
}
