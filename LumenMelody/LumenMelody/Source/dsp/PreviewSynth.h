#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// built-in sounds so the hook and bass can be heard right away
//   lead types: 0 Supersaw, 1 Pluck, 2 Keys      bass types: 0 Sub, 1 Saw Bass, 2 Electro Squelch
namespace lumen::melody
{
struct PreviewSound : juce::SynthesiserSound
{
    explicit PreviewSound (int ch) : channel (ch) {}
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int c) override { return c == channel; }
    int channel;
};

class PreviewVoice : public juce::SynthesiserVoice
{
public:
    PreviewVoice (const int& typeRef, bool isBass) : type (typeRef), bass (isBass) {}
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<PreviewSound*> (s) != nullptr; }
    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        freq = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        vel = velocity; t = 0; rel = false; relLevel = 0;
        for (int k = 0; k < 3; ++k) ph[k] = (double) k * 0.33;
        lp1 = lp2 = b1 = b2 = 0;
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
        const double atk = 0.003, dec = bass ? 0.5 : ty == 1 ? 0.16 : ty == 2 ? 0.8 : 0.4;
        const double sus = bass ? (ty == 2 ? 0.55 : 0.85) : ty == 1 ? 0.0 : ty == 2 ? 0.25 : 0.75;
        const double relS = bass ? 0.04 : 0.12;
        for (int i = 0; i < n; ++i)
        {
            const double ts = t / fs;
            double e = ts < atk ? ts / atk : sus + (1.0 - sus) * std::exp (-(ts - atk) / dec);
            if (rel) { e = relLevel * std::exp (-relT / (relS * fs)); relT += 1.0; if (e < 1e-4) { clearCurrentNote(); break; } }
            env = e;
            double s = 0, cutoff = 20000.0, q = 0.0;
            if (! bass && ty == 2)
            {
                ph[0] += freq / fs; ph[0] -= std::floor (ph[0]);
                s = std::sin (2 * juce::MathConstants<double>::pi * ph[0]) + 0.3 * std::sin (4 * juce::MathConstants<double>::pi * ph[0]) * std::exp (-ts * 4.0);
            }
            else if (bass && ty == 0)
            {
                ph[0] += freq / fs; ph[0] -= std::floor (ph[0]);
                s = std::tanh (1.4 * std::sin (2 * juce::MathConstants<double>::pi * ph[0]));
            }
            else
            {
                static const double det[3] { 0.0, 0.11, -0.13 };
                const int nv = bass ? 2 : 3;
                for (int k = 0; k < nv; ++k)
                {
                    const double f = freq * std::pow (2.0, (bass ? det[k] * 0.3 : det[k]) / 12.0);
                    ph[k] += f / fs; ph[k] -= std::floor (ph[k]);
                    const double dt = f / fs; double v = 2.0 * ph[k] - 1.0;
                    if (ph[k] < dt) { const double x = ph[k] / dt; v -= x + x - x * x - 1.0; }
                    else if (ph[k] > 1.0 - dt) { const double x = (ph[k] - 1.0) / dt; v -= x * x + x + x + 1.0; }
                    s += v / nv;
                }
                if (bass) cutoff = ty == 2 ? 180.0 + 2600.0 * std::exp (-ts / 0.07) : 900.0, q = ty == 2 ? 0.75 : 0.2;
                else cutoff = ty == 1 ? 700.0 + 6000.0 * std::exp (-ts / 0.07) : 6000.0;
            }
            // resonant 2-pole (Chamberlin SVF)
            const double fc = std::min (cutoff, fs * 0.2);
            const double f1 = 2.0 * std::sin (juce::MathConstants<double>::pi * fc / fs), damp = 2.0 * (1.0 - q * 0.9);
            b2 += f1 * b1; const double hp = s - b2 - damp * b1; b1 += f1 * hp;
            const double y = b2;
            const float g = (float) (y * e * vel * (bass ? 0.32 : 0.16));
            out.addSample (0, start + i, g);
            if (out.getNumChannels() > 1) out.addSample (1, start + i, g);
            t += 1.0;
        }
    }
private:
    const int& type; bool bass;
    double freq = 440, ph[3] {}, t = 0, relT = 0, env = 0, relLevel = 0, lp1 = 0, lp2 = 0, b1 = 0, b2 = 0; float vel = 1; bool rel = false;
};
} // namespace lumen::melody
