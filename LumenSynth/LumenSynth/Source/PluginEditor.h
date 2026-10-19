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

    static constexpr int kW = 1060, kH = 634;

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
    struct DriftBar : juce::Slider
    {
        juce::String label { "DRIFT" }; juce::Colour col { 0xffe6a440 };
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff111317)); g.fillRoundedRectangle (r, 4.0f);
            const float v = (float) valueToProportionOfLength (getValue());
            g.setColour (col.withAlpha (0.30f)); g.fillRoundedRectangle (r.withWidth (r.getWidth() * v), 4.0f);
            g.setColour (isMouseOverOrDragging() ? col.withAlpha (0.7f) : juce::Colour (0xff2b3037)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (juce::Colour (0xffdde2e8)); g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (label + " " + juce::String (juce::roundToInt (v * 100)) + "%", r, juce::Justification::centred);
        }
    };
    DriftBar driftSlider, warmSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driftAtt, warmAtt;
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
