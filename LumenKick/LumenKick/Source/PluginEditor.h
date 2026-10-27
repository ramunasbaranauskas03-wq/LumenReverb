#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

class LumenKickEditor;

struct KickRender
{
    juce::AudioBuffer<float> out;
    std::array<std::vector<float>, 3> layers;
    std::array<std::vector<float>, 4> spec;     // dB per bin: 3 layers + master
    double fs = 48000;
    int specN = 8192;
    std::vector<float> lowHz;                   // sub pitch curve (Hz) per ms
};

class KickView : public juce::Component, public juce::SettableTooltipClient
{
public:
    KickView (LumenKickProcessor& p, const KickRender& r) : proc (p), rd (r) { setTooltip ("Drag the kick from here straight into your DAW"); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    juce::String message; juce::uint32 msgTime = 0;
private:
    LumenKickProcessor& proc; const KickRender& rd; bool dragging = false;
};

class SpecView : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpecView (LumenKickProcessor& p, const KickRender& r) : proc (p), rd (r) { setTooltip ("Drag the two lines to move the TOP / MID / LOW split"); }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    float fx (double f) const { auto r = getLocalBounds().toFloat().reduced (6); return r.getX() + r.getWidth() * (float) (std::log10 (f / 20.0) / 3.0); }
    double xf (float x) const { auto r = getLocalBounds().toFloat().reduced (6); return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()))); }
    int hit (float x) const;
    LumenKickProcessor& proc; const KickRender& rd; int hover = 0, drag = 0;
};

class LumenKickEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit LumenKickEditor (LumenKickProcessor&);
    ~LumenKickEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { dropX = x; dropY = y; repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { dropX = x; dropY = y; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropX = -1; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void showMessage (const juce::String& m) { view->message = m; view->msgTime = juce::Time::getMillisecondCounter(); }
    static constexpr int W = 1240, H = 786;
private:
    void timerCallback() override;
    void rerender();
    int dropTarget (int x, int y) const;    // -1 none, 3 = whole kick (display), 0..2 layer
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip, const juce::String& tip);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r, bool round, const juce::String& tip);
    Combo& combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r, const juce::String& tip);
    void chooseFile (std::function<void (const juce::File&)> then);

    LumenKickProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    KickRender rd;
    std::unique_ptr<KickView> view;
    std::unique_ptr<SpecView> spec;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::ComboBox presetBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, playB { "PLAY" }, loadKickB { "LOAD KICK + SPLIT" }, exportB { "EXPORT WAV" };
    std::array<juce::TextButton, 3> smpName, smpPrev, smpNext, smpClear;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<float> lastSnap; int lastSampleVersion = -1, lastVersion = -1; juce::uint32 lastRender = 0; bool dirty = true;
    int dropX = -1, dropY = -1;
    float meter = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenKickEditor)
};
