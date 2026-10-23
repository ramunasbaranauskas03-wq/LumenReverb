#pragma once
#include "EqDesign.h"
#include "Convolver.h"
#include <vector>
#include <atomic>

// =====================================================================================
// Lumen EQ 2 engine
//   24 bands, each Stereo / Mid / Side / Left / Right, static or dynamic.
//   ZERO LATENCY : analog-matched IIR sections, parameters glide (no zipper), dynamic bands
//                  follow a band-limited detector (own signal or the external side-chain).
//   LINEAR PHASE : all static bands -> one zero-phase 2x2 FIR matrix (L/R and M/S exact),
//                  partitioned FFT convolution, built off the audio thread, crossfaded in;
//                  dynamic bands run as matched IIR after it.
// =====================================================================================
namespace lumen::eq2
{
constexpr int kBands = 24;

struct BandParams
{
    bool used = false, on = true;
    int type = Bell, slope = 3, chan = Stereo;
    float freq = 1000, gain = 0, q = 1, dynRange = 0, thr = -30;
    bool sc = false;
    bool isDynamic() const noexcept { return used && on && std::abs (dynRange) > 0.05f && (type == Bell || type == LowShelf || type == HighShelf || type == Tilt); }
    bool hasGain() const noexcept { return type == Bell || type == LowShelf || type == HighShelf || type == Tilt; }
};

struct Globals { float scale = 1, attackMs = 10, releaseMs = 150; };

inline BandParams scaled (BandParams b, float scale) { b.gain *= scale; b.dynRange *= scale; return b; }

// response of one band at f (dB), with an optional extra gain (dynamic offset)
inline double bandDb (const BandParams& b, double fs, double f, float extraGain = 0)
{
    if (! b.used || ! b.on) return 0;
    return designMagDb (design (b.type, fs, b.freq, b.gain + extraGain, b.q, b.slope), fs, f);
}

// ---------------------------------------------------------------------------- IIR engine
class IirEq
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& b : st) { b = {}; }
        first = true;
    }
    void reset() { for (auto& b : st) { for (auto& c : b.sec) for (auto& s : c) s.reset(); b.det[0].reset(); b.det[1].reset(); b.env = 0; } }

    // dynOnly: process only the dynamic bands (linear-phase mode runs the static ones as FIR)
    void setBands (const std::array<BandParams, kBands>& p, const Globals& g, bool dynOnly)
    {
        params = p; glob = g; onlyDyn = dynOnly;
        atk = (float) std::exp (-1.0 / (std::max (0.1f, g.attackMs) * 0.001 * fs));
        rel = (float) std::exp (-1.0 / (std::max (1.0f, g.releaseMs) * 0.001 * fs));
        for (int k = 0; k < kBands; ++k)
        {
            auto& s = st[(size_t) k]; const auto bp = scaled (p[(size_t) k], g.scale);
            s.active = bp.used && bp.on && (! dynOnly || bp.isDynamic());
            if (! s.active) continue;
            s.target = bp;
            if (first || ! s.wasActive || s.cur.type != bp.type || s.cur.slope != bp.slope || s.cur.chan != bp.chan)
            {
                // hard (re)start: type / slope / routing changed -> no glide between different topologies
                s.cur = bp; s.lastDesign = {}; s.force = true;
                for (auto& c : s.sec) for (auto& x : c) x.reset();
            }
            // detector filter
            if (bp.isDynamic() && (std::abs (s.detF - bp.freq) > 0.01f || s.detT != bp.type || std::abs (s.detQ - bp.q) > 0.001f))
            {
                s.detF = bp.freq; s.detT = bp.type; s.detQ = bp.q;
                for (auto& d : s.det)
                {
                    if (bp.type == Bell) d.c = matched ({ 0, 1 / std::max (0.3, (double) bp.q), 0, 1, 1 / std::max (0.3, (double) bp.q), 1, 2 }, 2 * kPi * bp.freq / fs);
                    else if (bp.type == LowShelf) d.c = matched ({ 1, 0, 0, 1, 1.4142, 1, 2 }, 2 * kPi * bp.freq / fs);
                    else if (bp.type == HighShelf) d.c = matched ({ 0, 0, 1, 1, 1.4142, 1, 2 }, 2 * kPi * bp.freq / fs);
                    else d.c = Coefs {};
                }
            }
        }
        for (int k = 0; k < kBands; ++k) st[(size_t) k].wasActive = st[(size_t) k].active;
        first = false;
    }

    // L / R in place; scL / scR may be null (no side-chain)
    void process (float* L, float* R, const float* scL, const float* scR, int n) noexcept
    {
        bool anyLR = false, anyMS = false;
        for (auto& s : st) if (s.active) { if (s.cur.chan == Mid || s.cur.chan == Side) anyMS = true; else anyLR = true; }
        if (! anyLR && ! anyMS) return;
        const float glide = 1.0f - (float) std::exp (-1.0 / (0.03 * fs / kSub));
        for (int start = 0; start < n; start += kSub)
        {
            const int len = std::min (kSub, n - start);
            // per sub-block: glide parameters, follow the detectors, redesign where needed
            for (int k = 0; k < kBands; ++k)
            {
                auto& s = st[(size_t) k]; if (! s.active) continue;
                auto& c = s.cur; const auto& t = s.target;
                c.freq = (float) std::exp (std::log (c.freq) + (std::log (t.freq) - std::log (c.freq)) * glide);
                c.gain += (t.gain - c.gain) * glide; c.q = (float) std::exp (std::log (c.q) + (std::log (t.q) - std::log (c.q)) * glide);
                c.dynRange = t.dynRange; c.thr = t.thr; c.sc = t.sc; c.on = t.on;
                float extra = 0;
                if (t.isDynamic())
                {
                    // detector on the band's own region of the input (or the side-chain)
                    double e = s.env;
                    for (int i = 0; i < len; ++i)
                    {
                        double x;
                        if (c.sc && scL != nullptr) x = 0.5 * (scL[start + i] + (scR ? scR[start + i] : scL[start + i]));
                        else if (c.chan == Left) x = L[start + i];
                        else if (c.chan == Right) x = R[start + i];
                        else if (c.chan == Side) x = 0.5 * (L[start + i] - R[start + i]);
                        else x = 0.5 * (L[start + i] + R[start + i]);
                        if (c.type != Tilt) x = s.det[1].process (s.det[0].process (x));
                        const double p = x * x;
                        e = p > e ? p + atk * (e - p) : p + rel * (e - p);
                    }
                    s.env = e;
                    const float lvl = (float) (10.0 * std::log10 (s.env * 2.0 + 1e-12));
                    const float amt = std::clamp ((lvl - c.thr) / 10.0f, 0.0f, 1.0f);
                    s.amount += (amt - s.amount) * 0.5f;
                    extra = c.dynRange * s.amount;
                }
                s.extraNow = extra;
                const float g = c.gain + extra;
                // while moving: the fast 3-point design; once settled (static bands): the precise least-squares design
                const bool settled = ! t.isDynamic() && std::abs (c.freq - t.freq) < t.freq * 1e-5f && std::abs (c.gain - t.gain) < 1e-4f && std::abs (c.q - t.q) < t.q * 1e-5f;
                if (settled) { c.freq = t.freq; c.gain = t.gain; c.q = t.q; }
                const bool changed = std::abs (c.freq - s.lastDesign.freq) > c.freq * 1e-6f || std::abs (g - s.lastDesign.gain) > 1e-5f || std::abs (c.q - s.lastDesign.q) > 1e-6f;
                if (s.force || changed || (settled && ! s.precise))
                {
                    const auto d = design (c.type, fs, c.freq, settled ? c.gain : g, c.q, c.slope, settled);
                    s.n = d.n; for (int q = 0; q < d.n; ++q) { s.sec[(size_t) q][0].c = d.s[(size_t) q]; s.sec[(size_t) q][1].c = d.s[(size_t) q]; }
                    s.lastDesign = c; s.lastDesign.gain = settled ? c.gain : g; s.force = false; s.precise = settled;
                }
            }
            // audio
            for (int i = start; i < start + len; ++i)
            {
                double l = L[i], r = R[i];
                if (anyLR)
                    for (auto& s : st)
                    {
                        if (! s.active || s.cur.chan == Mid || s.cur.chan == Side) continue;
                        const int ch = s.cur.chan;
                        for (int q = 0; q < s.n; ++q)
                        {
                            if (ch != Right) l = s.sec[(size_t) q][0].process (l);
                            if (ch != Left) r = s.sec[(size_t) q][1].process (r);
                        }
                    }
                if (anyMS)
                {
                    double m = 0.5 * (l + r), sd = 0.5 * (l - r);
                    for (auto& s : st)
                    {
                        if (! s.active || (s.cur.chan != Mid && s.cur.chan != Side)) continue;
                        for (int q = 0; q < s.n; ++q) { if (s.cur.chan == Mid) m = s.sec[(size_t) q][0].process (m); else sd = s.sec[(size_t) q][0].process (sd); }
                    }
                    l = m + sd; r = m - sd;
                }
                L[i] = (float) l; R[i] = (float) r;
            }
        }
    }

    float dynamicGain (int band) const noexcept { return st[(size_t) band].active ? st[(size_t) band].extraNow : 0.0f; }

