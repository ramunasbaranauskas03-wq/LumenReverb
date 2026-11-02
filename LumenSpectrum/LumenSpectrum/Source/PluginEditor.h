#pragma once
#include "PluginProcessor.h"
#include "Look.h"
#include <set>
#include <map>

class LumenSpectrumEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LumenSpectrumEditor (LumenSpectrumProcessor&);
    ~LumenSpectrumEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    static constexpr int P = lumen::spec::kPoints;

    struct Track
    {
        int slot = -1, uid = 0, role = 0;
        juce::Colour col;
        juce::String name;
        bool self = false, idle = false, visible = true, bus = false, master = false;
        float db[P] {};      // raw averaged dB
        float pk[P] {};      // peak hold (raw)
    };
    struct Clash
    {
        int a = -1, b = -1;              // indices into tracks
        int p0 = 0, p1 = 0, pPeak = 0;
        float score = 0, mean = 0;
    };

    void timerCallback() override;
    void gather();
    static void smoothSpectrum (float* db, int radius);
    void computeDerived();
    void layoutHeader (juce::Rectangle<int>);
    void syncControls();

    // drawing
    void drawGrid (juce::Graphics&, bool dbAxis, bool percentAxis);
    void drawBars (juce::Graphics&);
    void drawCurves (juce::Graphics&, bool maskingStyle);
    void drawOwnership (juce::Graphics&);
    void drawMasking (juce::Graphics&);
    void drawSonogram (juce::Graphics&);
    void drawHover (juce::Graphics&);
    void drawBandStrip (juce::Graphics&);
    void drawLegend (juce::Graphics&);
    void drawEmptyHint (juce::Graphics&, const juce::String&);

    // geometry
    float xOfFreq (float f) const;
    float xOfPoint (int p) const { return xOfFreq (lumen::spec::pointFreq ((float) p)); }
    float freqOfX (float x) const;
    float yOfDb (float db) const;
    float tilt (float f) const;
    float topDb() const;
    float rangeDb() const;
    int view() const;
    bool param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }
    int choice (const char* id) const { return (int) proc.apvts.getRawParameterValue (id)->load(); }
    const Track* selfTrack() const { return selfIdx >= 0 ? &tracks[(size_t) selfIdx] : nullptr; }
    juce::String clashAdvice (const Clash&) const;

    LumenSpectrumProcessor& proc;
    lumen::spec::ui::Look look;
    juce::TooltipWindow tooltips { this, 500 };

    // controls
    juce::Label nameLabel;
    juce::ComboBox roleBox, colourBox, targetBox, resBox, barColBox, fftBox, avgBox, chanBox, slopeBox, rangeBox, smoothBox;
    juce::TextButton peakBtn { "PEAK" }, othersBtn { "OTHERS" }, freezeBtn { "FREEZE" }, snapBtn { "SNAP REF" }, clearBtn { "X" };
    juce::TextButton viewBtn[5];
    std::vector<std::unique_ptr<APVTS::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> btnAtt;

    // areas
    juce::Rectangle<int> displayArea, plot, stripArea, legendArea, listArea;
    std::vector<std::pair<juce::Rectangle<int>, int>> chipRects;   // legend chip -> uid

    // data
    std::vector<Track> tracks;
    std::map<int, std::array<float, P>> peakStore;
    std::set<int> hidden;
    int selfIdx = -1, masterIdx = -1;
    float mixPow[P] {}, mixDb[P] {}, heat[P] {};
    bool haveMix = false;
    int numInstruments = 0;
    std::vector<Clash> clashes;
    float corr = 1.0f, rmsDb = -120.0f, peakDb = -120.0f;
    bool frozen = false;
    juce::Point<float> hover { -1, -1 };
    juce::Image sono;
    int lastGeneration = -1;
    float displayTop = -24.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LumenSpectrumEditor)
};
