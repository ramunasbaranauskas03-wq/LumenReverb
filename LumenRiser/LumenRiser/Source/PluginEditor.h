#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenRiserEditor;

class WavDrag : public juce::Component, public juce::SettableTooltipClient
{
public:
    WavDrag (LumenRiserProcessor& p, LumenRiserEditor& e, LumenRiserProcessor::RenderKind k, juce::String t, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    LumenRiserProcessor& proc; LumenRiserEditor& ed;
    LumenRiserProcessor::RenderKind kind; juce::String text; juce::Colour col;
    bool dragged = false;
};

class Timeline : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Timeline (LumenRiserProcessor& p) : proc (p) { setTooltip ("Song timeline: the build (glow), the pre-drop gap (red) and the DROP. The white line is the playhead"); }
    void paint (juce::Graphics&) override;
private:
    LumenRiserProcessor& proc;
};

class ProgressOrb : public juce::Component
{
public:
    explicit ProgressOrb (LumenRiserProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    float flash = 0;
private:
    LumenRiserProcessor& proc;
    lumen::led::DotMatrix dm, dm2;
};

class LumenRiserEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit LumenRiserEditor (LumenRiserProcessor&);
    ~LumenRiserEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void showMessage (const juce::String& m) { msg = m; repaint(); }
private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    LumenRiserProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<Timeline> timeline;
    std::unique_ptr<ProgressOrb> orb;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, aB { "A" }, bB { "B" }, copyB { "A>B" }, previewB { "PREVIEW BUILD" }, dropB { "DROP NOW" };
    lumen::led::DotMatrix logo;
    juce::String msg;
    float outPk = 0;
    int lastProgram = -1, lastDrops = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenRiserEditor)
};
