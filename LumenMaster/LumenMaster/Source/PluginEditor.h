#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

class LumenMasterEditor;

// one module in the chain strip: power LED + name + activity read-out; click = open its panel
class ModuleCard : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModuleCard (LumenMasterProcessor& p, LumenMasterEditor& e, int m) : proc (p), ed (e), module (m) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool selected = false;
private:
    LumenMasterProcessor& proc; LumenMasterEditor& ed; int module;
};

// big analyser: input / output spectrum, genre target, EQ curve with draggable nodes, smart balance curve
class SpectrumView : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpectrumView (LumenMasterProcessor& p, LumenMasterEditor& e);
    void paint (juce::Graphics&) override;
    void update();                    // called from the editor timer
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
private:
    float xOf (double f) const;
    double fOf (float x) const;
    float yDb (double db) const;      // spectrum scale (dBFS)
    float yEq (double db) const;      // EQ scale (+-12 dB)
    int nodeAt (juce::Point<float> p) const;
    LumenMasterProcessor& proc; LumenMasterEditor& ed;
    static constexpr int kPts = 240;
    std::array<float, kPts> specIn {}, specOut {};
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> buf, win;
    int drag = -1, hover = -1;
};

class LumenMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenMasterEditor (LumenMasterProcessor&);
    ~LumenMasterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void selectModule (int m);
    int selectedModule() const { return selMod; }
    void showMessage (const juce::String& m) { msg = m; msgTime = juce::Time::getMillisecondCounter(); repaint(); }
private:
    void timerCallback() override;
    void buildPanels();
    float pv (const juce::String& id) const { return proc.param (id); }
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (int mod, const juce::String& id, const juce::String& lbl, juce::Colour c, int x, int y, bool bip = false, int w = 58);
    Toggle& toggle (int mod, const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r);
    Combo& combo (int mod, const juce::String& id, juce::Colour c, juce::Rectangle<int> r);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);
    void drawPanelExtras (juce::Graphics&, juce::Rectangle<float>);

    LumenMasterProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    SpectrumView spectrum;
    std::vector<std::unique_ptr<ModuleCard>> cards;
    juce::ComboBox presetBox;
    std::unique_ptr<Combo> genreBox, destBox;
    juce::TextButton assistBtn { "MASTER ASSISTANT" }, refBtn { "REF" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, resetBtn { "RESET" };
    std::unique_ptr<Toggle> bypassT, matchT, lowLatT, monoT;
    std::unique_ptr<Knob> inKnob, outKnob, customKnob;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<juce::Component*> panel[lumen::master::kModules];
    std::unique_ptr<juce::FileChooser> chooser;
    int selMod = lumen::master::MMax;
    juce::String msg; juce::uint32 msgTime = 0;
    std::vector<float> histShort, histGr;
    float pkIn = 0, pkOutL = 0, pkOutR = 0;
    int lastAssist = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMasterEditor)
};
