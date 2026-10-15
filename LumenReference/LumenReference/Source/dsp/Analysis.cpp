#include "Analysis.h"
#include "LimiterEngine.h"
#include <algorithm>
#include <numeric>

namespace lumen::ref
{

void SpecAccum::prepare (double sampleRate)
{
    fs = sampleRate;
    win.resize (kFft);
    for (int i = 0; i < kFft; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (kFft - 1));
    bufL.assign (kFft, 0.0f); bufR.assign (kFft, 0.0f);
    fl.assign (2 * kFft, 0.0f); fr.assign (2 * kFft, 0.0f);
    reset();
}

void SpecAccum::reset()
{
    for (auto* v : { &pL, &pR, &pM, &pS, &cx }) v->assign (kFft / 2 + 1, 0.0);
    fill = 0; frames = 0;
}

void SpecAccum::push (const float* L, const float* R, int n)
{
    for (int i = 0; i < n; ++i)
    {
        bufL[(size_t) fill] = L[i]; bufR[(size_t) fill] = R[i];
        if (++fill == kFft)
        {
            frame();
            std::copy (bufL.begin() + kFft / 2, bufL.end(), bufL.begin());
            std::copy (bufR.begin() + kFft / 2, bufR.end(), bufR.begin());
            fill = kFft / 2;
        }
    }
}

void SpecAccum::frame()
{
    double e = 0.0;
    for (int i = 0; i < kFft; ++i) e += (double) bufL[(size_t) i] * bufL[(size_t) i] + (double) bufR[(size_t) i] * bufR[(size_t) i];
    if (e < 1.0e-9) return;                                   // ignore silence (don't dilute the average)
    std::fill (fl.begin(), fl.end(), 0.0f); std::fill (fr.begin(), fr.end(), 0.0f);
    for (int i = 0; i < kFft; ++i) { fl[(size_t) i] = bufL[(size_t) i] * win[(size_t) i]; fr[(size_t) i] = bufR[(size_t) i] * win[(size_t) i]; }
    fft.performRealOnlyForwardTransform (fl.data(), true);
    fft.performRealOnlyForwardTransform (fr.data(), true);
    const double hop = (kFft / 2) / fs;
    const double a = tau > 0.0f ? std::exp (-hop / tau) : frames / (frames + 1.0);
    for (int k = 0; k <= kFft / 2; ++k)
    {
        const double lr = fl[(size_t) (2 * k)], li = fl[(size_t) (2 * k + 1)], rr = fr[(size_t) (2 * k)], ri = fr[(size_t) (2 * k + 1)];
        const double l2 = lr * lr + li * li, r2 = rr * rr + ri * ri;
        const double mr = 0.5 * (lr + rr), mi = 0.5 * (li + ri), sr = 0.5 * (lr - rr), si = 0.5 * (li - ri);
        const double c = lr * rr + li * ri;
        auto upd = [a] (double& acc, double v) { acc = a * acc + (1.0 - a) * v; };
        upd (pL[(size_t) k], l2); upd (pR[(size_t) k], r2); upd (pM[(size_t) k], mr * mr + mi * mi); upd (pS[(size_t) k], sr * sr + si * si); upd (cx[(size_t) k], c);
    }
    frames += 1.0;
}

void SpecAccum::computeSpectral (Profile& p) const
{
    if (frames <= 0) { p.valid = false; return; }
    const double binHz = fs / kFft;
    const double r = std::pow (2.0, 1.0 / 12.0);
    float raw[kPoints];
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = pointFreq (i);
        int k0 = (int) std::floor (f / r / binHz), k1 = (int) std::ceil (f * r / binHz);
        k0 = std::clamp (k0, 1, kFft / 2); k1 = std::clamp (k1, k0, kFft / 2);
        double s = 0.0; int n = 0;
        for (int k = k0; k <= k1; ++k) { s += pL[(size_t) k] + pR[(size_t) k]; ++n; }
        raw[i] = (float) (10.0 * std::log10 (s / std::max (1, n) + 1e-20));
    }
    // normalise: 100 Hz .. 5 kHz average = 0 dB, then +3 dB/oct tilt compensation (pink reference reads flat)
    double norm = 0.0; int nn = 0;
    for (int i = 0; i < kPoints; ++i)
    {
        raw[i] += (float) (3.0 * std::log2 (pointFreq (i) / 1000.0));
        const float f = pointFreq (i);
        if (f >= 100.0f && f <= 5000.0f) { norm += raw[i]; ++nn; }
    }
    norm /= std::max (1, nn);
    for (int i = 0; i < kPoints; ++i) p.spec[i] = (float) (raw[i] - norm);

