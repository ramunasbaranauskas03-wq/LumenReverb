#pragma once
#include "PluginProcessor.h"
#include "StudioUi.h"

struct LiveSpectra
{
    static constexpr int kOrder = 13, kN = 1 << kOrder;
    float in[kN / 2] {}, out[kN / 2] {};
    float bandDb[DeviceReport::kBands] {};     // live tonal change per band (level matched)
    double fs = 48000;
};

class DeviceTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    DeviceTile (LumenMixCheckProcessor& p, int index);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    LumenMixCheckProcessor& proc; int index;
};

class ResponseView : public juce::Component
{
public:
    ResponseView (LumenMixCheckProcessor& p, const LiveSpectra& s) : proc (p), sp (s) {}
    void paint (juce::Graphics&) override;
private:
    LumenMixCheckProcessor& proc; const LiveSpectra& sp;
};

class ReportView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ReportView (LumenMixCheckProcessor& p) : proc (p) { setTooltip ("Click a row to listen through that device"); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh() { data = proc.reports(); repaint(); }
private:
    int rowAt (float y) const;
    LumenMixCheckProcessor& proc; std::vector<DeviceReport> data;
};

class LumenMixCheckEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenMixCheckEditor (LumenMixCheckProcessor&);
    ~LumenMixCheckEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
private:
    void timerCallback() override;
    void updateSpectra();
    void showTab (int t);
    float pv (const juce::String& id) const { return proc.P (id); }
    using Knob = lumen::led::Knob; using Toggle = lumen::led::Toggle; using Combo = lumen::led::Combo;
    Knob& knob (const juce::String& id, const juce::String& lbl, juce::Colour c, juce::Rectangle<int> r, bool bip = false);
    Toggle& toggle (const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r);

    LumenMixCheckProcessor& proc;
    lumen::led::LedLookAndFeel lnf;
    juce::TooltipWindow tips { this, 600 };
    LiveSpectra spectra;
    std::vector<std::unique_ptr<DeviceTile>> tiles;
    std::unique_ptr<ResponseView> response;
    std::unique_ptr<ReportView> report;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::TextButton tabListen { "LISTEN" }, tabReport { "ANALYZE ALL DEVICES" }, analyzeB { "ANALYZE LAST 8 SECONDS" }, loadIrB { "LOAD IR..." }, resetB { "RESET LUFS" }, loadEqB { "LOAD EQ FILE..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::dsp::FFT fft { LiveSpectra::kOrder };
    std::vector<float> fftBuf, window;
    int tab = 0, lastRepVersion = -1, lastDevice = -1;
    float bars[DeviceReport::kBands] {};
    juce::String irMsg, hpMsg;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenMixCheckEditor)
};
