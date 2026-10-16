#pragma once
#include "PluginProcessor.h"
#include "LedUi.h"

class LumenKickEditor;

// rendered kick: waveform + pitch curve; drag it out as a WAV
class KickView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit KickView (LumenKickProcessor& p) : proc (p) { setTooltip ("Your kick (note C3). Drag it into Ableton as a WAV file."); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    LumenKickProcessor& proc;
    bool dragging = false;
};

// sample layer drop zone
class SampleZone : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit SampleZone (LumenKickProcessor& p) : proc (p) { setTooltip ("Drop your kick sample here (WAV / AIFF / MP3 / FLAC)"); }
    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { hot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hot = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    std::function<void (const juce::String&)> onMessage;
private:
    LumenKickProcessor& proc;
    bool hot = false;
};

// drum loop overview with the detected hits
class LoopView : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit LoopView (LumenKickProcessor& p) : proc (p) { setTooltip ("Drop a drum loop here (WAV / AIFF / MP3 / FLAC), then press SPLIT"); }
    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { hot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hot = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    std::function<void (const juce::String&)> onMessage;
private:
    LumenKickProcessor& proc;
    bool hot = false;
};

// one stem: mini waveform, drag handle (whole row is draggable)
class StemRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    StemRow (LumenKickProcessor& p, int c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;
    juce::TextButton playB { "PLAY" }, toSampleB { "TO SAMPLE LAYER" };
private:
    LumenKickProcessor& proc;
    int cls;
};

class LumenKickEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit LumenKickEditor (LumenKickProcessor&);
    ~LumenKickEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void setTab (int t);
    void showMessage (const juce::String& m) { msg = m; repaint(); }

private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    LumenKickProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<juce::Component*> designComps, splitComps;
    std::unique_ptr<KickView> kickView;
    std::unique_ptr<SampleZone> sampleZone;
    std::unique_ptr<LoopView> loopView;
    std::unique_ptr<StemRow> stems[lumen::split::kClasses];
    juce::TextButton tabB[2], playB { "PLAY" }, exportB { "EXPORT WAV" }, loadB { "LOAD" }, clearB { "CLEAR" }, cloneB { "KICK CLONE" },
                     loadLoopB { "LOAD LOOP" }, splitB { "SPLIT" }, folderB { "OPEN FOLDER" };
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" }, prevB { "<" }, nextB { ">" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::FileChooser> chooser;
    lumen::led::DotMatrix logo, noteDisp;
    juce::String msg;
    float outPk = 0;
    int tab = 0, lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenKickEditor)
};