private:
    static constexpr int kSub = 16;
    struct State
    {
        bool active = false, wasActive = false, force = true, precise = false;
        BandParams cur, target, lastDesign;
        std::array<std::array<Section, 2>, 8> sec {}; int n = 0;
        Section det[2]; float detF = -1, detQ = -1; int detT = -1;
        double env = 0; float amount = 0, extraNow = 0;
    };
    double fs = 48000; bool first = true, onlyDyn = false;
    std::array<BandParams, kBands> params {}; Globals glob;
    std::array<State, kBands> st {};
    float atk = 0.99f, rel = 0.999f;
};

// ---------------------------------------------------------------------------- linear phase
// static response as a 2x2 matrix per frequency:  out = MS^-1 diag(HM, HS) MS diag(HL, HR) in
struct Matrix { double h11, h12, h21, h22; };
struct StaticCurve
{
    std::array<Design, kBands> d {}; std::array<int, kBands> chan {}; std::array<bool, kBands> active {}; double fs = 48000;
    void set (const std::array<BandParams, kBands>& p, float scale, double sampleRate, bool excludeDynamic)
    {
        fs = sampleRate;
        for (int k = 0; k < kBands; ++k)
        {
            const auto& raw = p[(size_t) k];
            active[(size_t) k] = raw.used && raw.on && ! (excludeDynamic && raw.isDynamic());
            if (! active[(size_t) k]) continue;
            const auto b = scaled (raw, scale);
            d[(size_t) k] = design (b.type, fs, b.freq, b.gain, b.q, b.slope); chan[(size_t) k] = b.chan;
        }
    }
    Matrix at (double f) const
    {
        double hl = 1, hr = 1, hm = 1, hs = 1; const double w = 2 * kPi * f / fs;
        for (int k = 0; k < kBands; ++k)
        {
            if (! active[(size_t) k]) continue;
            double m = 1; for (int q = 0; q < d[(size_t) k].n; ++q) m *= magnitude (d[(size_t) k].s[(size_t) q], w);
            switch (chan[(size_t) k]) { case Stereo: hl *= m; hr *= m; break; case Left: hl *= m; break; case Right: hr *= m; break; case Mid: hm *= m; break; default: hs *= m; break; }
        }
        return { 0.5 * (hm + hs) * hl, 0.5 * (hm - hs) * hr, 0.5 * (hm - hs) * hl, 0.5 * (hm + hs) * hr };
    }
};
inline Matrix staticMatrix (const std::array<BandParams, kBands>& p, float scale, double fs, double f, bool excludeDynamic)
{
    StaticCurve c; c.set (p, scale, fs, excludeDynamic); return c.at (f);
}

