#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

// RT60 against frequency (bass / treble decay + crossovers) with the wet EQ, draggable
class DecayCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DecayCurve (LumenAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10, 8).withTrimmedLeft (30).withTrimmedBottom (14).withTrimmedTop (16); }
    float fx (double f) const { auto r = plot(); return r.getX() + r.getWidth() * (float) (std::log10 (f / 20.0) / 3.0); }
    double xf (float x) const { auto r = plot(); return 20.0 * std::pow (10.0, 3.0 * juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
    float maxRt() const;
    float ty (float s) const { auto r = plot(); return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, s / maxRt()); }
    float yt (float y) const { auto r = plot(); return maxRt() * juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / r.getHeight()); }
    int hit (juce::Point<float>) const;
    LumenAudioProcessor& proc;
    int hover = -1, drag = -1;
};

// wet level over the last seconds (the reverb tail you hear)
class TailView : public juce::Component
{
public:
    explicit TailView (LumenAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenAudioProcessor& proc;
};

class LumenAudioEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenAudioEditor (LumenAudioProcessor&);
    ~LumenAudioEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
private:
    void timerCallback() override;
    void learnClicked();
    float pv (const juce::String& id) const { return proc.P (id); }
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip = false);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r);
    Combo& combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r);

    LumenAudioProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::unique_ptr<DecayCurve> curve;
    std::unique_ptr<TailView> tail;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, learnB { "LEARN" }, undoB { "UNDO" };
    juce::TextButton algoB[8];
    float inM = 0, outM = 0, wetM = 0, duckM = 0, clarM[4] {};
    int lastProgram = -1; bool wasLearning = false;
    juce::String status;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenAudioEditor)
};
