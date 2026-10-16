#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <atomic>

// =====================================================================================
// Lumen Resonance — dynamic spectral resonance suppressor
//   STFT (Hann / Hann, 75 % overlap, exact reconstruction -> pure delay of N when nothing is reduced)
//   per frame: level spectrum -> constant-Q envelope (width follows SHARPNESS) -> excess above
//   envelope + SELECTIVITY threshold -> x SENSITIVITY curve (6 bands) -> soft/hard law, capped by DEPTH
//   -> smoothed across frequency (SHARPNESS) -> per-bin attack/release -> TRANSIENT PROTECT lets onsets through
//   Linked / Mid-Side (separate amounts) / Left-Right, external sidechain key, delta, mix, auto gain.
// =====================================================================================
namespace lumen::res
{

struct Band { float freq = 1000, gainDb = 0; };
struct Params
{
    float depth = 5, sharpness = 4, selectivity = 3.5f, attackMs = 5, releaseMs = 50;
    bool hard = false;
    int resolution = 1;             // 0 low latency, 1 standard, 2 high
    int stereo = 0;                 // 0 linked, 1 mid/side, 2 left/right
    float midAmt = 1, sideAmt = 1, protect = 0.5f;
    float focus = 0.3f, toneKeep = 0.3f, recover = 0; bool perceptual = true;
    float lowF = 20, highF = 20000;
    Band band[6];
    bool sidechain = false, delta = false, autoGain = false;
    float mix = 1, outDb = 0;
    Params()
    {
        const float f[6] { 120, 400, 1500, 4000, 8000, 12000 };
        for (int i = 0; i < 6; ++i) band[i].freq = f[i];
    }
};

// sensitivity curve in dB: low shelf, 4 bells, high shelf
inline float sensDb (const Params& p, float f)
{
    const float lf = std::log2 (std::max (1.0f, f));
    float s = 0;
    s += p.band[0].gainDb / (1.0f + std::exp (3.0f * (lf - std::log2 (p.band[0].freq))));
    for (int b = 1; b < 5; ++b) { const float d = (lf - std::log2 (p.band[b].freq)) / 0.6f; s += p.band[b].gainDb * std::exp (-0.5f * d * d); }
    s += p.band[5].gainDb / (1.0f + std::exp (-3.0f * (lf - std::log2 (p.band[5].freq))));
    return s;
}
inline float rangeWeight (const Params& p, float f)
{
    // half-octave fades at the edges of the processing range
    const float lo = std::log2 (std::max (1.0f, f) / std::max (1.0f, p.lowF)), hi = std::log2 (p.highF / std::max (1.0f, f));
    return std::clamp (lo * 2.0f + 0.5f, 0.0f, 1.0f) * std::clamp (hi * 2.0f + 0.5f, 0.0f, 1.0f);
}
// SHARPNESS -> envelope width / gain smoothing width (octaves)
inline float envWidthOct (float sharp)  { return 2.5f * std::pow (0.6f / 2.5f, sharp / 10.0f); }
inline float gainWidthOct (float sharp) { return 0.5f * std::pow ((1.0f / 30.0f) / 0.5f, sharp / 10.0f); }
inline float selectThrDb (float sel)    { return 1.1f * sel - 1.5f; }

class ResonanceEngine
{
public:
    static constexpr int kDisp = 320;              // log-spaced display points 20 Hz .. 20 kHz
    static int fftSizeFor (double fs, int res)
    {
        int scale = 1; while (fs > 48000.0 * scale * 1.5) scale <<= 1;
        return (res == 0 ? 1024 : res == 1 ? 2048 : 4096) * scale;
    }