    const auto& e = bandEdges();
    for (int b = 0; b < kBands; ++b)
    {
        double s = 0.0; int n = 0;
        for (int i = 0; i < kPoints; ++i) { const float f = pointFreq (i); if (f >= e[(size_t) b] && f < e[(size_t) b + 1]) { s += p.spec[i]; ++n; } }
        p.bandDb[b] = (float) (s / std::max (1, n));
        double m = 0, sd = 0, l2 = 0, r2 = 0, c = 0;
        const int k0 = std::max (1, (int) (e[(size_t) b] / binHz)), k1 = std::min (kFft / 2, (int) (e[(size_t) b + 1] / binHz));
        for (int k = k0; k <= k1; ++k) { m += pM[(size_t) k]; sd += pS[(size_t) k]; l2 += pL[(size_t) k]; r2 += pR[(size_t) k]; c += cx[(size_t) k]; }
        p.bandWidth[b] = m > 1e-20 ? (float) (100.0 * std::sqrt (sd / m)) : 0.0f;
        p.bandCorr[b] = (l2 > 1e-20 && r2 > 1e-20) ? (float) std::clamp (c / std::sqrt (l2 * r2), -1.0, 1.0) : 1.0f;
    }
    p.valid = true;
}

float loudnessRange (std::vector<float> st)
{
    st.erase (std::remove_if (st.begin(), st.end(), [] (float v) { return v <= -70.0f; }), st.end());
    if (st.size() < 10) return 0.0f;
    double e = 0.0; for (float v : st) e += std::pow (10.0, v / 10.0);
    const float rel = (float) (10.0 * std::log10 (e / st.size())) - 20.0f;
    st.erase (std::remove_if (st.begin(), st.end(), [rel] (float v) { return v < rel; }), st.end());
    if (st.size() < 10) return 0.0f;
    std::sort (st.begin(), st.end());
    auto pct = [&] (double q) { return st[(size_t) std::clamp ((int) std::round (q * (st.size() - 1)), 0, (int) st.size() - 1)]; };
    return pct (0.95) - pct (0.10);
}

Profile analyseBuffer (const juce::AudioBuffer<float>& buf, int start, int end, double fs)
{
    Profile p;
    start = std::clamp (start, 0, buf.getNumSamples()); end = std::clamp (end, start, buf.getNumSamples());
    if (end - start < (int) (fs * 0.5)) return p;
    const float* L = buf.getReadPointer (0);
    const float* R = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);
    SpecAccum acc; acc.setTimeConstant (0.0f); acc.prepare (fs);
    lumen::LoudnessMeter lm; lm.prepare (fs);
    lumen::TruePeak tpL, tpR;
    std::vector<float> st;
    float tp = 0.0f;
    const int blk = (int) (fs * 0.1);
    for (int i = start; i < end; i += blk)
    {
        const int n = std::min (blk, end - i);
        acc.push (L + i, R + i, n);
        for (int k = 0; k < n; ++k)
        {
            lm.process (L[i + k], R[i + k]);
            tp = std::max (tp, std::max (tpL.push (L[i + k]), tpR.push (R[i + k])));
        }
        if (lm.shortTerm > -100.0f && i - start >= (int) (fs * 3.0)) st.push_back (lm.shortTerm);
    }
    acc.computeSpectral (p);
    p.lufsI = lm.integrated;
    p.lufsS = lm.shortTerm;
    p.lra = loudnessRange (st);
    p.truePeakDb = 20.0f * std::log10 (std::max (tp, 1.0e-6f));
    p.plr = p.truePeakDb - p.lufsI;
    p.valid = p.valid && p.lufsI > -70.0f;
    return p;
}

