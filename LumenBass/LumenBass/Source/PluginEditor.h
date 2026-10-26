#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

struct BassSpectra
{
    static constexpr int kOrder = 13, kN = 1 << kOrder;
    float pre[kN / 2] {}, post[kN / 2] {};
    float scopePre[1024] {}, scopePost[1024] {};
    double fs = 48000;
};

// analyzer + band zones + draggable crossovers
class BassDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    BassDisplay (LumenBassProcessor& p, const BassSpectra& s);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = 0; repaint(); }
    juce::String message; juce::uint32 msgTime = 0;
private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (2).withTrimmedBottom (18).withTrimmedTop (26); }
    float fx (double f) const { auto r = plot(); return r.getX() + r.getWidth() * (float) (std::log10 (f / 20.0) / 3.0); }
    double xf (float x) const { auto r = plot(); return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()))); }
    float sy (float db) const { auto r = plot(); return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 84.0f) / 84.0f); }
    int hitLine (float x) const;
    LumenBassProcessor& proc; const BassSpectra& sp;
    int hover = 0, drag = 0;
};

class ScopeView : public juce::Component
{
public:
    ScopeView (LumenBassProcessor& p, const BassSpectra& s) : proc (p), sp (s) {}
    void paint (juce::Graphics&) override;
private:
    LumenBassProcessor& proc; const BassSpectra& sp;
};

class LumenBassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenBassEditor (LumenBassProcessor&);
    ~LumenBassEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    static constexpr int W = 1200, H = 790;
private:
    void timerCallback() override;
    void updateSpectra();
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip);
    Combo& combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r);

    LumenBassProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    BassSpectra spectra;
    std::unique_ptr<BassDisplay> display;
    std::unique_ptr<ScopeView> scope;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, learnB { "LEARN" };
    juce::dsp::FFT fft { BassSpectra::kOrder };
    std::vector<float> fftBuf, window;
    int lastVersion = -1, tick = 0;
    float inM = 0, outM = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenBassEditor)
};
