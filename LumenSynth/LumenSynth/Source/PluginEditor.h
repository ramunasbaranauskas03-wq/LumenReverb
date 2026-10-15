#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

namespace lumen::led
{
struct Page : public juce::Component
{
    virtual void tick() {}
};

class LedKeyboard : public juce::MidiKeyboardComponent
{
public:
    LedKeyboard (juce::MidiKeyboardState& s) : MidiKeyboardComponent (s, horizontalKeyboard) {}
    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour) override;
};
}

class LumenSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenSynthEditor (LumenSynthProcessor&);
    ~LumenSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int idx);
    void showPresetMenu();
    void stepPreset (int delta);

    LumenSynthProcessor& proc;
    lumen::led::LedLookAndFeel lnf;

    std::unique_ptr<lumen::led::Page> pages[4];
    std::unique_ptr<lumen::led::Page> smart, macros, scope;
    juce::TextButton tabs[4];
    int currentPage = 0;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetArea;
    lumen::led::DotMatrix nameMatrix, logoMatrix, catMatrix;
    lumen::led::LedKeyboard keyboard;

    float meterL = 0, meterR = 0, holdL = 0, holdR = 0; int holdTimer = 0;
    float voiceLv[16] {};
    int lastPresetVersion = -1;
    double phaseClock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSynthEditor)
};
