#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

class LumenSamplerEditor;

// waveform editor: start / end / loop markers (zero-cross snap), slices, zoom, playheads, file drop
class WaveView : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    WaveView (LumenSamplerProcessor& p, LumenSamplerEditor& e) : proc (p), ed (e)
    { setTooltip ("Drop an audio file here.  Drag the markers (S/E = start/end, LS/LE = loop).  Markers snap to zero crossings (hold Shift = free).  Wheel = zoom, double-click = full view"); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { v0 = 0; v1 = 1; dirty = true; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void refresh() { dirty = true; repaint(); }
private:
    float xOf (double norm) const;
    double normOf (float x) const;
    int markerAt (float x) const;
    void rebuild();
    LumenSamplerProcessor& proc; LumenSamplerEditor& ed;
    double v0 = 0, v1 = 1;            // visible range (normalised)
    bool hover = false, dirty = true;
    int dragMarker = -1, hoverMarker = -1, auditionNote = -1;
    std::vector<float> mn[2], mx[2];
    int builtW = 0, builtVersion = -1;
};

class SliceDrag : public juce::Component, public juce::SettableTooltipClient
{
public:
    SliceDrag (LumenSamplerProcessor& p, LumenSamplerEditor& e, bool wavOut) : proc (p), ed (e), wav (wavOut)
    {
        setTooltip (wav ? "Drag the edited region (start / end / reverse) out as a WAV - same rate and bit depth as the original, bit-exact. Click = save to Documents / Lumen Sampler"
                        : "Drag the slices as a MIDI clip (slice 1 = C1, 2 = C#1 ...) into Ableton - plays the loop back exactly, then rearrange the notes. Click = save to Documents / Lumen Sampler");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::File make() { return wav ? proc.exportRegionWav() : proc.exportSliceMidi(); }
    LumenSamplerProcessor& proc; LumenSamplerEditor& ed; bool wav; bool dragged = false;
};

class LumenSamplerEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit LumenSamplerEditor (LumenSamplerProcessor&);
    ~LumenSamplerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void showMessage (const juce::String& m) { msg = m; msgTime = juce::Time::getMillisecondCounter(); repaint(); }
    void loadFile (const juce::File& f);
private:
    void timerCallback() override;
    float pv (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, int x, int y, bool bip = false, int w = 58);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r);
    Combo& combo (const juce::String& id, juce::Colour c, juce::Rectangle<int> r);

    LumenSamplerProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    WaveView wave;
    SliceDrag sliceDrag, wavDrag;
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton loadBtn { "LOAD" }, rootBtn { "AUTO ROOT" }, abA { "A" }, abB { "B" }, abCopy { "COPY" };
    juce::TextButton modeTabs[3];
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<Combo>> combos;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String msg; juce::uint32 msgTime = 0;
    float meter = 0, hold = 0; int lastVersion = -1, lastMode = -1;
    struct Box { juce::Rectangle<float> r; juce::String title; juce::Colour col; juce::String onId; };
    std::vector<Box> boxes;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSamplerEditor)
};
