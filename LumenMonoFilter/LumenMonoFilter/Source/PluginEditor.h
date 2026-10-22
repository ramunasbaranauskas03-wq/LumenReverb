#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

// spectra computed by the editor from the processor's ring buffers
struct Spectra
{
    static constexpr int kOrder = 13, kN = 1 << kOrder;
    float midOut[kN / 2] {}, sideOut[kN / 2] {}, sideIn[kN / 2] {};   // dB, smoothed
    double fs = 48000;
};

// frequency / width display with draggable MONO, crossover and band-width handles
class WidthGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    WidthGraph (LumenMonoFilterProcessor& p, const Spectra& s);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void showMessage (const juce::String& m) { msg = m; msgTime = juce::Time::getMillisecondCounter(); repaint(); }
private:
    enum Handle { HMono = 0, HX1, HX2, HX3, HB1, HB2, HB3, HB4, HLabel1 = 20 };
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedTop (30).withTrimmedBottom (20).reduced (8, 0); }
    float fToX (float f) const { auto r = plot(); return r.getX() + r.getWidth() * std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f; }
    float xToF (float x) const { auto r = plot(); return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
    float wToY (float w) const { auto r = plot(); return r.getY() + 6 + (1.0f - w / 2.0f) * (r.getHeight() - 12); }
    float yToW (float y) const { auto r = plot(); return juce::jlimit (0.0f, 2.0f, 2.0f * (1.0f - (y - r.getY() - 6) / (r.getHeight() - 12))); }
    float dbToY (float db) const { auto r = plot(); return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 96.0f) / 90.0f); }
    float pv (const juce::String& id) const { return proc.P (id); }
    void xo (float out[3]) const;
    float bandCentre (int b) const;
    int hit (juce::Point<float> p) const;
    juce::String idOf (int h) const;
    void setParamValue (const juce::String& id, float v);
    LumenMonoFilterProcessor& proc;
    const Spectra& sp;
    int hover = -1, drag = -1;
    juce::String msg; juce::uint32 msgTime = 0;
};

// goniometer + correlation / width / mono-loss read-outs
class StereoScope : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StereoScope (LumenMonoFilterProcessor& p) : proc (p)
    { setTooltip ("Vectorscope of the OUTPUT: vertical = mono (mid), horizontal = stereo (side).  A vertical line is mono, a round cloud is wide, a horizontal line is out of phase."); }
    void paint (juce::Graphics&) override;
    void update();
    float corr = 1, corrLow = 1, width = 0, widthIn = 0, loss = 0, bal = 0, lvlL = 0, lvlR = 0;
private:
    LumenMonoFilterProcessor& proc;
    std::vector<juce::Point<float>> pts;
    float scale = 1.0f;
};

class LumenMonoFilterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenMonoFilterEditor (LumenMonoFilterProcessor&);
    ~LumenMonoFilterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
private:
    void timerCallback() override;
    void updateSpectra();
    float pv (const juce::String& id) const { return proc.P (id); }
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip = false);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round = false);
    Combo& combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r);

    LumenMonoFilterProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    Spectra spectra;
    std::unique_ptr<WidthGraph> graph;
    std::unique_ptr<StereoScope> scope;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, autoB { "AUTO" };
    juce::TextButton mon[9];
    juce::dsp::FFT fft { Spectra::kOrder };
    std::vector<float> fftBuf, window;
    int lastProgram = -1;
    bool wasLearning = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMonoFilterEditor)
};