class LinearEq
{
public:
    static int firLength (double fs, int quality) { const double sec[] { 0.085, 0.17, 0.34 }; int n = 1; while (n < fs * sec[std::clamp (quality, 0, 2)]) n <<= 1; return n; }
    static constexpr int kBlock = 512;

    void prepare (double sampleRate, int quality)
    {
        fs = sampleRate; N = firLength (fs, quality);
        for (auto& c : conv) c.prepare (kBlock, N / kBlock);
        tmp.setSize (4, 8192);
    }
    int latency() const { return N / 2 + kBlock; }
    int length() const { return N; }
    PartitionedConvolver& convA() { return conv[0]; }
    PartitionedConvolver& convB() { return conv[1]; }
    bool crossTalk() const { return cross.load(); }

    // worker thread: build both convolver IRs (A: L-> (outL, outR), B: R -> (outL, outR))
    void build (const std::array<BandParams, kBands>& p, float scale, bool immediate)
    {
        int order = 0; while ((1 << order) < N) ++order;
        juce::dsp::FFT big (order);
        const int half = N / 2;
        std::vector<float> w ((size_t) (2 * N));
        { std::fill (w.begin(), w.end(), 0.0f); w[0] = 1; big.performRealOnlyForwardTransform (w.data()); big.performRealOnlyInverseTransform (w.data()); }
        const float bigScale = 1.0f / w[0];
        std::vector<Matrix> mx ((size_t) half + 1);
        bool ms = false;
        StaticCurve curve; curve.set (p, scale, fs, true);
        for (int k = 0; k <= half; ++k) { mx[(size_t) k] = curve.at (std::max (1.0, k * fs / N)); ms |= std::abs (mx[(size_t) k].h12) > 1e-9 || std::abs (mx[(size_t) k].h21) > 1e-9; }
        auto fir = [&] (int which, std::vector<float>& h)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            for (int k = 0; k <= half; ++k)
            {
                const auto& m = mx[(size_t) k];
                const double v = which == 0 ? m.h11 : which == 1 ? m.h21 : which == 2 ? m.h12 : m.h22;
                w[(size_t) (2 * k)] = (float) v;
                if (k > 0 && k < half) w[(size_t) (2 * (N - k))] = (float) v;
            }
            big.performRealOnlyInverseTransform (w.data());
            h.resize ((size_t) N);
            for (int i = 0; i < N; ++i)
            {
                const int src = (i - half + N) % N;
                const double win = 0.42 - 0.5 * std::cos (2 * kPi * (i + 0.5) / N) + 0.08 * std::cos (4 * kPi * (i + 0.5) / N);
                h[(size_t) i] = (float) (w[(size_t) src] * bigScale * win);
            }
        };
        std::vector<float> h11, h21, h12, h22;
        fir (0, h11); fir (1, h21); fir (2, h12); fir (3, h22);
        auto* a = makeIR (h11, h21); auto* b = makeIR (h12, h22);
        if (immediate) { conv[0].setImmediately (a); conv[1].setImmediately (b); }
        else { conv[0].submit (a); conv[1].submit (b); }
        cross.store (ms);
    }

    // in place; tmp work space
    void process (float* L, float* R, int n) noexcept
    {
        for (int start = 0; start < n; start += 8192)
        {
            const int len = std::min (8192, n - start);
            float* a0 = tmp.getWritePointer (0); float* a1 = tmp.getWritePointer (1); float* b0 = tmp.getWritePointer (2); float* b1 = tmp.getWritePointer (3);
            for (int i = 0; i < len; ++i) { a0[i] = a1[i] = L[start + i]; b0[i] = b1[i] = R[start + i]; }
            conv[0].process (a0, a1, len); conv[1].process (b0, b1, len);
            for (int i = 0; i < len; ++i) { L[start + i] = a0[i] + b0[i]; R[start + i] = a1[i] + b1[i]; }
        }
    }

