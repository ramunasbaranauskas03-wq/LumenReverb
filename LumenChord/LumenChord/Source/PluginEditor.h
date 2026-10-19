#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenChordEditor;

class PadButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    PadButton (LumenChordProcessor& p, int i) : proc (p), idx (i) { setTooltip ("Click: hear it.  Right-click: change the chord (degree, flat / sharp, quality, inversion, octave)"); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String name, roman;
private:
    LumenChordProcessor& proc;
    int idx;
};

class StepLane : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepLane (LumenChordProcessor& p) : proc (p) { setTooltip ("Arp steps: drag up / down = velocity (bottom = rest). Right-click = ratchet (double hit)"); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    void set (const juce::MouseEvent&);
    LumenChordProcessor& proc;
};

class Keyboard : public juce::Component
{
public:
    explicit Keyboard (LumenChordProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    LumenChordProcessor& proc;
};

class MidiDrag : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDrag (LumenChordProcessor& p, LumenChordEditor& e) : proc (p), ed (e) { setTooltip ("Drag the progression (with the arp, if it is on) into Ableton as a MIDI clip. Click = save to Documents / Lumen Chord"); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    LumenChordProcessor& proc; LumenChordEditor& ed;
    bool dragged = false;
};

class LumenChordEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit LumenChordEditor (LumenChordProcessor&);
    ~LumenChordEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void showMessage (const juce::String& m) { msg = m; repaint(); }
    void refreshPads();
private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    LumenChordProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<PadButton> pads[8];
    std::unique_ptr<StepLane> lane;
    std::unique_ptr<Keyboard> keys;
    std::unique_ptr<MidiDrag> drag;
    juce::ComboBox presetBox, moodBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, genB { "GENERATE" }, aB { "A" }, bB { "B" }, copyB { "A>B" };
    lumen::led::DotMatrix logo;
    juce::String msg;
    float outPk = 0;
    int lastProgram = -1, lastHash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenChordEditor)
};
