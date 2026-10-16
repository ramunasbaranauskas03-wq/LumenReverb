#pragma once
#include "KickSynth.h"
#include <vector>

// KICK CLONE: measures a kick sample's pitch sweep and amplitude envelope and returns synth settings that rebuild its sub.
namespace lumen::kick
{

struct CloneResult
{
    bool ok = false;
    float tuneHz = 50, pitchSt = 24, pitchMs = 40, holdMs = 40, decayMs = 300;
    juce::String message;
};

inline CloneResult cloneKick (const SampleData& s)
{
    CloneResult r;
    const int n = s.buf.getNumSamples();
    const double fs = s.rate;
    if (n < (int) (0.05 * fs)) { r.message = "sample too short"; return r; }
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float v = 0; for (int c = 0; c < s.buf.getNumChannels(); ++c) v += s.buf.getSample (c, i);
        x[(size_t) i] = v / (float) s.buf.getNumChannels();
    }
    float pk = 0; for (float v : x) pk = std::max (pk, std::abs (v));
    if (pk < 1e-4f) { r.message = "sample is silent"; return r; }
    int on = 0; while (on < n && std::abs (x[(size_t) on]) < 0.05f * pk) ++on;
    // low-pass for the pitch track
    Biquad a, b; a.lp (fs, 400.0, 0.5412); b.lp (fs, 400.0, 1.3066);
    std::vector<float> lp ((size_t) n);
    for (int i = 0; i < n; ++i) lp[(size_t) i] = b.process (a.process (x[(size_t) i]));
    // envelope (5 ms RMS)
    const int w = std::max (8, (int) (0.005 * fs));
    std::vector<float> envDb;
    for (int i = on; i + w < n; i += w)
    {
        double e = 0; for (int j = 0; j < w; ++j) e += (double) lp[(size_t) (i + j)] * lp[(size_t) (i + j)];
        envDb.push_back ((float) (10.0 * std::log10 (e / w + 1e-20)));
    }
    if (envDb.size() < 4) { r.message = "sample too short"; return r; }
    int pkI = 0; for (int i = 0; i < (int) envDb.size(); ++i) if (envDb[(size_t) i] > envDb[(size_t) pkI]) pkI = i;
    const float top = envDb[(size_t) pkI];
    int holdEnd = pkI; while (holdEnd + 1 < (int) envDb.size() && envDb[(size_t) holdEnd + 1] > top - 3.0f) ++holdEnd;
    int i6 = holdEnd, i36 = -1;
    while (i6 < (int) envDb.size() && envDb[(size_t) i6] > top - 6.0f) ++i6;
    for (int i = i6; i < (int) envDb.size(); ++i) if (envDb[(size_t) i] < top - 36.0f) { i36 = i; break; }
    const double wMs = 1000.0 * w / fs;
    r.holdMs = (float) std::clamp ((holdEnd - 0) * wMs, 0.0, 500.0);
    if (i36 > i6) r.decayMs = (float) std::clamp ((i36 - i6) * wMs * 2.0, 30.0, 3000.0);
    else r.decayMs = (float) std::clamp ((envDb.size() - (size_t) holdEnd) * wMs, 30.0, 3000.0);
    // pitch: positive zero crossings of the low-passed signal
    std::vector<double> zc;
    const float gate = 0.02f * pk;
    for (int i = on + 1; i < n; ++i)
    {
        if (lp[(size_t) i - 1] < 0 && lp[(size_t) i] >= 0 && std::abs (lp[(size_t) i]) + std::abs (lp[(size_t) i - 1]) > 1e-9f)
        {
            // stop once the tail is gone
            const int ei = (i - on) / w;
            if (ei < (int) envDb.size() && envDb[(size_t) ei] < top - 40.0f) break;
            zc.push_back (i - 1 + lp[(size_t) i - 1] / (lp[(size_t) i - 1] - lp[(size_t) i]));
        }
        (void) gate;
    }
    if (zc.size() < 4) { r.message = "no clear pitch found"; return r; }
    std::vector<std::pair<double, double>> ft;     // (time s, freq)
    for (size_t k = 1; k < zc.size(); ++k) { const double per = zc[k] - zc[k - 1]; if (per > 0) ft.push_back ({ (zc[k - 1] - on) / fs, fs / per }); }
    // end frequency: median of the later periods
    std::vector<double> late;
    for (auto& q : ft) if (q.first > 0.06 && q.second > 20 && q.second < 250) late.push_back (q.second);
    if (late.empty()) for (size_t k = ft.size() / 2; k < ft.size(); ++k) late.push_back (ft[k].second);
    std::sort (late.begin(), late.end());
    const double fEnd = std::clamp (late[late.size() / 2], 20.0, 250.0);
    const double fStart = std::max (fEnd, ft.front().second);
    const double st0 = 12.0 * std::log2 (fStart / fEnd);
    double tau = 0.03;
    for (auto& q : ft) { const double st = 12.0 * std::log2 (std::max (q.second, fEnd) / fEnd); if (st < st0 / std::exp (1.0)) { tau = std::max (0.002, q.first); break; } }
    r.tuneHz = (float) fEnd;
    // the first measurable period already lags the true start: extrapolate back half a period
    const double tFirst = ft.front().first;
    r.pitchSt = (float) std::clamp (st0 * std::exp (tFirst / tau), 0.0, 60.0);
    r.pitchMs = (float) std::clamp (tau * 1000.0, 2.0, 300.0);
    r.ok = true;
    const int midi = (int) std::round (69 + 12 * std::log2 (fEnd / 440.0));
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    r.message = juce::String (fEnd, 1) + " Hz (" + names[(midi % 12 + 12) % 12] + juce::String (midi / 12 - 1) + "), sweep " + juce::String (juce::roundToInt (r.pitchSt))
              + " st / " + juce::String (juce::roundToInt (r.pitchMs)) + " ms, decay " + juce::String (juce::roundToInt (r.decayMs)) + " ms";
    return r;
}

} // namespace lumen::kick