juce::StringArray advise (const Profile& mix, const Profile& ref, int maxLines)
{
    struct Item { float weight; juce::String text; };
    std::vector<Item> items;
    if (! mix.valid || ! ref.valid) return { "Play your track (and load a reference) to get advice." };

    const float dL = mix.lufsI - ref.lufsI;
    if (std::abs (dL) > 1.0f && mix.lufsI > -70.0f)
        items.push_back ({ std::abs (dL) * 1.2f, dL < 0 ? "Your mix is " + juce::String (-dL, 1) + " LU quieter than the reference - push the limiter (Lumen Limiter) or lower the reference."
                                                      : "Your mix is " + juce::String (dL, 1) + " LU louder than the reference - check for over-limiting." });
    static const char* fix[kBands][2] = {
        { "Sub (20-60 Hz) is too loud - lower the 808/sub or high-pass non-bass parts.", "Sub (20-60 Hz) is weak - boost the sub/808 around 45-55 Hz or use Lumen Bass harmonics." },
        { "Low end (60-200 Hz) is too heavy - cut around 100-150 Hz or sidechain the bass to the kick.", "Low end (60-200 Hz) is thin - raise kick/bass or boost around 80-120 Hz." },
        { "Low-mids (200-800 Hz) are muddy - cut 2-3 dB around 300-400 Hz on pads/synths.", "Low-mids (200-800 Hz) are scooped - the mix may sound thin; ease the cuts around 300-500 Hz." },
        { "Mids (800 Hz-3 kHz) poke out - tame leads/vocals around 1-2 kHz.", "Mids (800 Hz-3 kHz) are recessed - bring the lead/vocal forward around 1.5-2.5 kHz." },
        { "High-mids (3-8 kHz) are harsh - de-ess or cut around 4-6 kHz.", "High-mids (3-8 kHz) lack presence - boost leads/snare around 4-5 kHz." },
        { "Air (8-20 kHz) is too bright - lower hats/cymbals or shelf down above 10 kHz.", "Air (8-20 kHz) is dull - add a high shelf above 10 kHz or brighten hats/FX." } };
    for (int b = 0; b < kBands; ++b)
    {
        const float d = mix.bandDb[b] - ref.bandDb[b];
        if (std::abs (d) >= 1.5f)
            items.push_back ({ std::abs (d), (d > 0 ? "+" : "") + juce::String (d, 1) + " dB  " + fix[b][d > 0 ? 0 : 1] });
    }
    if (mix.bandCorr[0] < 0.9f || mix.bandWidth[0] > 25.0f)
        items.push_back ({ 4.0f, "Sub is not mono (correlation " + juce::String (mix.bandCorr[0], 2) + ") - make below ~100 Hz mono (Lumen Bass MONO)." });
    for (int b = 3; b < kBands; ++b)
    {
        const float dw = mix.bandWidth[b] - ref.bandWidth[b];
        if (dw < -20.0f) items.push_back ({ std::abs (dw) / 12.0f, juce::String (bandNames()[b]) + " is narrower than the reference (" + juce::String ((int) mix.bandWidth[b]) + "% vs " + juce::String ((int) ref.bandWidth[b]) + "%) - widen synths/FX there." });
        if (dw > 25.0f) items.push_back ({ dw / 12.0f, juce::String (bandNames()[b]) + " is wider than the reference - check mono compatibility." });
    }
    for (int b = 0; b < kBands; ++b)
        if (mix.bandCorr[b] < 0.0f) items.push_back ({ 5.0f, juce::String (bandNames()[b]) + " has negative correlation - it will cancel in mono!" });
    const float dP = mix.plr - ref.plr;
    if (std::abs (dP) > 2.0f && mix.lufsI > -70.0f)
        items.push_back ({ std::abs (dP) * 0.8f, dP < 0 ? "Your mix is more compressed (PLR " + juce::String (mix.plr, 1) + " vs " + juce::String (ref.plr, 1) + " dB) - ease the limiter/OTT for more punch."
                                                       : "Your mix is more dynamic (PLR " + juce::String (mix.plr, 1) + " vs " + juce::String (ref.plr, 1) + " dB) - more bus compression/limiting would match." });
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.weight > b.weight; });
    juce::StringArray out;
    for (auto& it : items) { if (out.size() >= maxLines) break; out.add (it.text); }
    if (out.isEmpty()) out.add ("Very close to the reference - tonal balance, width and loudness match within tolerance.");
    return out;
}

} // namespace lumen::ref
