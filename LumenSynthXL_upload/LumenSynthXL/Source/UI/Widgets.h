#pragma once
#include "../PluginProcessor.h"
#include "LookAndFeel.h"

namespace lxl::ui
{
inline bool gForceTick = false;
using APVTS = juce::AudioProcessorValueTreeState;

/** Section frame with a title strip and optional power toggle. */
class Panel : public juce::Component
{
public:
    Panel (juce::String title, APVTS* state = nullptr, juce::String powerParam = {});
    void paint (juce::Graphics&) override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (8).withTrimmedTop (20); }
    std::function<void()> onLayout;
    bool isOn() const { return power == nullptr || power->getToggleState(); }
private:
    juce::String title;
    std::unique_ptr<juce::ToggleButton> power;
    std::unique_ptr<APVTS::ButtonAttachment> powerAtt;
};

/** Rotary knob with name/value label, modulation ring and mod-source drop target. */
class Knob : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    Knob (LumenSynthXLProcessor& p, const juce::String& paramId, const juce::String& label, bool bipolar = false);
    ~Knob() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    bool hasModulation() const { return destIndex > 0 && ! proc.rowsForDest (destIndex).isEmpty(); }
    void tick();
    void setLabel (const juce::String& l) { if (l != label) { label = l; repaint(); } }
    int maxKnobSize = 58;

    class KnobSlider : public juce::Slider
    {
    public:
        std::function<bool (const juce::MouseEvent&)> onDownHook;
        std::function<void (const juce::MouseEvent&)> onDragHook;
        void mouseDown (const juce::MouseEvent& e) override { hooked = onDownHook && onDownHook (e); if (! hooked) Slider::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (hooked) { if (onDragHook) onDragHook (e); } else Slider::mouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override { if (! hooked) Slider::mouseUp (e); hooked = false; }
    private:
        bool hooked = false;
    };
    KnobSlider slider;

private:
    void showModMenu();
    LumenSynthXLProcessor& proc;
    juce::String paramId, label;
    int destIndex = -1;
    bool dragHover = false;
    float lastModDisplay = 0.0f;
    std::unique_ptr<APVTS::SliderAttachment> att;
    juce::RangedAudioParameter* param = nullptr;
    int modDragRow = -1;
    float modDragStart = 0.0f;
    int numRows = 0;
};

/** Combo box bound to a choice parameter. */
class Choice : public juce::Component
{
public:
    Choice (APVTS& s, const juce::String& paramId, const juce::String& label = {});
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ComboBox box;
private:
    juce::String label;
    std::unique_ptr<APVTS::ComboBoxAttachment> att;
};

/** Toggle bound to a bool parameter. */
class Toggle : public juce::ToggleButton
{
public:
    Toggle (APVTS& s, const juce::String& paramId, const juce::String& text);
private:
    std::unique_ptr<APVTS::ButtonAttachment> att;
};

/** Draggable modulation source chip with live value bar. */
class ModChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModChip (LumenSynthXLProcessor& p, int sourceIndex, juce::String text);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick();
    std::function<void()> onClick;
    bool selected = false;
    juce::String text;
private:
    LumenSynthXLProcessor& proc;
    int src;
    float value = 0.0f;
    bool dragging = false;
};

// ------------------------------------------------------------------ displays
class WavetableView : public juce::Component
{
public:
    WavetableView (LumenSynthXLProcessor& p, int osc);
    void paint (juce::Graphics&) override;
    void tick();
private:
    LumenSynthXLProcessor& proc;
    int osc;
    float lastPos = -1.0f;
    const Wavetable* lastTable = nullptr;
    int lastMode = -1;
    float lastWarp = -1.0f;
};

class FilterView : public juce::Component
{
public:
    FilterView (LumenSynthXLProcessor& p, int filter);
    void paint (juce::Graphics&) override;
    void tick();
private:
    LumenSynthXLProcessor& proc;
    int filter;
    float lastC = -1, lastR = -1; int lastT = -1; bool lastOn = false;
};

class EnvView : public juce::Component
{
public:
    /** prefix "amp_" or "modN_" */
    EnvView (LumenSynthXLProcessor& p, juce::String prefix, int slot = -1);
    void paint (juce::Graphics&) override;
    void tick();
private:
    LumenSynthXLProcessor& proc;
    juce::String prefix;
    int slot;
    float last[6] {};
};

class LfoView : public juce::Component
{
public:
    LfoView (LumenSynthXLProcessor& p, int slot);
    void paint (juce::Graphics&) override;
    void tick();
private:
    LumenSynthXLProcessor& proc;
    int slot;
    float lastVal = 0.0f;
};

class PerformerView : public juce::Component
{
public:
    PerformerView (LumenSynthXLProcessor& p, int slot);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick();
private:
    void setFromMouse (juce::Point<float>);
    LumenSynthXLProcessor& proc;
    int slot;
    int lastStep = -1;
    int lastEdited = -1;
};

class Meter : public juce::Component
{
public:
    explicit Meter (LumenSynthXLProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
private:
    LumenSynthXLProcessor& proc;
    float l = 0.0f, r = 0.0f;
};

/** Lays out a row of components with equal widths inside an area. */
void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps, int gap = 4);
} // namespace lxl::ui
