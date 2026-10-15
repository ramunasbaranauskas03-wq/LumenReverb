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

class LumenSynthEditor : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         public lumen::led::ModHost,
                         private juce::Timer
{
public:
    explicit LumenSynthEditor (LumenSynthProcessor&);
    ~LumenSynthEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;
    void paintContent (juce::Graphics&);

    // ModHost
    std::vector<lumen::led::ModRing> ringsFor (int dst) override;
    void dropMod (int src, int dst) override;
    void knobMenu (int dst, juce::Component* target) override;

    static constexpr int kW = 1060, kH = 720;

private:
    struct Root : public juce::Component
    {
        LumenSynthEditor& ed;
        explicit Root (LumenSynthEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintContent (g); }
    };

    void timerCallback() override;
    void showPage (int idx);
    void stepPreset (int delta);
    void openSaveDialog();
    void collectKnobs (juce::Component& c);

    LumenSynthProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    Root root { *this };

    static constexpr int kPages = 7;
    std::unique_ptr<lumen::led::Page> pages[kPages];
    std::unique_ptr<lumen::led::Page> smart, macros, scope, browser;
    juce::TextButton tabs[kPages];
    int currentPage = 0;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetArea, browseBtn { "BROWSE" }, saveBtn { "SAVE" };
    std::unique_ptr<lumen::led::Toggle> hqToggle;
    juce::ComboBox zoom;
    lumen::led::DotMatrix nameMatrix, logoMatrix;
    lumen::led::LedKeyboard keyboard;
    std::unique_ptr<juce::AlertWindow> saveWindow;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<lumen::led::Knob*> knobs;
    std::vector<lumen::led::ModBadge*> badges;
    int ringCounter = 0;

    float meterL = 0, meterR = 0, holdL = 0, holdR = 0; int holdTimer = 0;
    float voiceLv[16] {};
    int lastPresetVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSynthEditor)
};
