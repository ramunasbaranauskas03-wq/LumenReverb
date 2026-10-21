#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

// =====================================================================================
// Genre targets: tonal balance (average per-FFT-bin power, dB, on a 1/6-octave grid from
// 20 Hz) and club loudness. Only the shape matters (curves are level-aligned when used).
// The modern master reference is a -4.5 dB/oct slope above 100 Hz with genre-specific
// low end and air; the "Reference track" slot is filled from a file the user loads.
// =====================================================================================
namespace lumen::master
{
static constexpr int kCurvePts = 61;                 // 20 Hz * 2^(i/6), i = 0..60  (20 Hz .. 20.5 kHz)
using Curve = std::array<float, kCurvePts>;
inline double curveFreq (int i) { return 20.0 * std::pow (2.0, i / 6.0); }

struct GenreInfo { const char* name; float lufs; float sub, low, lowMid, presence, air; };
inline const std::array<GenreInfo, 13>& genres()
{
    // offsets in dB relative to the base curve:  sub (30-60), low (60-120), low-mid (200-500), presence (2-5k), air (10k+)
    static const std::array<GenreInfo, 13> g {{
        { "EDM / Big Room",        -6.5f,  1.0f,  1.5f, -0.5f,  1.0f,  0.5f },
        { "Progressive House",     -7.5f,  0.5f,  1.0f,  0.0f,  0.5f,  0.5f },
        { "Future / Bass House",   -7.0f,  1.5f,  1.0f, -0.5f,  1.0f,  0.0f },
        { "Tech House",            -8.0f,  1.0f,  1.5f,  0.5f, -0.5f, -0.5f },
        { "Techno",                -8.5f,  1.0f,  1.0f,  1.0f, -1.0f, -0.5f },
        { "Melodic Techno / Afro", -9.0f,  0.5f,  1.0f,  0.5f, -0.5f,  0.0f },
        { "Trance",                -7.5f,  0.0f,  0.5f, -0.5f,  1.0f,  1.5f },
        { "Dubstep / Riddim",      -6.0f,  2.5f,  0.5f,  0.0f,  1.0f,  0.0f },
        { "Drum & Bass",           -7.0f,  2.0f,  0.0f, -0.5f,  1.0f,  0.5f },
        { "Future Bass / Pop EDM", -8.0f,  1.0f,  0.5f, -1.0f,  1.5f,  1.0f },
        { "Hip-Hop / Trap",        -8.0f,  3.0f,  1.0f,  0.0f,  0.5f,  0.0f },
        { "Pop",                   -9.0f,  0.0f,  0.0f, -0.5f,  1.5f,  1.0f },
        { "Lo-Fi / Chill",        -12.0f,  0.0f,  0.5f,  1.0f, -1.5f, -3.0f },
    }};
    return g;
}
static constexpr int kGenreCount = 13;
static constexpr int kTargetReference = 13;           // "Reference track"

inline Curve buildGenreCurve (const GenreInfo& g)
{
    Curve c {};
    auto bump = [] (double f, double lo, double hi) { const double x = std::log2 (f); const double a = std::log2 (lo), b = std::log2 (hi);
                                                       if (x < a - 0.5 || x > b + 0.5) return 0.0; if (x >= a && x <= b) return 1.0; return x < a ? 1.0 - (a - x) * 2.0 : 1.0 - (x - b) * 2.0; };
    for (int i = 0; i < kCurvePts; ++i)
    {
        const double f = curveFreq (i);
        // base: modern master (per-bin power), -4.5 dB/oct above 100 Hz, roll-offs at both ends
        static const double pf[] { 20, 25, 30, 40, 60, 80, 100 }, pv[] { -8, -3, 0, 1.5, 1.5, 1, 0 };
        double d;
        if (f <= 100)
        {
            int k = 0; while (k < 5 && f > pf[k + 1]) ++k;
            const double t = std::clamp ((std::log2 (f) - std::log2 (pf[k])) / (std::log2 (pf[k + 1]) - std::log2 (pf[k])), 0.0, 1.0);
            d = pv[k] + (pv[k + 1] - pv[k]) * t;
        }
        else d = -4.5 * std::log2 (f / 100.0);
        if (f > 12000) d -= 6.0 * std::log2 (f / 12000.0);
        d += g.sub * bump (f, 30, 60) + g.low * bump (f, 60, 120) + g.lowMid * bump (f, 200, 500) + g.presence * bump (f, 2000, 5000) + g.air * bump (f, 10000, 16000);
        c[(size_t) i] = (float) d;
    }
    return c;
}

// reference-track slot (double buffered: written on the message thread, read anywhere)
struct ReferenceSlot
{
    std::array<Curve, 2> buf {}; std::atomic<int> idx { 0 }; std::atomic<bool> valid { false }; std::atomic<float> lufs { -9.0f };
    void set (const Curve& c, float l) { const int w = 1 - idx.load(); buf[(size_t) w] = c; idx.store (w); lufs.store (l); valid.store (true); }
    const Curve& get() const { return buf[(size_t) idx.load()]; }
};
inline ReferenceSlot& referenceSlot() { static ReferenceSlot r; return r; }

inline const Curve& targetCurve (int target)
{
    static const auto all = []
    {
        std::array<Curve, kGenreCount> a {};
        for (int i = 0; i < kGenreCount; ++i) a[(size_t) i] = buildGenreCurve (genres()[(size_t) i]);
        return a;
    }();
    if (target == kTargetReference && referenceSlot().valid.load()) return referenceSlot().get();
    return all[(size_t) std::clamp (target, 0, kGenreCount - 1)];
}

inline double targetDb (const Curve& c, double f)
{
    const double p = std::clamp (6.0 * std::log2 (std::max (f, 20.0) / 20.0), 0.0, (double) kCurvePts - 1.0001);
    const int i = (int) p; const double t = p - i;
    return c[(size_t) i] + (c[(size_t) i + 1] - c[(size_t) i]) * t;
}

// streaming / club loudness destinations
struct Platform { const char* name; float lufs; float tp; };
inline const std::array<Platform, 7>& platforms()
{
    static const std::array<Platform, 7> p {{
        { "Club (genre)", 0.0f, -0.5f },     // lufs 0 = use the genre value
        { "Spotify",     -14.0f, -1.0f },
        { "Apple Music", -16.0f, -1.0f },
        { "YouTube",     -14.0f, -1.0f },
        { "SoundCloud",  -10.0f, -1.0f },
        { "Beatport",     -8.0f, -0.3f },
        { "Custom",       0.0f, -1.0f },
    }};
    return p;
}
// what the streaming services do to a master of this loudness (dB turned down, <= 0)
struct Penalty { const char* name; float target; };
inline const std::array<Penalty, 5>& penalties()
{
    static const std::array<Penalty, 5> p {{ { "Spotify", -14.0f }, { "Apple", -16.0f }, { "YouTube", -14.0f }, { "Tidal", -14.0f }, { "Amazon", -14.0f } }};
    return p;
}

} // namespace lumen::master
