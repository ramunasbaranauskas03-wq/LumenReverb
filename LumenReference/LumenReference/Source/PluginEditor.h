#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenReferenceEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit LumenReferenceEditor (LumenReferenceProcessor&);
    ~LumenReferenceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void loadDialog (int slot);
    void paintHeader (juce::Graphics&);
    void paintWave (juce::Graphics&);
    void paintSpectrum (juce::Graphics&);
    void paintLoudness (juce::Graphics&);
    void paintStereo (juce::Graphics&);
    void paintBands (juce::Graphics&);
    void paintAdvice (juce::Graphics&);
    void paintMatch (juce::Graphics&);
    juce::Rectangle<float> waveArea() const { return { 26.0f, 98.0f, 820.0f, 54.0f }; }
    float paramValue (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float v);

    LumenReferenceProcessor& proc;
    lumen::led::LedLookAndFeel lnf;

    juce::TextButton slotBtn[kSlots], loadBtn { "LOAD" }, abBtn, resetBtn { "RESET" }, captureBtn { "CAPTURE" }, zeroBtn { "FLAT" };
    std::unique_ptr<lumen::led::Toggle> matchT, monoT, previewT, meqT;
    std::unique_ptr<lumen::led::Combo> syncC, avgC;
    std::unique_ptr<lumen::led::Knob> trimK, amtK;
    std::unique_ptr<juce::FileChooser> chooser;
    lumen::led::DotMatrix logo, gainMatrix, abMatrix;

    lumen::ref::SpecAccum mixAcc;
    lumen::ref::Profile mixProf, refProf;
    std::vector<float> stHistory;
    juce::StringArray advice;
    std::vector<float> tmpL, tmpR;
    int tick = 0, lastRefVersion = -1, dragHandle = -1;
    float lastAvg = -1.0f;
    float outL = 0, outR = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenReferenceEditor)
};
