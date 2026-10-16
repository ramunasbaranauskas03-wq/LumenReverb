#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

// spectrum in/out + live reduction curve + draggable sensitivity curve (6 nodes) + processing range handles
class ResDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ResDisplay (LumenResonanceProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void update();                              // pull a new frame from the engine (timer)
    float gr[lumen::res::ResonanceEngine::kDisp] {}, boost[lumen::res::ResonanceEngine::kDisp] {}, grHold[lumen::res::ResonanceEngine::kDisp] {},
          sIn[lumen::res::ResonanceEngine::kDisp] {}, sOut[lumen::res::ResonanceEngine::kDisp] {};
private:
    float fToX (float f) const { return (float) getWidth() * std::log10 (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / 3.0f; }
    float xToF (float x) const { return 20.0f * std::pow (10.0f, 3.0f * juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
    float sToY (float s) const { return 20.0f + (12.0f - s) / 36.0f * ((float) getHeight() - 40.0f); }
    float yToS (float y) const { return juce::jlimit (-24.0f, 12.0f, 12.0f - (y - 20.0f) / ((float) getHeight() - 40.0f) * 36.0f); }
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    int hit (juce::Point<float> p) const;       // 0..5 nodes, 6 low range, 7 high range
    LumenResonanceProcessor& proc;
    std::unique_ptr<juce::ParameterAttachment> fAtt[6], gAtt[6], lowAtt, highAtt;
    int hover = -1, drag = -1;
};

// scrolling reduction spectrogram
class GrMap : public juce::Component, public juce::SettableTooltipClient
{
public:
    GrMap() { setTooltip ("Reduction over time: brighter = more reduction. Shows where and when the resonances are caught"); }
    void push (const float* gr, float depth);
    void paint (juce::Graphics&) override;
private:
    juce::Image img;
};

class LumenResonanceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenResonanceEditor (LumenResonanceProcessor&);
    ~LumenResonanceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenResonanceProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<ResDisplay> display;
    std::unique_ptr<GrMap> grmap;
    juce::Component* msKnobs[2] {};
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" };
    juce::ComboBox presetBox, srcBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, learnB { "SMART LEARN" };
    lumen::led::DotMatrix logo, status, grNum;
    float inPk = 0, outPk = 0, avgRed = 0;
    int lastProgram = -1, lastFrame = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenResonanceEditor)
};
