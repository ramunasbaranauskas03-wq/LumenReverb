#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

// ============================================================================
//  Lumen Reverse — DSP engine
//  8-line modulated FDN reverb with six characters, octave-up shimmer in the
//  feedback loop, and an offline renderer that builds reverse swells.
// ============================================================================

namespace lumen
{

struct DelayLine
{
    std::vector<float> buf;
    int w = 0, mask = 0;

    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // d = samples ago (0 = newest)
    inline float read (float d) const
    {
        float rp = (float) (w - 1) - d;
        while (rp < 0.0f) rp += (float) (mask + 1);
        int i0 = (int) rp;
        float f = rp - (float) i0;
        float a = buf[(size_t) (i0 & mask)];
        float b = buf[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * f;
    }
    inline float readInt (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
};

struct Allpass
{
    DelayLine dl;
    int len = 1;
    float g = 0.5f;
    void prepare (int maxSamples) { dl.prepare (maxSamples); }
    void clear() { dl.clear(); }
    inline float process (float x)
    {
        float d = dl.readInt (len - 1);
        float v = x + g * d;
        float y = d - g * v;
        dl.push (v);
        return y;
    }
};

// Two-tap crossfading pitch shifter (sin^2 / cos^2 windows sum to unity)
struct PitchShifter
{
    DelayLine dl;
    float phase = 0.0f, window = 2048.0f, ratio = 2.0f;
    void prepare (double sr)
    {
        window = (float) (sr * 0.045);
        dl.prepare ((int) window * 2 + 8);
        phase = 0.0f;
    }
    void clear() { dl.clear(); phase = 0.0f; }
    inline float process (float x)
    {
        dl.push (x);
        phase += (ratio - 1.0f) / window;
        if (phase >= 1.0f) phase -= 1.0f;
        float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        float d1 = window * (1.0f - phase) + 1.0f;
        float d2 = window * (1.0f - p2) + 1.0f;
        float g1 = std::sin (juce::MathConstants<float>::pi * phase); g1 *= g1;
        float g2 = std::sin (juce::MathConstants<float>::pi * p2);    g2 *= g2;
        return dl.read (d1) * g1 + dl.read (d2) * g2;
    }
};

enum class Algo { Room = 0, Hall, Plate, Cathedral, Shimmer, Bloom };

struct ReverbParams
{
    int   algo = 1;
    float size = 0.6f;       // 0..1
    float decay = 4.0f;      // RT60 seconds
    float predelayMs = 0.0f;
    float diffusion = 0.7f;  // 0..1
    float dampHz = 9000.0f;
    float mod = 0.3f;        // 0..1
    float shimmer = 0.0f;    // 0..1
};

class FDNReverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate);
    void setParams (const ReverbParams& p);
    void reset();
    void process (float inL, float inR, float& outL, float& outR);

private:
    double sr = 48000.0;
    std::array<DelayLine, N> lines;
    std::array<float, N> len {}, gain {}, lp {}, lfoPhase {}, lfoInc {};
    float modDepthSamples = 0.0f, dampCoef = 0.0f, shimmerAmt = 0.0f, shimNorm = 1.0f;
    DelayLine preL, preR;
    float predelaySamples = 0.0f;
    std::array<Allpass, 6> diffL, diffR;
    int numDiff = 4;
    PitchShifter shifter;
    float shimLP = 0.0f;
};

// ---------------------------------------------------------------------------

struct RenderSettings
{
    double sampleRate = 48000.0;
    int    swellSamples = 48000;
    int    mode = 0;            // 0 = Reverse Tail, 1 = Pre-Verb (reverse-reverb-reverse)
    ReverbParams reverb;
    float  rise = 1.0f;         // envelope exponent (0 = natural)
    float  tailFadeMs = 5.0f;
    float  sourceFadeMs = 5.0f;
    float  sourceBlend = 0.0f;
    float  sweep = 0.0f;        // 0..1 low-pass opening across the swell
    float  lowCut = 20.0f, highCut = 20000.0f;
    bool   normalize = true;
};

struct RenderResult
{
    juce::AudioBuffer<float> audio;   // 2ch, [swell | source region]
    int swellLen = 0, srcLen = 0;
    std::vector<float> peaks;         // min/max pairs, numCols
    std::vector<float> envelope;      // 256 points over swell region
    int numCols = 0;
    int version = 0;
};

struct Renderer
{
    static std::unique_ptr<RenderResult> render (const juce::AudioBuffer<float>& source,
                                                 const RenderSettings& s);
    static float envelopeAt (float frac, const RenderSettings& s, int swellLen, int pos);
};

} // namespace lumen
