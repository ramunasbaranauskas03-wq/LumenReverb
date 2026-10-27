#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

class HistoryView : public juce::Component
{
public:
    explicit HistoryView (LumenCompressorProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    juce::String message; juce::uint32 msgTime = 0;
private:
    LumenCompressorProcessor& proc;
};

class TransferView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TransferView (LumenCompressorProcessor& p) : proc (p) { setTooltip ("Drag up / down to move the threshold"); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    LumenCompressorProcessor& proc;
    float startThr = 0;
};

class LumenCompressorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenCompressorEditor (LumenCompressorProcessor&);
    ~LumenCompressorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    static constexpr int W = 1200, H = 780;
private:
    void timerCallback() override;
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip);
    Combo& combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r, const juce::String& tip);

    LumenCompressorProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::unique_ptr<HistoryView> history;
    std::unique_ptr<TransferView> transfer;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, learnB { "LEARN" }, alignB { "ALIGN" };
    std::array<juce::TextButton, lumen::comp2::kNumStyles> styleB;
    juce::String phaseStatus;
    int lastVersion = -1;
    float inM = 0, outM = 0, grM = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenCompressorEditor)
};
