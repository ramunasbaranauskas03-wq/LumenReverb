#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenMeterEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit LumenMeterEditor (LumenMeterProcessor&);
    ~LumenMeterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void setTab (int t);
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

    struct Check { int state; juce::String title, text; };   // state 0 ok, 1 warn, 2 bad, 3 info
    std::vector<Check> computeChecks() const;

private:
    void timerCallback() override;
    void analyse();
    void paintLoudness (juce::Graphics&);
    void paintAnalyzer (juce::Graphics&);
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenMeterProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<juce::Component*> loudComps, anaComps;
    juce::TextButton tabB[2], resetB { "RESET" }, loadRefB { "LOAD REFERENCE" };
    std::unique_ptr<juce::FileChooser> chooser;
    lumen::led::DotMatrix dm;
    LumenMeterProcessor::Snapshot snap {};
    // analysis
    juce::dsp::FFT fft { 13 };
    std::vector<float> fbuf, win;
    float spec[LumenMeterProcessor::kSpecBands] {}, specPeak[LumenMeterProcessor::kSpecBands] {};
    double specAvg[LumenMeterProcessor::kSpecBands] {}; double avgW = 0;
    juce::Image scope;
    int tab = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMeterEditor)
};
