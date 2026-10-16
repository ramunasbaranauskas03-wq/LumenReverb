#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

// shared analysis computed by the editor (spectra + correlation by frequency)
struct Analysis
{
    static constexpr int kOrder = 12, kN = 1 << kOrder, kBands = 30;   // 1/3 octaves from 20 Hz
    float midOut[kN / 2] {}, sideOut[kN / 2] {}, sideIn[kN / 2] {};   // dB
    float corrIn[kBands] {}, corrOut[kBands] {}, energy[kBands] {};
    static float bandCentre (int b) { return 20.0f * std::pow (2.0f, (b + 0.5f) / 3.0f); }
};

// spectrum + width envelope with draggable LOW / HIGH nodes and the HPF handle
class WidthGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    WidthGraph (LumenMonoFilterProcessor& p, const Analysis& a);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    enum { Low, High, Hpf };
    float fToX (float f) const { return (float) getWidth() * std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f; }
    float xToF (float x) const { return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
    float wToY (float w) const { return top() + (1.0f - w / 2.0f) * plotH(); }
    float yToW (float y) const { return juce::jlimit (0.0f, 2.0f, 2.0f * (1.0f - (y - top()) / plotH())); }
    float top() const { return 22.0f; }
    float plotH() const { return (float) getHeight() - 44.0f; }
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    int hit (juce::Point<float> p) const;
    LumenMonoFilterProcessor& proc;
    const Analysis& an;
    std::unique_ptr<juce::ParameterAttachment> att[5];   // lowF lowW highF highW hpf
    int hover = -1, drag = -1;
};

// correlation by frequency: input (grey) vs output (colour), per 1/3 octave
class CorrStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CorrStrip (const Analysis& a) : an (a) { setTooltip ("Correlation per 1/3 octave. Green = mono-safe, amber = wide, red = out of phase (cancels in mono / on club systems). Grey = input, colour = output"); }
    void paint (juce::Graphics&) override;
private:
    const Analysis& an;
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
    void analyse();
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenMonoFilterProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    Analysis an;
    juce::dsp::FFT fft { Analysis::kOrder };
    std::vector<float> win, buf[4];
    double cs[2][Analysis::kBands][3] {};
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<WidthGraph> graph;
    std::unique_ptr<CorrStrip> strip;
    juce::TextButton monB[5];
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox, goalBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, focusB { "AUTO FOCUS" };
    lumen::led::DotMatrix logo, status;
    float cIn = 1, cOut = 1;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMonoFilterEditor)
};
