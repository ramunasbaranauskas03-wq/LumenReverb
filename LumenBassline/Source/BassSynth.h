#pragma once
#include <JuceHeader.h>

//==============================================================================
// Monophonic preview bass synth with glide, filter envelope, drive and a
// built-in kick/hat groove + sidechain duck so the bassline is heard in context.
//==============================================================================
class BassSynth
{
public:
    enum Sound { SubRumble, GritSaw, AcidSquelch, Reese, PluckBass, OrganBass, RubberSquare, numSounds };

    static juce::StringArray soundNames()
    {
        return { "Sub Rumble", "Grit Saw", "Acid Squelch", "Reese", "Pluck Bass", "Organ Bass", "Rubber Square" };
    }

    struct Settings
    {
        int   sound = GritSaw;
        float cutoff = 0.45f, reso = 0.3f, envAmt = 0.5f, decay = 0.35f;
        float drive = 0.3f, sub = 0.5f, glideMs = 60.0f, volume = 0.8f;
        float drums = 0.6f, duck = 0.5f;
        bool  drumsOn = true;
    };

    void prepare (double sr)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, 512, 1 };
        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filter2.prepare (spec);
        filter2.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        amp.setSampleRate (sr);
        reset();
    }

    void reset()
    {
        filter.reset(); filter2.reset();
        amp.reset();
        currentNote = -1; gate = false;
        kickEnv = hatEnv = duckEnv = 0;
    }

    void setSettings (const Settings& s) { set = s; }

    void noteOn (int note, float velocity, bool legato, bool accent)
    {
        targetFreq = (float) juce::MidiMessage::getMidiNoteInHertz (note);
        const bool glide = legato && gate && currentNote >= 0;
        if (! glide)
        {
            freq = targetFreq;
            amp.noteOn();
            fenv = 1.0f;
            if (set.sound == PluckBass || set.sound == AcidSquelch) phase1 = phase2 = 0;
        }
        else if (accent)
            fenv = std::max (fenv, 0.7f);

        accentAmt = accent ? 1.0f : 0.0f;
        vel = velocity;
        currentNote = note;
        gate = true;
        if (! glide) fenv = 1.0f + (accent ? 0.6f : 0.0f);
        updateAdsr();
    }

    void noteOff (int note)
    {
        if (note == currentNote) { amp.noteOff(); gate = false; }
    }

    void allOff() { amp.noteOff(); gate = false; currentNote = -1; }

    void triggerKick() { kickEnv = 1.0f; kickPhase = 0; duckEnv = 1.0f; }
    void triggerHat()  { hatEnv = 1.0f; }

    void render (float* L, float* R, int n)
    {
        const float sr = (float) sampleRate;
        const float glideCoef = std::exp (-1.0f / (0.001f * std::max (1.0f, set.glideMs) * sr));
        const float fenvCoef  = std::exp (-1.0f / ((0.03f + set.decay * set.decay * 1.2f) * sr
                                                   * (set.sound == PluckBass ? 0.5f : 1.0f)));
        const float kickDec   = std::exp (-1.0f / (0.28f * sr));
        const float kickPDec  = std::exp (-1.0f / (0.035f * sr));
        const float hatDec    = std::exp (-1.0f / (0.035f * sr));
        const float duckDec   = std::exp (-1.0f / (0.16f * sr));
        const float res       = juce::jlimit (0.5f, 12.0f, 0.6f + set.reso * set.reso * (set.sound == AcidSquelch ? 14.0f : 8.0f));
        filter.setResonance (res);
        filter2.setResonance (0.6f);
        const float driveAmt  = 1.0f + set.drive * (set.sound == GritSaw ? 12.0f : 7.0f);
        const float driveNorm = 1.0f / std::tanh (driveAmt * 0.6f);

        for (int i = 0; i < n; ++i)
        {
            freq = targetFreq + (freq - targetFreq) * glideCoef;
            const float f = freq;

            // --- oscillators
            const float dt1 = f / sr;
            float osc = 0.0f;
            switch (set.sound)
            {
                case SubRumble:   osc = 0.6f * std::sin (juce::MathConstants<float>::twoPi * phase1) + 0.4f * tri (phase1) + 0.15f * saw (phase1, dt1); break;
                case GritSaw:     osc = saw (phase1, dt1) * 0.8f + square (phase1, dt1, 0.5f) * 0.25f; break;
                case AcidSquelch: osc = saw (phase1, dt1); break;
                case Reese:
                {
                    const float dt2 = f * 1.0085f / sr;
                    osc = 0.55f * (saw (phase1, dt1) + saw (phase2, dt2));
                    phase2 += dt2; if (phase2 >= 1.0f) phase2 -= 1.0f;
                    break;
                }
                case PluckBass:   osc = 0.7f * saw (phase1, dt1) + 0.4f * square (phase1, dt1, 0.25f); break;
                case OrganBass:   osc = 0.5f * square (phase1, dt1, 0.5f) + 0.35f * std::sin (juce::MathConstants<float>::twoPi * phase1 * 2.0f); break;
                default:          osc = square (phase1, dt1, 0.35f + 0.15f * std::sin (lfoPhase)); break;
            }
            phase1 += dt1; if (phase1 >= 1.0f) phase1 -= 1.0f;
            lfoPhase += 0.7f * juce::MathConstants<float>::twoPi / sr;
            if (lfoPhase > juce::MathConstants<float>::twoPi) lfoPhase -= juce::MathConstants<float>::twoPi;

            subPhase += dt1; if (subPhase >= 1.0f) subPhase -= 1.0f;
            const float subOsc = std::sin (juce::MathConstants<float>::twoPi * subPhase);

            // --- filter (cutoff recomputed every 8 samples)
            fenv *= fenvCoef;
            if ((i & 7) == 0)
            {
                const float base = 40.0f * std::pow (400.0f, set.cutoff);              // 40 Hz .. 16 kHz
                const float envOct = set.envAmt * 6.0f * fenv * (0.6f + 0.4f * vel + 0.4f * accentAmt);
                const float fc = juce::jlimit (30.0f, (float) (sampleRate * 0.45), base * std::pow (2.0f, envOct));
                filter.setCutoffFrequency (fc);
                filter2.setCutoffFrequency (juce::jmin (fc * 1.5f, (float) (sampleRate * 0.45)));
            }
            float x = filter.processSample (0, osc);
            if (set.sound == SubRumble || set.sound == Reese) x = filter2.processSample (0, x);

            x = std::tanh (x * driveAmt) * driveNorm;
            x += subOsc * set.sub * (set.sound == SubRumble ? 0.9f : 0.6f);

            const float env = amp.getNextSample();
            duckEnv *= duckDec;
            const float duckGain = set.drumsOn ? 1.0f - set.duck * duckEnv * 0.85f : 1.0f;
            float bass = x * env * (0.55f + 0.45f * vel) * set.volume * 0.5f * duckGain;

            // --- preview drums
            float drums = 0.0f;
            if (set.drumsOn)
            {
                if (kickEnv > 1.0e-4f)
                {
                    kickPEnv = (kickEnv >= 0.999f) ? 1.0f : kickPEnv * kickPDec;
                    const float kf = 48.0f + 190.0f * kickPEnv;
                    kickPhase += kf / sr; if (kickPhase >= 1.0f) kickPhase -= 1.0f;
                    drums += std::tanh (1.6f * std::sin (juce::MathConstants<float>::twoPi * kickPhase)) * kickEnv * 0.8f;
                    kickEnv *= kickDec;
                }
                if (hatEnv > 1.0e-4f)
                {
                    const float noise = random.nextFloat() * 2.0f - 1.0f;
                    const float hp = noise - hatLast; hatLast = noise;
                    drums += hp * hatEnv * 0.12f;
                    hatEnv *= hatDec;
                }
                drums *= set.drums * set.volume;
            }

            const float out = bass + drums;
            L[i] += out; if (R != L) R[i] += out;
        }
    }

    bool isActive() const { return amp.isActive() || kickEnv > 1.0e-4f || hatEnv > 1.0e-4f; }

