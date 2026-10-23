#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

struct EqSpectra
{
    static constexpr int kOrder = 13, kN = 1 << kOrder;
    float pre[kN / 2] {}, post[kN / 2] {}, sc[kN / 2] {};
    double fs = 48000;
};

class LumenEQEditor;

// the big interactive display: analyzer + curves + band nodes
class EqDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    EqDisplay (LumenEQProcessor& p, LumenEQEditor& e, const EqSpectra& s);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; mousePos = { -1, -1 }; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    float range = 12;                       // display range of the EQ curve (+- dB)
    bool showPre = true, showPost = true, showSc = true;
    juce::String message; juce::uint32 msgTime = 0;
private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (2).withTrimmedBottom (18).withTrimmedRight (34); }
    float fx (double f) const { auto r = plot(); return r.getX() + r.getWidth() * (float) (std::log10 (f / 10.0) / std::log10 (3000.0)); }
    double xf (float x) const { auto r = plot(); return 10.0 * std::pow (3000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()))); }
    float gy (float db) const { auto r = plot(); return r.getCentreY() - (r.getHeight() * 0.5f - 8) * juce::jlimit (-1.2f, 1.2f, db / range); }
    float yg (float y) const { auto r = plot(); return juce::jlimit (-30.0f, 30.0f, (r.getCentreY() - y) / (r.getHeight() * 0.5f - 8) * range); }
    float sy (float db) const { auto r = plot(); return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 96.0f) / 96.0f); }
    juce::Point<float> nodePos (int b) const;
    int hitNode (juce::Point<float> p) const;
    void showBandMenu (int b);
    LumenEQProcessor& proc; LumenEQEditor& ed; const EqSpectra& sp;
    int hover = -1, drag = -1; juce::Point<float> mousePos { -1, -1 }, dragStart; float startF = 0, startG = 0;
};

class LumenEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenEQEditor (LumenEQProcessor&);
    ~LumenEQEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    void select (int b);
    int selected() const { return sel; }
    void showMessage (const juce::String& m) { display->message = m; display->msgTime = juce::Time::getMillisecondCounter(); }
private:
    void timerCallback() override;
    void updateSpectra();
    void buildBandPanel();
    float pv (const juce::String& id) const { return proc.P (id); }
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;

    LumenEQProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    EqSpectra spectra;
    std::unique_ptr<EqDisplay> display;
    std::vector<std::unique_ptr<juce::Component>> owned, bandComps;
    juce::ComboBox presetBox, rangeBox;
    juce::TextButton prevB { "<" }, nextB { ">" }, abA { "A" }, abB { "B" }, abCopy { "COPY" }, preB { "PRE" }, postB { "POST" }, scB { "SC" };
    juce::TextButton matchB { "EQ MATCH" }, unmaskB { "UNMASK" }, soloB { "SOLO" }, delB { "DELETE" }, clearB { "CLEAR ALL" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::dsp::FFT fft { EqSpectra::kOrder };
    std::vector<float> fftBuf, window;
    int sel = -1, builtFor = -2, lastProgram = -1;
    float inM = 0, outM = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenEQEditor)
};
