#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

class LumenSynthSamplerEditor;

class WaveDrop : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    WaveDrop (LumenSynthSamplerProcessor& p, LumenSynthSamplerEditor& e, int layer) : proc (p), ed (e), layer (layer)
    { setTooltip ("Drop a sample from your pack (bass / pluck / lead / pad ...) or click to browse.  Right-click = clear"); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& f, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
private:
    LumenSynthSamplerProcessor& proc; LumenSynthSamplerEditor& ed; int layer;
    bool hover = false; int builtVersion = -1, builtW = 0;
    std::vector<float> mn, mx;
};

class LumenSynthSamplerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenSynthSamplerEditor (LumenSynthSamplerProcessor&);
    ~LumenSynthSamplerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void showMessage (const juce::String& m) { msg = m; msgTime = juce::Time::getMillisecondCounter(); repaint(); }
    void loadInto (int layer, const juce::File& f);
    void selectTab (int t);
private:
    void timerCallback() override;
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (int tab, const juce::String& id, const juce::String& lbl, juce::Colour c, int x, int y, bool bip = false, int w = 58);
    Toggle& toggle (int tab, const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r);
    Combo& combo (int tab, const juce::String& id, juce::Colour c, juce::Rectangle<int> r);
    void box (int tab, juce::Rectangle<int> r, const juce::String& title, juce::Colour c, const juce::String& onId = {});
    float pv (const juce::String& id) const { return proc.param (id); }

    LumenSynthSamplerProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::MidiKeyboardComponent keyboard;
    juce::ComboBox presetBox;
    juce::TextButton tabs[3], abA { "A" }, abB { "B" }, abCopy { "COPY" };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<juce::Component*> tabComps[3];
    struct Box { int tab; juce::Rectangle<float> r; juce::String title; juce::Colour col; juce::String onId; };
    std::vector<Box> boxes;
    std::unique_ptr<juce::FileChooser> chooser;
    int tab = 0;
    juce::String msg; juce::uint32 msgTime = 0;
    float meter = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSynthSamplerEditor)
};
