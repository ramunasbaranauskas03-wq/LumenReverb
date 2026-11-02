#pragma once
#include "Params.h"

namespace lxl
{
inline float warpFactor (int mode, float w) noexcept
{
    switch (mode)
    {
        case ModeBendPlus: case ModeBendMinus: return 1.0f + w * 15.0f;
        case ModeMirror:  return 1.0f + w;
        case ModePWM:     return 0.5f / (0.5f - w * 0.47f);
        case ModeSync: case ModeFormant: return 1.0f + w * 7.0f;
        default: return 1.0f;
    }
}

inline float warpPhase (int mode, float p, float w, float& gain) noexcept
{
    switch (mode)
    {
        case ModeBendPlus:  { const float k = 1.0f + w * 15.0f; return p * k / (1.0f + (k - 1.0f) * p); }
        case ModeBendMinus: { const float k = 1.0f + w * 15.0f; return p / (k - (k - 1.0f) * p); }
        case ModeMirror:    { const float m = 1.0f - w * 0.5f; return p < m ? p / m : (1.0f - p) / juce::jmax (1.0e-4f, 1.0f - m); }
        case ModePWM:       { const float ww = 0.5f - w * 0.47f; return p < ww ? p * 0.5f / ww : 0.5f + (p - ww) * 0.5f / (1.0f - ww); }
        case ModeSync:      { const float r = 1.0f + w * 7.0f; const float q = p * r; return q - (float) (int) q; }
        case ModeFormant:   { const float r = 1.0f + w * 7.0f; const float q = p * r; gain = 4.0f * p * (1.0f - p) * 1.35f; return q - (float) (int) q; }
        case ModeJump:      { if (p < 0.5f) return p; const float q = p + w * 0.5f; return q - (float) (int) q; }
        case ModeQuantize:  { const float n = 2.0f + (1.0f - w) * (1.0f - w) * 254.0f; return std::floor (p * n) / n; }
        default: return p;
    }
}

inline float foldSample (float x) noexcept
{
    float y = (x + 1.0f) * 0.25f;
    y -= std::floor (y);
    return 1.0f - 4.0f * std::abs (y - 0.5f);
}

} // namespace lxl