    void prepare (double sampleRate, int resolution)
    {
        fs = sampleRate;
        maxN = fftSizeFor (fs, 2);
        for (int r = 0; r < 3; ++r)
        {
            const int n = fftSizeFor (fs, r), o = (int) std::round (std::log2 ((double) n));
            ffts[(size_t) r] = std::make_unique<juce::dsp::FFT> (o);
            acFfts[(size_t) r] = std::make_unique<juce::dsp::FFT> (o + 1);
            // autocorrelation of the Hann window (normalises the lag bias)
            auto& w = wAc[(size_t) r]; w.assign ((size_t) n / 2 + 2, 0.0f);
            for (int t = 0; t <= n / 2 + 1; ++t)
            {
                double acc = 0;
                for (int i = 0; i + t < n; i += 4) acc += (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n)) * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (i + t) / n));
                w[(size_t) t] = (float) acc;
            }
        }
        acBuf.assign ((size_t) maxN * 4 + 4, 0.0f);
        for (auto& v : inQ) v.assign ((size_t) maxN, 0.0f);
        for (auto& v : outAcc) v.assign ((size_t) maxN, 0.0f);
        for (auto& v : ready) v.assign ((size_t) maxN, 0.0f);
        for (auto& v : spec) v.assign ((size_t) maxN * 2, 0.0f);
        for (auto& v : scSpec) v.assign ((size_t) maxN * 2, 0.0f);
        for (auto& v : dryDl) v.assign ((size_t) maxN * 2, 0.0f);
        for (auto& v : scHist) v.assign ((size_t) maxN, 0.0f);
        const size_t nb = (size_t) maxN / 2 + 1;
        lev.assign (nb, 0.0f); env.assign (nb, 0.0f); red.assign (nb, 0.0f); prefix.assign (nb + 1, 0.0);
        for (auto& v : state) v.assign (nb, 0.0f);
        for (auto& v : prevLev) v.assign (nb, -120.0f);
        for (auto& v : pers) v.assign (nb, 0.0f);
        for (auto& v : psm) v.assign (nb, 0.0f);
        for (auto& v : dipSlow) v.assign (nb, 0.0f);
        envL.assign (nb, 0.0f); keepW.assign (nb, 1.0f); earTab.assign (nb, 1.0f);
        win.assign ((size_t) maxN, 0.0f);
        envLo.assign (nb, 0); envHi.assign (nb, 0); envLo2.assign (nb, 0); envHi2.assign (nb, 0); gLo.assign (nb, 0); gHi.assign (nb, 0);
        sensTab.assign (nb, 1.0f);
        dispGr.fill (0.0f); dispIn.fill (-120.0f); dispOut.fill (-120.0f);
        setResolution (resolution);
        agGain = 1; agSm = 1; eIn = eOut = 1e-9;
        outSm = std::pow (10.0f, p.outDb / 20.0f);
    }

    void setResolution (int r)
    {
        res = std::clamp (r, 0, 2);
        N = fftSizeFor (fs, res); hop = N / 4;
        const double scale = 2.0 / 3.0;  // sum of hann^2 at 75 % overlap = 1.5
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        synthScale = (float) scale;
        for (auto& v : inQ) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : outAcc) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : ready) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : dryDl) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : scHist) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : state) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : prevLev) std::fill (v.begin(), v.end(), -120.0f);
        for (auto& v : pers) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : psm) std::fill (v.begin(), v.end(), 0.0f);
        pgS[0] = pgS[1] = 0.0f;
        for (auto& v : dipSlow) std::fill (v.begin(), v.end(), 0.0f);
        cnt = 0; dlPos = 0;
        lastSharp = -1; lastBandsHash = -1;
        // display bin map
        for (int d = 0; d < kDisp; ++d)
        {
            const double f0 = 20.0 * std::pow (1000.0, (double) d / kDisp), f1 = 20.0 * std::pow (1000.0, (double) (d + 1) / kDisp);
            dispLo[(size_t) d] = std::clamp ((int) std::floor (f0 * N / fs), 1, N / 2);
            dispHi[(size_t) d] = std::clamp ((int) std::ceil (f1 * N / fs), dispLo[(size_t) d] + 1, N / 2 + 1);
        }
    }
    int latency() const noexcept { return N; }
    int resolution() const noexcept { return res; }

    void setParams (const Params& np) noexcept
    {
        p = np;
        if (p.resolution != res) setResolution (p.resolution);
        outT = std::pow (10.0f, p.outDb / 20.0f);
    }

    // L, R in place; scL/scR optional external key
    void process (float* L, float* R, const float* scL, const float* scR, int n) noexcept
    {
        const float sm = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
        const int dlMask = (int) dryDl[0].size() - 1;
        for (int i = 0; i < n; ++i)
        {
            const float x[2] { L[i], R[i] };
            // dry delay (latency N) for mix / delta
            float dry[2];
            for (int c = 0; c < 2; ++c) { dryDl[c][(size_t) dlPos] = x[c]; dry[c] = dryDl[c][(size_t) ((dlPos - N) & dlMask)]; }
            dlPos = (dlPos + 1) & dlMask;
            for (int c = 0; c < 2; ++c) inQ[c][(size_t) (N - hop + cnt)] = x[c];
            if (scL != nullptr) { scQ[0][(size_t) cnt] = scL[i]; scQ[1][(size_t) cnt] = scR != nullptr ? scR[i] : scL[i]; }
            else { scQ[0][(size_t) cnt] = 0.0f; scQ[1][(size_t) cnt] = 0.0f; }
            float wet[2] { ready[0][(size_t) cnt], ready[1][(size_t) cnt] };
            if (++cnt == hop) { cnt = 0; frame (scL != nullptr && p.sidechain); }

            agSm += (agGain - agSm) * sm * 0.05f;
            const float g = p.autoGain ? agSm : 1.0f;
            outSm += (outT - outSm) * sm;
            for (int c = 0; c < 2; ++c)
            {
                const float w = wet[c] * g;
                const float y = p.delta ? dry[c] - wet[c] : dry[c] + (w - dry[c]) * p.mix;
                (c == 0 ? L : R)[i] = y * outSm;
            }
        }
    }

    // UI
    std::array<float, kDisp> dispGr {}, dispBoost {}, dispIn {}, dispOut {};
    std::atomic<float> lastF0 { 0 }, lastConf { 0 };
    std::atomic<int> frameCounter { 0 };
    float avgReduction() const noexcept { return avgRed; }
    float maxReduction() const noexcept { return maxRed; }
    float autoGainDb() const noexcept { return 20.0f * std::log10 (std::max (1e-6f, agSm)); }
    double getFs() const noexcept { return fs; }

