#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <cmath>

// =====================================================================================
// Lumen Reference analysis: tonal profile (1/6-octave, loudness-independent), per-band
// stereo width / correlation, loudness (BS.1770 integrated, LRA), true peak and PLR.
// =====================================================================================
namespace lumen::ref
{

constexpr int kPoints = 61;          // 1/6 octave, 20 Hz .. 20 kHz
constexpr int kBands  = 6;
constexpr int kOrder  = 13;          // FFT 8192
constexpr int kFft    = 1 << kOrder;

inline float pointFreq (int i) { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPoints - 1)); }
inline const std::array<float, kBands + 1>& bandEdges() { static const std::array<float, kBands + 1> e { 20, 60, 200, 800, 3000, 8000, 20000 }; return e; }
inline const juce::StringArray& bandNames() { static const juce::StringArray s { "SUB", "LOW", "LOW-MID", "MID", "HIGH-MID", "AIR" }; return s; }

struct Profile
{
    bool valid = false;
    float spec[kPoints] {};          // dB, normalised so the 100 Hz..5 kHz average is 0 dB
    float bandDb[kBands] {};         // average of spec[] inside each band
    float bandWidth[kBands] {};      // side/mid amplitude ratio x 100 (0 = mono, 100 = side as loud as mid)
    float bandCorr[kBands] {};       // L/R correlation per band (-1..1)
    float lufsI = -100.0f, lufsS = -100.0f, lra = 0.0f, truePeakDb = -100.0f, plr = 0.0f;
};

// Accumulates averaged L/R power spectra and cross-spectrum (Hann, 50 % overlap).
class SpecAccum
{
public:
    void prepare (double sampleRate);
    void reset();
    void setTimeConstant (float seconds) { tau = seconds; }          // <= 0 -> infinite average
    void push (const float* L, const float* R, int n);
    void computeSpectral (Profile& p) const;                         // fills spec / band / width / corr
    bool hasData() const { return frames > 0; }

private:
    void frame();
    double fs = 48000.0;
    float tau = 4.0f;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> win, bufL, bufR, fl, fr;
    int fill = 0;
    double frames = 0;
    std::vector<double> pL, pR, pM, pS, cx;     // per bin
};

// Full offline analysis of a stereo buffer region (reference tracks).
Profile analyseBuffer (const juce::AudioBuffer<float>& buf, int start, int end, double fs);

// Loudness range (EBU 3342) from a list of short-term loudness values.
float loudnessRange (std::vector<float> st);

// Plain-language advice comparing a mix against the reference (max `maxLines`).
juce::StringArray advise (const Profile& mix, const Profile& ref, int maxLines = 6);

} // namespace lumen::ref
