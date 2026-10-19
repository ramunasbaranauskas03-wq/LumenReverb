#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// small built-in sound so the chords / arp can be heard right away
namespace lumen::chord
{
struct PreviewSound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class PreviewVoice : public juce::SynthesiserVoice
{
public:
    explicit PreviewVoice (const int& typeRef) : type (typeRef) {}
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<PreviewSound*> (s) != nullptr; }
    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        freq = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        vel = velocity; t = 0; rel = false; relLevel = 0;
        for (int k = 0; k < 3; ++k) ph[k] = (double) k * 0.33;
        lp1 = lp2 = 0;
    }
    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) { rel = true; relT = 0; relLevel = env; }
        else clearCurrentNote();
    }
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int n) override
    {
        if (! isVoiceActive()) return;
        const double fs = getSampleRate();
        const int ty = type;
        // envelopes per sound
        const double atk = ty == 2 ? 0.25 : 0.003, dec = ty == 1 ? 0.18 : ty == 3 ? 0.9 : 0.4, sus = ty == 1 ? 0.0 : ty == 3 ? 0.25 : 0.75, relS = ty == 2 ? 0.6 : 0.15;
        for (int i = 0; i < n; ++i)
        {
            const double ts = t / fs;
            double e = ts < atk ? ts / atk : sus + (1.0 - sus) * std::exp (-(ts - atk) / dec);
            if (rel) { e = relLevel * std::exp (-relT / (relS * fs)); relT += 1.0; if (e < 1e-4) { clearCurrentNote(); break; } }
            env = e;
            double s = 0;
            if (ty == 3)
            {
                ph[0] += freq / fs; ph[0] -= std::floor (ph[0]);
                s = std::sin (2 * juce::MathConstants<double>::pi * ph[0]) + 0.3 * std::sin (4 * juce::MathConstants<double>::pi * ph[0]) * std::exp (-ts * 4.0);
            }
            else
            {
                static const double det[3] { 0.0, 0.11, -0.13 };
                for (int k = 0; k < 3; ++k)
                {
                    const double f = freq * std::pow (2.0, det[k] / 12.0);
                    ph[k] += f / fs; ph[k] -= std::floor (ph[k]);
                    // polyBLEP saw
                    const double dt = f / fs; double v = 2.0 * ph[k] - 1.0;
                    if (ph[k] < dt) { const double x = ph[k] / dt; v -= x + x - x * x - 1.0; }
                    else if (ph[k] > 1.0 - dt) { const double x = (ph[k] - 1.0) / dt; v -= x * x + x + x + 1.0; }
                    s += v / 3.0;
                }
            }
            // filter: pluck closes fast, pad is soft
            const double cutoff = ty == 1 ? 600.0 + 6000.0 * std::exp (-ts / 0.08) : ty == 2 ? 1800.0 : ty == 3 ? 20000.0 : 5000.0;
            const double a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * std::min (cutoff, fs * 0.45) / fs);
            lp1 += (s - lp1) * a; lp2 += (lp1 - lp2) * a;
            const float y = (float) (lp2 * e * vel * 0.18);
            out.addSample (0, start + i, y);
            if (out.getNumChannels() > 1) out.addSample (1, start + i, y);
            t += 1.0;
        }
    }
private:
    const int& type;
    double freq = 440, ph[3] {}, t = 0, relT = 0, env = 0, relLevel = 0, lp1 = 0, lp2 = 0; float vel = 1; bool rel = false;
};
} // namespace lumen::chord
