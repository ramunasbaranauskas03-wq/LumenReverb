#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace lumen::ui
{
    namespace col
    {
        const juce::Colour bg       { 0xff0c0d10 };
        const juce::Colour panel    { 0xff14161b };
        const juce::Colour panelHi  { 0xff1b1e25 };
        const juce::Colour stroke   { 0xff262a33 };
        const juce::Colour text     { 0xffd4d7de };
        const juce::Colour dim      { 0xff6d7381 };
        const juce::Colour amber    { 0xffffb547 };
        const juce::Colour coral    { 0xffff6b3d };
        const juce::Colour ice      { 0xff7fd6ff };
        const juce::Colour rec      { 0xffff4058 };
    }

    class LnF : public juce::LookAndFeel_V4
    {
    public:
        LnF();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
        juce::Font getLabelFont (juce::Label&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minP, float maxP, juce::Slider::SliderStyle, juce::Slider&) override;
    };

    struct Knob : juce::Component
    {
        Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name);
        void resized() override;
        void paint (juce::Graphics&) override;
        juce::Slider slider;
        juce::String title;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    struct Choice : juce::Component
    {
        Choice (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name);
        void resized() override;
        void paint (juce::Graphics&) override;
        juce::ComboBox box;
        juce::String title;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };

    struct Toggle : juce::Component
    {
        Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name);
        void resized() override { button.setBounds (getLocalBounds()); }
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };

    class WaveDisplay : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        explicit WaveDisplay (LumenReverseProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void refresh();
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int, int) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    private:
        LumenReverseProcessor& proc;
        LumenReverseProcessor::DisplayData data;
        bool dragHover = false;
        float spin = 0.0f;
    };

    class DragOut : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit DragOut (LumenReverseProcessor& p) : proc (p) { setTooltip ("Drag the rendered swell straight into your arrangement"); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
    private:
        LumenReverseProcessor& proc;
        bool dragging = false;
    };

    struct Painter : juce::Component
    {
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> draw;
        Painter() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (draw) draw (g, getLocalBounds().toFloat()); }
    };

    struct Section : juce::Component
    {
        explicit Section (juce::String t) : title (std::move (t)) { setInterceptsMouseClicks (false, true); }
        void paint (juce::Graphics&) override;
        juce::String title;
    };
}

class LumenReverseEditor : public juce::AudioProcessorEditor,
                           public juce::DragAndDropContainer,
                           private juce::Timer
{
public:
    explicit LumenReverseEditor (LumenReverseProcessor&);
    ~LumenReverseEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void layoutSection (juce::Rectangle<int> area, std::vector<juce::Component*> items, int cols);
    void refreshPresetBox();

    LumenReverseProcessor& proc;
    lumen::ui::LnF lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // header
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, aBtn { "A" }, bBtn { "B" }, copyBtn { "A>B" }, undoBtn { "Undo" }, redoBtn { "Redo" };

    // display area
    lumen::ui::WaveDisplay display { proc };
    juce::TextButton captureBtn { "CAPTURE" }, previewBtn { "PLAY" }, loopBtn { "LOOP" }, loadBtn { "LOAD" }, clearBtn { "CLEAR" }, saveBtn { "EXPORT" };
    lumen::ui::DragOut dragOut { proc };
    lumen::ui::Choice capMode, capBars;
    lumen::ui::Knob threshold;

    // length panel
    lumen::ui::Section lengthSec { "LENGTH" };
    juce::OwnedArray<juce::TextButton> lenButtons;
    lumen::ui::Toggle syncToggle;
    lumen::ui::Knob freeMs;
    lumen::ui::Choice trigger, cycle;
    lumen::ui::Toggle pitchTrack;

    // modules
    lumen::ui::Section spaceSec { "SPACE" }, swellSec { "SWELL" }, motionSec { "MOTION" }, outSec { "OUTPUT" };
    lumen::ui::Choice algo, mode, tremRate, tremShape;
    lumen::ui::Knob size, decay, predelay, diffusion, damping, mod, shimmer;
    lumen::ui::Knob rise, tailFade, sourceFade, blend, sweep;
    lumen::ui::Knob tremDepth, tremPhase, tremFollow;
    lumen::ui::Knob lowCut, highCut, width, gain, dry;
    lumen::ui::Toggle normalize;

    std::unique_ptr<juce::FileChooser> chooser;
    lumen::ui::Painter readout;
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenReverseEditor)
};
