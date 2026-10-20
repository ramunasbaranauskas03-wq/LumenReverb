#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

class LumenDrumsEditor;

class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    StepGrid (LumenDrumsProcessor& p, LumenDrumsEditor& e) : proc (p), ed (e)
    { setTooltip ("Click = hit on/off.  Drag up/down = velocity.  Shift-click = accent.  Right-click = probability / ratchet"); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    int page = 0;
    static constexpr int kRowH = 39, kCols = 16;
private:
    bool cellAt (juce::Point<float> pt, int& pad, int& step) const;
    LumenDrumsProcessor& proc; LumenDrumsEditor& ed;
    int dragPad = -1, dragStep = -1; float dragStartY = 0; int dragStartVel = 0;
};

class SampleZone : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    SampleZone (LumenDrumsProcessor& p, LumenDrumsEditor& e) : proc (p), ed (e) { setTooltip ("Drop a WAV / AIFF here to replace the synth sound with your sample.  Click = hear the pad"); }
    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void refresh() { dirty = true; repaint(); }
    int pad = 0;
private:
    void rebuild();
    LumenDrumsProcessor& proc; LumenDrumsEditor& ed;
    bool hover = false, dirty = true;
    std::vector<float> peaks;
    float lastSig = -1;
public:
    void checkParams();
};

class DragOut : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragOut (LumenDrumsProcessor& p, LumenDrumsEditor& e, bool wav, juce::String t) : proc (p), ed (e), isWav (wav), text (std::move (t))
    {
        setTooltip (wav ? "Drag the pattern as a WAV loop (song tempo, loop-ready) into Ableton. Click = save to Documents / Lumen Drums"
                        : "Drag the pattern as a MIDI clip (GM drum notes: kick C1, snare D1, clap D#1, hats F#1 / A#1 ...) into Ableton. Click = save");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int bars = 4;
private:
    juce::File make();
    LumenDrumsProcessor& proc; LumenDrumsEditor& ed; bool isWav; juce::String text; bool dragged = false;
};

class LumenDrumsEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit LumenDrumsEditor (LumenDrumsProcessor&);
    ~LumenDrumsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void selectPad (int p);
    int selectedPad() const { return selPad; }
    void showMessage (const juce::String& m) { msg = m; repaint(); }
    int exportBars() const { return barsBox.getSelectedId() > 0 ? (1 << (barsBox.getSelectedId() - 1)) : 4; }
private:
    void timerCallback() override;
    void buildPadEditor();
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    LumenDrumsProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned, padOwned;
    std::unique_ptr<StepGrid> grid;
    std::unique_ptr<SampleZone> zone;
    juce::OwnedArray<juce::TextButton> patBtns, pageBtns, nameBtns, diceBtns;
    juce::OwnedArray<juce::ComboBox> lenBoxes;
    juce::ComboBox presetBox, barsBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, aB { "A" }, bB { "B" }, copyB { "A>B" },
                     genB { "GENERATE" }, songB { "SONG A-H" }, fillB { "FILL" }, varyB { "VARY" }, clearB { "CLEAR" }, clearSmp { "X" };
    juce::String msg;
    int selPad = 0, lastProgram = -1, lastPattern = -1, lastPatVersion = -1;
    float outPk = 0;
    std::array<float, lumen::drums::kPads> flash {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenDrumsEditor)
};
