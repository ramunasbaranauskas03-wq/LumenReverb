#pragma once
#include "Eq.h"
#include "Spectral.h"
#include "Modules.h"

// =====================================================================================
// The full mastering chain. Used by the plugin in real time and by the Master Assistant
// offline (same code, so what the assistant predicts is what you hear).
//   in gain -> EQ -> Smart Balance -> Bass -> Multiband -> Glue -> Saturate -> Imager
//          -> Clipper -> Maximizer -> out gain -> Dither
// Latency is constant for a given "Low latency" setting (modules keep running while off,
// so switching them never shifts the timing).
// =====================================================================================
namespace lumen::master
{
enum Module { MEq, MShaper, MBass, MMulti, MGlue, MSat, MImager, MClip, MMax, MDither, kModules };

struct Settings
{
    float inGain = 0, outGain = 0; bool lowLat = false;
    bool on[kModules] { false, false, false, false, false, false, false, false, true, false };
    int eqMode = 1; EqSet eq {};
    ShaperParams sh; BassParams bass; MbParams mb; GlueParams glue; SatParams sat; ImagerParams img; ClipParams clip; MaxParams mx;
    int dither = 0;
};

class MasterChain
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate; maxBlock = std::max (16, maxBlockSize);
        eq.prepare (fs);
        eq.setBands ({}); eq.buildNow();
        shaper.prepare (fs);
        bass.prepare (fs); multi.prepare (fs); glue.prepare (fs);
        sat.prepare (fs, maxBlock); imager.prepare (fs); clip.prepare (fs, maxBlock); maxi.prepare (fs);
        for (int c = 0; c < 2; ++c) { tmp[c].assign ((size_t) maxBlock, 0.0f); dry[c].assign ((size_t) maxBlock, 0.0f); }
        for (auto& f : fade) f = -1.0f;
    }

    int latency (bool lowLat) const
    {
        const int fixed = sat.latency() + clip.latency() + maxi.latency();
        return lowLat ? fixed : fixed + eq.linearLatency() + shaper.latency();
    }

    // in place, n <= maxBlock
    void process (float* L, float* R, int n, const Settings& s) noexcept
    {
        if (n <= 0) return;
        if (s.inGain != 0.0f) { const float g = (float) dbToGain (s.inGain); for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; } }
        eq.setBands (s.eq);
        // EQ: in full mode both the linear and the (delay-compensated) natural path always run,
        // so switching mode or bypass is seamless and the latency never changes
        if (! s.lowLat)
        {
            copyTo (L, R, n, tmp);
            eq.processLinear (L, R, n);
            eq.processNatural (tmp[0].data(), tmp[1].data(), n, s.on[MEq] && s.eqMode == 0, true);
            if (! (s.on[MEq] && s.eqMode == 1)) { std::copy (tmp[0].begin(), tmp[0].begin() + n, L); std::copy (tmp[1].begin(), tmp[1].begin() + n, R); }
        }
        else if (s.on[MEq]) eq.processNatural (L, R, n, true, false);
        if (! s.lowLat) { shaper.setParams (s.sh); shaper.process (L, R, n, s.on[MShaper]); }
        run (MBass,  s.on[MBass],  L, R, n, [&] { bass.process (L, R, n, s.bass); });
        run (MMulti, s.on[MMulti], L, R, n, [&] { multi.process (L, R, n, s.mb); });
        run (MGlue,  s.on[MGlue],  L, R, n, [&] { glue.process (L, R, n, s.glue); });
        { auto sp = s.sat; sp.on = s.on[MSat]; sat.process (L, R, n, sp); }
        run (MImager, s.on[MImager], L, R, n, [&] { imager.process (L, R, n, s.img); });
        { auto cp = s.clip; cp.on = s.on[MClip]; clip.process (L, R, n, cp); }
        { auto mp = s.mx; mp.on = s.on[MMax]; if (! mp.on) mp.delta = false; maxi.process (L, R, n, mp); }
        if (s.outGain != 0.0f) { const float g = (float) dbToGain (s.outGain); for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; } }
        if (s.on[MDither]) dither.process (L, R, n, s.dither);
    }

    void serviceBackground() { eq.service(); }
    void buildEqNow() { eq.buildNow(); }

    MasterEq eq; SpectralShaper shaper; BassControl bass; MultibandDynamics multi; Glue glue;
    Saturation sat; Imager imager; Clipper clip; Maximizer maxi; Dither dither;

private:
    static void copyTo (const float* L, const float* R, int n, std::vector<float>* dst) { std::copy (L, L + n, dst[0].data()); std::copy (R, R + n, dst[1].data()); }

    // runs a module with a 10 ms crossfade when it is switched on or off
    template <typename F> void run (int m, bool on, float* L, float* R, int n, F&& fn) noexcept
    {
        float& f = fade[(size_t) m];
        if (f < 0) f = on ? 1.0f : 0.0f;             // first block: no fade
        const float target = on ? 1.0f : 0.0f;
        if (f == 0.0f && target == 0.0f) return;
        if (f == 1.0f && target == 1.0f) { fn(); return; }
        copyTo (L, R, n, dry);
        fn();
        const float step = 1.0f / (float) (0.01 * fs);
        for (int i = 0; i < n; ++i)
        {
            f = target > f ? std::min (target, f + step) : std::max (target, f - step);
            L[i] = dry[0][(size_t) i] + (L[i] - dry[0][(size_t) i]) * f;
            R[i] = dry[1][(size_t) i] + (R[i] - dry[1][(size_t) i]) * f;
        }
    }

    double fs = 48000; int maxBlock = 512;
    std::vector<float> tmp[2], dry[2];
    std::array<float, kModules> fade {};
};
} // namespace lumen::master