private:
    void updateAdsr()
    {
        juce::ADSR::Parameters p;
        switch (set.sound)
        {
            case PluckBass: p = { 0.001f, 0.18f + set.decay * 0.4f, 0.15f, 0.04f }; break;
            case SubRumble: p = { 0.004f, 0.3f, 0.9f, 0.06f }; break;
            default:        p = { 0.002f, 0.25f, 0.8f, 0.04f }; break;
        }
        amp.setParameters (p);
    }

    static float polyBlep (float t, float dt)
    {
        if (t < dt)        { t /= dt;           return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    static float saw (float ph, float dt)     { return 2.0f * ph - 1.0f - polyBlep (ph, dt); }
    static float tri (float ph)               { return 4.0f * std::abs (ph - 0.5f) - 1.0f; }
    static float square (float ph, float dt, float pw)
    {
        float v = ph < pw ? 1.0f : -1.0f;
        v += polyBlep (ph, dt);
        float t2 = ph - pw; if (t2 < 0.0f) t2 += 1.0f;
        v -= polyBlep (t2, dt);
        return v;
    }

    double sampleRate = 44100.0;
    Settings set;
    juce::dsp::StateVariableTPTFilter<float> filter, filter2;
    juce::ADSR amp;
    juce::Random random;

    float freq = 55.0f, targetFreq = 55.0f, vel = 0.8f, accentAmt = 0.0f, fenv = 0.0f;
    float phase1 = 0, phase2 = 0.37f, subPhase = 0, lfoPhase = 0;
    float kickEnv = 0, kickPEnv = 0, kickPhase = 0, hatEnv = 0, hatLast = 0, duckEnv = 0;
    int   currentNote = -1;
    bool  gate = false;
};