private:
    void updateTables()
    {
        const int nb = N / 2 + 1;
        if (std::abs (p.sharpness - lastSharp) > 1e-4f)
        {
            const float re = std::pow (2.0f, envWidthOct (p.sharpness) * 0.5f), rg = std::pow (2.0f, gainWidthOct (p.sharpness) * 0.5f);
            for (int k = 0; k < nb; ++k)
            {
                const float kk = (float) std::max (1, k);
                envLo[(size_t) k] = std::clamp ((int) std::floor (kk / re), 1, nb - 1); envHi[(size_t) k] = std::clamp ((int) std::ceil (kk * re), envLo[(size_t) k], nb - 1);
                envLo2[(size_t) k] = std::clamp ((int) std::floor (kk / (re * re)), 1, nb - 1); envHi2[(size_t) k] = std::clamp ((int) std::ceil (kk * re * re), envLo2[(size_t) k], nb - 1);
                gLo[(size_t) k] = std::clamp ((int) std::floor (kk / rg), 0, nb - 1); gHi[(size_t) k] = std::clamp ((int) std::ceil (kk * rg), gLo[(size_t) k], nb - 1);
                if (gHi[(size_t) k] - gLo[(size_t) k] < 2) { gLo[(size_t) k] = std::max (0, k - 1); gHi[(size_t) k] = std::min (nb - 1, k + 1); }
            }
            lastSharp = p.sharpness;
        }
        // sensitivity + range table
        float h = p.lowF * 1.3f + p.highF * 0.7f;
        for (auto& b : p.band) h = h * 1.0001f + b.freq * 0.37f + b.gainDb * 11.3f;
        if (std::abs (h - lastBandsHash) > 1e-3f)
        {
            for (int k = 0; k < nb; ++k)
            {
                const float f = (float) (k * fs / N);
                sensTab[(size_t) k] = std::pow (10.0f, std::clamp (sensDb (p, f), -60.0f, 18.0f) / 20.0f) * rangeWeight (p, f);
                // ear weighting (equal-loudness shaped): harshness 2-5 kHz counts more, the deep lows less
                const float d = std::log2 (std::max (1.0f, f) / 3200.0f) / 0.9f;
                earTab[(size_t) k] = 1.0f + 0.6f * std::exp (-0.5f * d * d) - 0.35f / (1.0f + std::pow (std::max (1.0f, f) / 120.0f, 3.0f));
            }
            lastBandsHash = h;
        }
    }

    // f0 of the key: YIN on the (decimated) mono time signal, then refined from the measured partial
    // frequencies of the spectrum (`pw`) -> octave-safe and accurate to a few cents
    float detectF0 (const float* pw, const float* ta, const float* tb, float& conf) noexcept
    {
        const int nb = N / 2 + 1;
        conf = 0;
        const int dec = std::max (1, (int) std::round (fs / 24000.0));
        const double fsd = fs / dec;
        const int Nd = N / dec;
        float* x = acBuf.data();
        double e = 0;
        float prevV = 0, hpY = 0; const float hpA = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 70.0 / fsd);
        for (int i = 0; i < Nd; ++i)
        {
            float v = 0; for (int j = 0; j < dec; ++j) v += ta[i * dec + j] + tb[i * dec + j];
            v *= 0.5f / (float) dec;
            hpY = hpA * (hpY + v - prevV); prevV = v; x[i] = hpY;      // 1-pole high-pass ~70 Hz: rumble stops faking short periods
            e += (double) x[i] * x[i];
        }
        if (e / Nd < 1e-9) return 0.0f;
        const int tMin = std::max (2, (int) std::floor (fsd / 1000.0)), tMax = std::min (Nd / 2, (int) std::ceil (fsd / 55.0));
        const int Wn = Nd - tMax;
        float* d = acBuf.data() + Nd + 4;
        double run = 0;
        d[0] = 1.0f;
        for (int t = 1; t <= tMax; ++t)
        {
            double acc = 0;
            for (int j = 0; j < Wn; ++j) { const float df = x[j] - x[j + t]; acc += (double) df * df; }
            run += acc;
            d[t] = run > 0 ? (float) (acc * t / run) : 1.0f;
        }
        // first local minimum that is (almost) as deep as the deepest one -> the shortest true period
        float m = 1e9f; for (int t = tMin; t < tMax; ++t) m = std::min (m, d[t]);
        const float lim = std::min (0.3f, m) * 1.2f + 0.06f;
        int bt = -1;
        for (int t = tMin + 1; t < tMax - 1; ++t)
            if (d[t] < lim && d[t] <= d[t - 1] && d[t] <= d[t + 1]) { bt = t; break; }
        if (bt < 0) for (int t = tMin; t < tMax; ++t) if (std::abs (d[t] - m) < 1e-9f) { bt = t; break; }
        if (bt <= tMin || bt >= tMax - 1) return 0.0f;
        const double y0 = d[bt - 1], y1 = d[bt], y2 = d[bt + 1], den = y0 - 2 * y1 + y2;
        const double tau = bt + (std::abs (den) > 1e-9 ? 0.5 * (y0 - y2) / den : 0.0);
        conf = std::clamp ((0.55f - d[bt]) / 0.3f, 0.0f, 1.0f);
        float f0 = (float) (fsd / tau);

        // refine from the spectrum's partials
        for (int k = 0; k < nb; ++k) lev[(size_t) k] = 10.0f * std::log10 (pw[k] + 1e-14f);
        prefix[0] = 0; for (int k = 0; k < nb; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + lev[(size_t) k];
        const float binHz = (float) (fs / N);
        auto partial = [&] (float f, float& fPrec) -> float
        {
            const int c0 = (int) std::round (f / binHz);
            if (c0 < 2 || c0 >= nb - 2) return 0.0f;
            int c = c0; for (int j = c0 - 1; j <= c0 + 1; ++j) if (lev[(size_t) j] > lev[(size_t) c]) c = j;
            const float a0 = lev[(size_t) c - 1], a1 = lev[(size_t) c], a2 = lev[(size_t) c + 1], dd = a0 - 2 * a1 + a2;
            const float dlt = std::abs (dd) > 1e-6f ? std::clamp (0.5f * (a0 - a2) / dd, -0.5f, 0.5f) : 0.0f;
            fPrec = ((float) c + dlt) * binHz;
            const int lo = envLo2[(size_t) c], hi = envHi2[(size_t) c];
            const float envP = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1));
            return a1 > std::max (a0, a2) ? std::max (0.0f, a1 - envP - 3.0f) : 0.0f;
        };
        float num = 0, dsum = 0;
        for (int h = 2; h <= 8; ++h)       // from the 2nd partial up: the fundamental bin is too coarse at low notes
        {
            float fp = 0; const float pr = partial (h * f0, fp);
            if (pr > 0.0f && std::abs (fp / (float) h - f0) < 0.03f * f0) { num += pr * (float) h * fp / (float) h; dsum += pr * (float) h; }
        }
        if (dsum > 0.0f) f0 = num / dsum;
        return f0;
    }

    // reduction (dB; positive = cut, negative = RECOVER boost) for one detection spectrum (power), written to `red`
    void computeReduction (const float* pw, int chan, float amount) noexcept
    {
        const int nb = N / 2 + 1;
        const float offs = 20.0f * std::log10 ((float) N / 4.0f);          // -> dBFS of a sine
        float maxLev = -200.0f;
        // detection level: 3-bin power average + one-hop memory -> random noise bins stop looking like resonances
        auto& ps = psm[chan];
        for (int k = 0; k < nb; ++k)
        {
            const float pk = (pw[std::max (0, k - 1)] + pw[k] + pw[std::min (nb - 1, k + 1)]) * (1.0f / 3.0f);
            ps[(size_t) k] = 0.5f * (ps[(size_t) k] + pk);
            const float use = std::max (ps[(size_t) k], pk * 0.6f);   // a sudden peak still shows at once
            lev[(size_t) k] = 10.0f * std::log10 (use + 1e-14f) - offs;
            if (sensTab[(size_t) k] > 0.0f) maxLev = std::max (maxLev, lev[(size_t) k]);
        }
        const float floorDb = std::max (-90.0f, maxLev - 70.0f);
        prefix[0] = 0; for (int k = 0; k < nb; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + lev[(size_t) k];
        for (int k = 0; k < nb; ++k) { const int lo = envLo[(size_t) k], hi = envHi[(size_t) k]; envL[(size_t) k] = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1)); }
        const float thr = selectThrDb (p.selectivity);
        const float strength = p.depth / 10.0f * amount;
        // transient detection: positive spectral flux in the processing range
        double flux = 0; int fluxN = 0;
        for (int k = 2; k < nb; ++k)
        {
            if (sensTab[(size_t) k] > 0.0f && lev[(size_t) k] > floorDb) { flux += std::max (0.0f, lev[(size_t) k] - prevLev[chan][(size_t) k]); ++fluxN; }
            prevLev[chan][(size_t) k] = lev[(size_t) k];
        }
        const float fl = fluxN > 0 ? (float) (flux / fluxN) : 0.0f;
        pgS[chan] = std::max (std::clamp ((fl - 1.5f) / 3.0f, 0.0f, 1.0f), pgS[chan] * 0.5f);   // hold ~2 hops
        const float pg = p.protect * pgS[chan];
        // TONE KEEP: protect the fundamental and the first harmonics of the played note
        std::fill (keepW.begin(), keepW.begin() + nb, 1.0f);
        const float f0 = curF0, conf = curConf;
        if (p.toneKeep > 0.0f && f0 > 0.0f && conf > 0.0f)
        {
            const float binHz = (float) (fs / N), kk = 1.0f - p.toneKeep * conf;
            for (int h = 1; h <= 8; ++h)
            {
                const float fc = h * f0, half = std::max (1.5f * binHz, 0.035f * fc);
                const int b0 = std::max (0, (int) std::floor ((fc - half) / binHz)), b1 = std::min (nb - 1, (int) std::ceil ((fc + half) / binHz));
                for (int b = b0; b <= b1; ++b) keepW[(size_t) b] = std::min (keepW[(size_t) b], kk);
            }
        }
        const float ht = (float) hop / (float) fs;
        const float cP = 1.0f - std::exp (-ht / 1.2f);                    // persistence memory ~1.2 s
        // raw cut / boost
        for (int k = 0; k < nb; ++k)
        {
            const float lv = lev[(size_t) k], d = lv - envL[(size_t) k], sens = sensTab[(size_t) k];
            const float ear = p.perceptual ? earTab[(size_t) k] : 1.0f;
            float r = lv < floorDb ? 0.0f : std::max (0.0f, d - thr) * sens * ear;
            // RESONANCE FOCUS: blend towards what keeps sticking out over time (stationary resonances)
            auto& pst = pers[chan][(size_t) k];
            pst += (r - pst) * cP;
            r += (std::min (r, pst * 1.25f) - r) * p.focus;
            if (! p.hard) r = r * r / (r + 6.0f);                          // soft knee
            float cut = std::min (p.depth * amount, r * strength);
            // RECOVER: lift persistent dips (masked areas) back towards the envelope
            float boost = 0.0f;
            if (p.recover > 0.0f)
            {
                const int l2 = envLo2[(size_t) k], h2 = envHi2[(size_t) k];
                const float envW = (float) ((prefix[(size_t) h2 + 1] - prefix[(size_t) l2]) / (h2 - l2 + 1));   // wide reference: holes don't hide in it
                const float dip = (lv > floorDb + 12.0f && sens > 0.0f) ? std::max (0.0f, envW - lv - 2.0f) * std::min (1.0f, sens) : 0.0f;
                auto& ds = dipSlow[chan][(size_t) k];
                ds += (dip - ds) * cP;
                boost = std::min (p.recover, ds);
            }
            env[(size_t) k] = cut - boost;
        }
        // shape across frequency: dilate (keeps the full depth on the resonance, widens it with lower SHARPNESS),
        // then box-smooth the edges. Cuts dilate by max, boosts by min (largest boost), separately.
        for (int k = 0; k < nb; ++k)
        {
            const int lo = gLo[(size_t) k], hi = gHi[(size_t) k];
            float mx = 0.0f, mn = 0.0f;
            for (int j = lo; j <= hi; ++j) { const float v = env[(size_t) j]; mx = std::max (mx, v); mn = std::min (mn, v); }
            red[(size_t) k] = mx + mn;
        }
        prefix[0] = 0; for (int k = 0; k < nb; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + red[(size_t) k];
        for (int k = 0; k < nb; ++k)
        {
            const int lo = gLo[(size_t) k], hi = gHi[(size_t) k];
            float v = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1));
            if (v > 0.0f) v *= keepW[(size_t) k];                          // TONE KEEP after shaping: the note's partials stay
            env[(size_t) k] = v;
        }
        // time smoothing per bin, transient protect on the applied value
        const float ca = 1.0f - std::exp (-ht / std::max (1e-4f, p.attackMs * 0.001f)), cr = 1.0f - std::exp (-ht / std::max (1e-4f, p.releaseMs * 0.001f));
        auto& st = state[chan];
        for (int k = 0; k < nb; ++k)
        {
            const float t = env[(size_t) k];
            st[(size_t) k] += (t - st[(size_t) k]) * (t > st[(size_t) k] ? ca : cr);
            red[(size_t) k] = st[(size_t) k] * (1.0f - pg);
        }
    }

    void frame (bool useSc) noexcept
    {
        auto& fft = *ffts[(size_t) res];
        const int nb = N / 2 + 1;
        updateTables();
        // shift-in sidechain into its queue
        for (int c = 0; c < 2; ++c)
        {
            auto& s = spec[c];
            for (int i = 0; i < N; ++i) s[(size_t) i] = inQ[c][(size_t) i] * win[(size_t) i];
            std::fill (s.begin() + N, s.begin() + 2 * N, 0.0f);
            fft.performRealOnlyForwardTransform (s.data(), true);
        }
        if (useSc)
            for (int c = 0; c < 2; ++c)
            {
                auto& q = scHist[c];
                std::memmove (q.data(), q.data() + hop, sizeof (float) * (size_t) (N - hop));
                std::memcpy (q.data() + N - hop, scQ[(size_t) c].data(), sizeof (float) * (size_t) hop);
                auto& s = scSpec[c];
                for (int i = 0; i < N; ++i) s[(size_t) i] = q[(size_t) i] * win[(size_t) i];
                std::fill (s.begin() + N, s.begin() + 2 * N, 0.0f);
                fft.performRealOnlyForwardTransform (s.data(), true);
            }

        curF0 = 0; curConf = 0;
        if (p.toneKeep > 0.0f)
        {
            const float* ka = useSc ? scSpec[0].data() : spec[0].data();
            const float* kb = useSc ? scSpec[1].data() : spec[1].data();
            for (int k = 0; k <= N / 2; ++k) power[(size_t) k] = 0.5f * (ka[2 * k] * ka[2 * k] + ka[2 * k + 1] * ka[2 * k + 1] + kb[2 * k] * kb[2 * k] + kb[2 * k + 1] * kb[2 * k + 1]);
            const float* ta = useSc ? scHist[0].data() : inQ[0].data();
            const float* tb = useSc ? scHist[1].data() : inQ[1].data();
            float cf = 0; const float f = detectF0 (power.data(), ta, tb, cf);
            curF0 = f; curConf = cf;
        }
        lastF0 = curF0; lastConf = curConf;
        float* A = spec[0].data(); float* B = spec[1].data();
        const float* KA = useSc ? scSpec[0].data() : A; const float* KB = useSc ? scSpec[1].data() : B;
        if (p.stereo == 1)   // mid / side: rotate audio (and key) into M/S
        {
            for (int k = 0; k < nb; ++k)
                for (int j = 0; j < 2; ++j) { const float a = A[2 * k + j], b = B[2 * k + j]; A[2 * k + j] = 0.5f * (a + b); B[2 * k + j] = 0.5f * (a - b); }
            if (useSc)
                for (int k = 0; k < nb; ++k)
                    for (int j = 0; j < 2; ++j) { float* sa = scSpec[0].data(); float* sb = scSpec[1].data(); const float a = sa[2 * k + j], b = sb[2 * k + j]; sa[2 * k + j] = 0.5f * (a + b); sb[2 * k + j] = 0.5f * (a - b); }
        }
        double ein = 0, eout = 0;
        auto pw = [] (const float* X, int k) { return X[2 * k] * X[2 * k] + X[2 * k + 1] * X[2 * k + 1]; };
        double redSum = 0; int redN = 0;
        auto apply = [&] (float* X, int k, float rdb) { const float g = std::pow (10.0f, -rdb / 20.0f); ein += pw (X, k); X[2 * k] *= g; X[2 * k + 1] *= g; eout += pw (X, k); };
        auto avg = [&] (int k, float rdb) { if (sensTab[(size_t) k] > 0.0f) { redSum += rdb; ++redN; } };

        if (p.stereo == 0)
        {
            for (int k = 0; k < nb; ++k) power[(size_t) k] = 0.5f * (pw (KA, k) + pw (KB, k));
            computeReduction (power.data(), 0, 1.0f);
            for (int k = 0; k < nb; ++k) { apply (A, k, red[(size_t) k]); apply (B, k, red[(size_t) k]); avg (k, red[(size_t) k]); grMix[(size_t) k] = red[(size_t) k]; }
        }
        else
        {
            const float amt[2] { p.stereo == 1 ? p.midAmt : 1.0f, p.stereo == 1 ? p.sideAmt : 1.0f };
            float* X[2] { A, B }; const float* K[2] { KA, KB };
            if (p.stereo == 1 && useSc) { K[0] = scSpec[0].data(); K[1] = scSpec[1].data(); }
            for (int c = 0; c < 2; ++c)
            {
                for (int k = 0; k < nb; ++k) power[(size_t) k] = pw (K[c], k);
                computeReduction (power.data(), c, amt[c]);
                for (int k = 0; k < nb; ++k) { apply (X[c], k, red[(size_t) k]); avg (k, red[(size_t) k]); grMix[(size_t) k] = c == 0 ? red[(size_t) k] : (std::abs (red[(size_t) k]) > std::abs (grMix[(size_t) k]) ? red[(size_t) k] : grMix[(size_t) k]); }
            }
        }
        avgRed = ein > 1e-9 ? (float) (10.0 * std::log10 ((ein + 1e-12) / (eout + 1e-12))) : 0.0f;
        (void) redSum; (void) redN;
        maxRed = 0; for (int k = 0; k < nb; ++k) maxRed = std::max (maxRed, grMix[(size_t) k]);
        // auto gain: slow energy match
        eIn = eIn * 0.97 + ein; eOut = eOut * 0.97 + eout;
        agGain = (float) std::clamp (std::sqrt ((eIn + 1e-12) / (eOut + 1e-12)), 1.0, 2.82);

        // display (input spectrum is the pre-reduction power, output post)
        for (int d = 0; d < kDisp; ++d)
        {
            float g = 0, bst = 0, pin = 0, pout = 0;
            for (int k = dispLo[(size_t) d]; k < dispHi[(size_t) d]; ++k)
            {
                g = std::max (g, grMix[(size_t) k]); bst = std::max (bst, -grMix[(size_t) k]);
                const float po = (pw (A, k) + pw (B, k)) * 0.5f;
                pout = std::max (pout, po);
                pin = std::max (pin, po * std::pow (10.0f, grMix[(size_t) k] / 10.0f));
            }
            if (dispHi[(size_t) d] - dispLo[(size_t) d] <= 2)
            {
                // few bins per point (the lows): interpolate at the point's centre instead of drawing steps
                const float cb = (float) (20.0 * std::pow (1000.0, (d + 0.5) / kDisp) * N / fs);
                const int i0 = std::clamp ((int) cb, 1, N / 2 - 1); const float fr = std::clamp (cb - (float) i0, 0.0f, 1.0f);
                auto lerp = [fr] (float a, float b) { return a + (b - a) * fr; };
                const float p0 = (pw (A, i0) + pw (B, i0)) * 0.5f, p1 = (pw (A, i0 + 1) + pw (B, i0 + 1)) * 0.5f;
                const float r0 = grMix[(size_t) i0], r1 = grMix[(size_t) i0 + 1];
                const float gi = lerp (r0, r1);
                pout = std::exp (lerp (std::log (p0 + 1e-20f), std::log (p1 + 1e-20f)));
                pin = pout * std::pow (10.0f, gi / 10.0f);
                g = std::max (0.0f, gi); bst = std::max (0.0f, -gi);
            }
            const float nrm = 4.0f / (float) N;
            dispGr[(size_t) d] = g; dispBoost[(size_t) d] = bst;
            dispIn[(size_t) d] = 10.0f * std::log10 (pin * nrm * nrm + 1e-14f);
            dispOut[(size_t) d] = 10.0f * std::log10 (pout * nrm * nrm + 1e-14f);
        }
        frameCounter.fetch_add (1, std::memory_order_release);

        if (p.stereo == 1)
            for (int k = 0; k < nb; ++k)
                for (int j = 0; j < 2; ++j) { const float m = A[2 * k + j], s = B[2 * k + j]; A[2 * k + j] = m + s; B[2 * k + j] = m - s; }

        for (int c = 0; c < 2; ++c)
        {
            auto& s = spec[c];
            fft.performRealOnlyInverseTransform (s.data());
            auto& acc = outAcc[c];
            for (int i = 0; i < N; ++i) acc[(size_t) i] += s[(size_t) i] * win[(size_t) i] * synthScale;
            std::memcpy (ready[c].data(), acc.data(), sizeof (float) * (size_t) hop);
            std::memmove (acc.data(), acc.data() + hop, sizeof (float) * (size_t) (N - hop));
            std::fill (acc.begin() + (N - hop), acc.begin() + N, 0.0f);
            // advance the input queue
            std::memmove (inQ[c].data(), inQ[c].data() + hop, sizeof (float) * (size_t) (N - hop));
        }
    }

    Params p;
    double fs = 48000;
    int maxN = 4096, N = 2048, hop = 512, res = 1, cnt = 0, dlPos = 0;
    std::array<std::unique_ptr<juce::dsp::FFT>, 3> ffts, acFfts;
    std::array<std::vector<float>, 3> wAc;
    std::vector<float> acBuf;
    float curF0 = 0, curConf = 0;
    std::vector<float> inQ[2], outAcc[2], ready[2], spec[2], scSpec[2], dryDl[2], scHist[2];
    std::array<std::vector<float>, 2> scQ { std::vector<float> (16384, 0.0f), std::vector<float> (16384, 0.0f) };
    std::vector<float> win, lev, env, red, sensTab, state[2], prevLev[2], pers[2], dipSlow[2], psm[2], envL, keepW, earTab;
    std::vector<float> power = std::vector<float> (16385, 0.0f), grMix = std::vector<float> (16385, 0.0f);
    std::vector<double> prefix;
    std::vector<int> envLo, envHi, envLo2, envHi2, gLo, gHi;
    std::array<int, kDisp> dispLo {}, dispHi {};
    float synthScale = 2.0f / 3.0f, lastSharp = -1, lastBandsHash = -1, avgRed = 0, maxRed = 0;
    float agGain = 1, agSm = 1, outT = 1, outSm = 1, pgS[2] {};
    double eIn = 1e-9, eOut = 1e-9;
};

} // namespace lumen::res
