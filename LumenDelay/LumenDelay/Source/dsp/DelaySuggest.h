#pragma once
#include "SmartAnalyzer.h"
#include <algorithm>

namespace lumen::SmartAssist
{
    // Values in parameter units (percent = 0..100, Hz). division/mode are choice indices.
    struct DelaySuggestion
    {
        int   division;   // index into divisionChoices()
        int   mode;       // 0 Stereo, 1 Ping-Pong, 2 Mono
        float offset, feedback, diffusion, width, lowCut, highCut, drive, wow, modRate, duck, mix;
    };

    inline DelaySuggestion suggestDelay (Material m, const Features& f)
    {
        DelaySuggestion s {};
        switch (m)
        {
            case Material::Percussive: s = { 2, 1,   0, 25, 10, 100, 300, 6000, 25,  5, 0.5f, 30, 15 }; break;
            case Material::Bass:       s = { 2, 2,   0, 10,  0,  60, 500, 4000, 30,  5, 0.5f, 30, 10 }; break;
            case Material::Pad:        s = { 9, 0,  15, 60, 60, 130, 150, 9000, 10, 30, 0.4f,  0, 35 }; break;
            case Material::FullMix:    s = { 5, 1,   0, 25, 10, 100, 300, 7000, 10, 10, 0.5f, 35, 12 }; break;
            case Material::VocalLead:
            case Material::Silence:
            default:                   s = { 6, 1,   0, 35, 15, 100, 250, 6500, 15, 15, 0.6f, 50, 22 }; break;
        }

        if (f.highRatio > 0.15f) s.highCut *= 0.8f;                      // bright source -> darker repeats
        if (f.lowRatio  > 0.45f) s.lowCut = std::max (s.lowCut, 300.0f); // keep low end clean
        return s;
    }
}
