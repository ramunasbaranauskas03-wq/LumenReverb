#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>

namespace lumen
{

struct Features
{
    float onsetRate = 0.0f;   // onsets per second
    float lowRatio  = 0.0f;   // energy below ~250 Hz / total
    float highRatio = 0.0f;   // energy above ~4 kHz / total
    float crestDb   = 0.0f;   // peak-to-RMS
    float levelDb   = -100.0f;
};

//==============================================================================
// Lightweight real-time signal analysis. process() runs on the audio thread;
// getLive(), requestLearn() and consumeLearnResult() are safe from the UI thread.
class SmartAnalyzer
{
public:
    static constexpr int kLearnFrames = 60; // 60 x 100 ms of non-silent audio

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        cLow  = 1.0f - std::exp (-2.0f * 3.14159265f * 250.0f / fs);
        cHigh = 1.0f - std::exp (-2.0f * 3.14159265f * 4000.0f / fs);
        fastA = coef (0.001f); fastR = coef (0.030f);
        slowA = coef (0.040f); slowR = coef (0.300f);
        frameLen = std::max (1, (int) (0.1f * fs));
        refractoryLen = (int) (0.07f * fs);
        zLow = zHigh = envF = envS = 0.0f;
        refractory = 0;
        resetFrame();
    }

    void process (const float* L, const float* R, int n) noexcept
    {
        if (learnRequested.exchange (false))
        {
            learnFrames = 0;
            accOn = accLow = accHigh = accCrest = accLevel = 0.0f;
            learnProgress.store (0.0f);
            learning.store (true);
        }
        if (cancelRequested.exchange (false))
            learning.store (false);

        for (int i = 0; i < n; ++i)
        {
            const float m = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
            zLow  += (m - zLow) * cLow;
            zHigh += (m - zHigh) * cHigh;
            const float hi = m - zHigh;

            sumAll  += m * m;
            sumLow  += zLow * zLow;
            sumHigh += hi * hi;

            const float a = std::fabs (m);
            peak = std::max (peak, a);
            envF += (a - envF) * (a > envF ? fastA : fastR);
            envS += (a - envS) * (a > envS ? slowA : slowR);

            if (refractory > 0)
                --refractory;
            else if (envF > 0.004f && envF > 1.8f * envS)
            {
                ++onsets;
                refractory = refractoryLen;
            }

            if (++frameCount >= frameLen)
                finishFrame();
        }
    }

    Features getLive() const noexcept
    {
        Features f;
        f.onsetRate = liveOn.load();
        f.lowRatio  = liveLow.load();
        f.highRatio = liveHigh.load();
        f.crestDb   = liveCrest.load();
        f.levelDb   = liveLevel.load();
        return f;
    }

    void requestLearn() noexcept  { learnRequested.store (true); }
    void cancelLearn() noexcept   { cancelRequested.store (true); }
    bool isLearning() const noexcept { return learning.load(); }
    float getLearnProgress() const noexcept { return learnProgress.load(); }

    bool consumeLearnResult (Features& out) noexcept
    {
        if (! learnDone.exchange (false, std::memory_order_acquire))
            return false;
        out.onsetRate = resOn.load();
        out.lowRatio  = resLow.load();
        out.highRatio = resHigh.load();
        out.crestDb   = resCrest.load();
        out.levelDb   = resLevel.load();
        return true;
    }

private:
    float coef (float sec) const { return 1.0f - std::exp (-1.0f / (sec * fs)); }

    void resetFrame() noexcept
    {
        sumAll = sumLow = sumHigh = 0.0f;
        peak = 0.0f; onsets = 0; frameCount = 0;
    }

    void finishFrame() noexcept
    {
        const float rms     = std::sqrt (sumAll / (float) frameCount);
        const float levelDb = 20.0f * std::log10 (rms + 1.0e-9f);
        const float crest   = 20.0f * std::log10 ((peak + 1.0e-9f) / (rms + 1.0e-9f));
        const float lowR    = std::clamp (sumLow  / (sumAll + 1.0e-12f), 0.0f, 1.0f);
        const float highR   = std::clamp (sumHigh / (sumAll + 1.0e-12f), 0.0f, 1.0f);
        const float rate    = (float) onsets / ((float) frameCount / fs);

        liveLevel.store (levelDb);

        if (levelDb > -55.0f)
        {
            constexpr float k = 0.08f; // ~1.2 s smoothing
            sOn    += (rate  - sOn)    * k;
            sLow   += (lowR  - sLow)   * k;
            sHigh  += (highR - sHigh)  * k;
            sCrest += (crest - sCrest) * k;
            liveOn.store (sOn); liveLow.store (sLow); liveHigh.store (sHigh); liveCrest.store (sCrest);

            if (learning.load())
            {
                accOn += rate; accLow += lowR; accHigh += highR; accCrest += crest; accLevel += levelDb;
                ++learnFrames;
                learnProgress.store ((float) learnFrames / (float) kLearnFrames);

                if (learnFrames >= kLearnFrames)
                {
                    const float inv = 1.0f / (float) learnFrames;
                    resOn.store (accOn * inv);   resLow.store (accLow * inv);
                    resHigh.store (accHigh * inv); resCrest.store (accCrest * inv);
                    resLevel.store (accLevel * inv);
                    learning.store (false);
                    learnDone.store (true, std::memory_order_release);
                }
            }
        }

        resetFrame();
    }

