#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenMelodyEditor;

class PianoRoll : public juce::Component, public juce::SettableTooltipClient
{
public:
    PianoRoll (LumenMelodyProcessor& p, int part, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    struct Geo { int lo, hi, steps; float x0, w, y0, h; };
    Geo geo() const;
    int pitchOf (const lumen::melody::Note& n) const;
    int relForPitch (int step, int pitch) const;
    int hit (juce::Point<float> pt) const;
    LumenMelodyProcessor& proc;
    int part; juce::Colour col;
    int dragIdx = -1; lumen::melody::Pattern work; bool changed = false;
    int fixedLo = -1, fixedHi = -1;
};

class MidiDrag : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDrag (LumenMelodyProcessor& p, LumenMelodyEditor& e, LumenMelodyProcessor::ExportKind k, juce::String t, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    LumenMelodyProcessor& proc; LumenMelodyEditor& ed; LumenMelodyProcessor::ExportKind kind; juce::String text; juce::Colour col;
    bool dragged = false;
};

class LumenMelodyEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit LumenMelodyEditor (LumenMelodyProcessor&);
    ~LumenMelodyEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void showMessage (const juce::String& m) { msg = m; repaint(); }
private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    LumenMelodyProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<PianoRoll> leadRoll, bassRoll;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, aB { "A" }, bB { "B" }, copyB { "A>B" }, playB { "PLAY" },
                     genLead { "GENERATE HOOK" }, genBass { "GENERATE BASS" }, dice { "DICE BOTH" };
    lumen::led::DotMatrix logo;
    juce::String msg;
    float outPk = 0;
    int lastProgram = -1, lastPattern = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMelodyEditor)
};