private:
    PartitionedConvolver::IR* makeIR (const std::vector<float>& h0, const std::vector<float>& h1)
    {
        const int B = conv[0].blockSize(), bins = B + 1;
        auto* ir = new PartitionedConvolver::IR(); ir->parts = N / B;
        int pOrder = 0; while ((1 << pOrder) < 2 * B) ++pOrder;
        juce::dsp::FFT small (pOrder); std::vector<float> pw ((size_t) (4 * B));
        const std::vector<float>* src[2] { &h0, &h1 };
        for (int c = 0; c < 2; ++c)
        {
            ir->h[c].assign ((size_t) (ir->parts * bins), {});
            for (int p = 0; p < ir->parts; ++p)
            {
                std::fill (pw.begin(), pw.end(), 0.0f);
                for (int i = 0; i < B; ++i) pw[(size_t) i] = (*src[c])[(size_t) (p * B + i)];
                small.performRealOnlyForwardTransform (pw.data(), true);
                for (int k = 0; k < bins; ++k) ir->h[c][(size_t) (p * bins + k)] = { pw[(size_t) (2 * k)], pw[(size_t) (2 * k + 1)] };
            }
        }
        return ir;
    }
    double fs = 48000; int N = 8192;
    PartitionedConvolver conv[2];
    juce::AudioBuffer<float> tmp;
    std::atomic<bool> cross { false };
};

// ---------------------------------------------------------------------------- auto gain
// loudness change of the static curve for pink-noise-like music (equal weight per octave, K-ish weighting)
inline float autoGainDb (const std::array<BandParams, kBands>& p, float scale, double fs)
{
    double num = 0, den = 0;
    StaticCurve curve; curve.set (p, scale, fs, false);
    for (double f = 25; f < 18000; f *= 1.06)
    {
        const auto m = curve.at (f);
        const double pw = 0.5 * (m.h11 * m.h11 + m.h12 * m.h12 + m.h21 * m.h21 + m.h22 * m.h22);
        const double kw = f < 100 ? 0.5 : f > 1500 ? 1.4 : 1.0;
        num += pw * kw; den += kw;
    }
    return (float) (-10.0 * std::log10 (num / den + 1e-12));
}
}