    float fs = 48000.0f;
    float cLow = 0, cHigh = 0, fastA = 0, fastR = 0, slowA = 0, slowR = 0;
    float zLow = 0, zHigh = 0, envF = 0, envS = 0;
    float sumAll = 0, sumLow = 0, sumHigh = 0, peak = 0;
    int onsets = 0, frameCount = 0, frameLen = 4800, refractory = 0, refractoryLen = 3360;
    float sOn = 0, sLow = 0, sHigh = 0, sCrest = 0;

    int learnFrames = 0;
    float accOn = 0, accLow = 0, accHigh = 0, accCrest = 0, accLevel = 0;

    std::atomic<float> liveOn { 0 }, liveLow { 0 }, liveHigh { 0 }, liveCrest { 0 }, liveLevel { -100 };
    std::atomic<float> resOn { 0 }, resLow { 0 }, resHigh { 0 }, resCrest { 0 }, resLevel { -100 };
    std::atomic<float> learnProgress { 0 };
    std::atomic<bool> learnRequested { false }, cancelRequested { false }, learning { false }, learnDone { false };
};

//==============================================================================
namespace SmartAssist
{
    enum class Material { Silence, Percussive, Bass, VocalLead, Pad, FullMix };

    inline const char* name (Material m)
    {
        switch (m)
        {
            case Material::Percussive: return "Drums / Percussion";
            case Material::Bass:       return "Bass";
            case Material::VocalLead:  return "Vocal / Lead";
            case Material::Pad:        return "Pad / Sustained";
            case Material::FullMix:    return "Full Mix";
            case Material::Silence:    break;
        }
        return "Waiting for audio";
    }

    inline Material classify (const Features& f)
    {
        if (f.levelDb < -55.0f)                           return Material::Silence;
        if (f.lowRatio > 0.55f && f.highRatio < 0.08f)    return Material::Bass;
        if (f.onsetRate > 3.5f && f.crestDb > 11.0f)      return Material::Percussive;
        if (f.onsetRate < 1.2f && f.crestDb < 12.0f)      return Material::Pad;
        if (f.lowRatio > 0.25f && f.highRatio > 0.06f && f.onsetRate > 2.0f)
                                                          return Material::FullMix;
        return Material::VocalLead;
    }

    // A complete suggestion in parameter units (algo index, seconds, ms, Hz, 0..1)
    struct Suggestion
    {
        int algo; float decay, size, preDelay, diffusion, early, bassMult, trebleMult, lowCut, highCut, width, modDepth, duck, clarity, mix;
    };

    // algo: 0 Hall 1 Plate 2 Room 3 Chamber 4 Cathedral 5 Bloom 6 Shimmer 7 Ambience
    inline Suggestion suggest (Material m, const Features& f, double bpm)
    {
        const bool hasTempo = bpm > 30.0 && bpm < 300.0;
        const float beat = hasTempo ? (float) (60.0 / bpm) : 0.5f;
        auto beats = [beat] (float n, float lo, float hi) { return std::clamp (n * beat, lo, hi); };
        const float pre16 = std::clamp (beat / 4.0f * 1000.0f, 5.0f, 150.0f), pre32 = std::clamp (beat / 8.0f * 1000.0f, 5.0f, 80.0f);

        Suggestion s {};
        switch (m)
        {
            case Material::Percussive: s = { 2, beats (2, 0.5f, 1.4f), 0.45f, std::min (pre32, 20.0f), 0.85f, 0.7f, 0.9f, 0.45f, 250, 11000, 1.1f, 0.2f, 0.25f, 0.35f, 0.18f }; break;
            case Material::Bass:       s = { 2, beats (1, 0.4f, 1.0f), 0.30f, 0.0f, 0.7f, 0.5f, 0.6f, 0.4f, 400, 6000, 0.8f, 0.15f, 0.3f, 0.4f, 0.10f }; break;
            case Material::Pad:        s = { 5, beats (8, 3.0f, 9.0f), 0.85f, 10.0f, 0.95f, 0.2f, 1.2f, 0.55f, 150, 14000, 1.3f, 0.6f, 0.0f, 0.15f, 0.35f }; break;
            case Material::FullMix:    s = { 3, beats (3, 0.9f, 2.0f), 0.50f, 15.0f, 0.8f, 0.5f, 1.0f, 0.5f, 300, 12000, 1.0f, 0.3f, 0.3f, 0.4f, 0.12f }; break;
            case Material::VocalLead:
            case Material::Silence:
            default:                   s = { 1, beats (4, 1.4f, 3.2f), 0.60f, pre16, 0.85f, 0.35f, 0.9f, 0.5f, 200, 11000, 1.1f, 0.4f, 0.35f, 0.45f, 0.22f }; break;
        }
        if (f.highRatio > 0.15f) s.trebleMult *= 0.75f;                 // bright source -> darker tail
        if (f.lowRatio  > 0.45f) s.lowCut = std::max (s.lowCut, 300.0f); // keep the low end clean
        return s;
    }
}

} // namespace lumen
