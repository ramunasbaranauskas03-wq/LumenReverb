#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

class LimGraph : public juce::Component
{
public:
    explicit LimGraph (LumenLimiterProcessor& p) : proc (p) { histo.fill (0.0f); }
    void paint (juce::Graphics&) override;
    void addHistogram (float lufs);
    void clearHistogram() { histo.fill (0.0f); }
    juce::String message; juce::uint32 msgTime = 0;
private:
    LumenLimiterProcessor& proc;
    std::array<float, 120> histo {};      // short-term LUFS, 0.25 LU bins from -30 to 0
};

class LumenLimiterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenLimiterEditor (LumenLimiterProcessor&);
    ~LumenLimiterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    static constexpr int W = 1240, H = 800;
private:
    void timerCallback() override;
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip);
    Combo& combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r, const juce::String& tip);
    void drawNeedle (juce::Graphics&, juce::Rectangle<float>, float grDb);

    LumenLimiterProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::unique_ptr<LimGraph> graph;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, matchB { "MATCH TARGET" }, resetB { "RESET" };
    std::array<juce::TextButton, 4> modeB;
    std::array<juce::TextButton, 6> monB;
    int lastVersion = -1, matchTick = 0;
    float needle = 0, grBar = 0, outL = 0, outR = 0, inBar = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenLimiterEditor)
};
