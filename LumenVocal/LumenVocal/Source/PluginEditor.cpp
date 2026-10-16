#include "PluginEditor.h"

using namespace lumen::led;

namespace
{
constexpr int W = 1120, HH = 724;
const juce::Colour MAIN = C::magenta;
const juce::Colour TUNE = C::cyan;

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
juce::String noteName (int m) { return keyNames()[((m % 12) + 12) % 12] + juce::String (m / 12 - 1); }

// vertical LED gain-reduction meter (0 .. -12 dB)
void grMeter (juce::Graphics& g, juce::Rectangle<float> r, float grDb, juce::Colour col)
{
    const int segs = 12;
    const float sh = r.getHeight() / segs;
    for (int i = 0; i < segs; ++i)
    {
        const bool lit = grDb > (float) i + 0.5f;
        auto s = juce::Rectangle<float> (r.getX(), r.getY() + i * sh + 1.0f, r.getWidth(), sh - 2.0f);
        g.setColour (lit ? col : col.withAlpha (0.08f)); g.fillRoundedRectangle (s, 1.5f);
        if (lit) { g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (s.expanded (1.5f), 2.0f); }
    }
}
}

//==============================================================================
void PitchView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, TUNE);
    const int key = (int) proc.apvts.getRawParameterValue (IDs::key)->load();
    const int scale = (int) proc.apvts.getRawParameterValue (IDs::scale)->load();
    const int N = LumenVocalProcessor::kHist;
    const int start = proc.histPos.load (std::memory_order_acquire);
    // follow the singer: centre the view on the recent notes
    float sum = 0; int cnt = 0;
    for (int k = N - 120; k < N; ++k) { const float m = proc.hist[(start + k) % N].det; if (m > 0) { sum += m; ++cnt; } }
    if (cnt > 10) centre += (sum / cnt - centre) * 0.05f;
    const float span = 18.0f;   // semitones visible
    const float lo = centre - span * 0.5f;
    auto yOf = [&] (float m) { return r.getBottom() - (m - lo) / span * r.getHeight(); };
    // lanes
    for (int n = (int) std::floor (lo); n <= (int) std::ceil (lo + span); ++n)
    {
        const float y0 = yOf ((float) n + 0.5f), y1 = yOf ((float) n - 0.5f);
        const bool in = lumen::vox::Scale::inScale (n, key, scale);
        const bool black = juce::String ("010100101010").substring (((n % 12) + 12) % 12, ((n % 12) + 12) % 12 + 1) == "1";
        g.setColour (in ? TUNE.withAlpha (black ? 0.07f : 0.11f) : juce::Colours::black.withAlpha (0.25f));
        g.fillRect (r.getX() + 34.0f, y0, r.getWidth() - 36.0f, y1 - y0);
        g.setColour (C::border.withAlpha (0.6f)); g.fillRect (r.getX() + 34.0f, y1, r.getWidth() - 36.0f, 1.0f);
        if (((n % 12) + 12) % 12 == key || n % 12 == 0)
        {
            g.setFont (font (8.5f)); g.setColour (in ? TUNE.withAlpha (0.8f) : C::dim);
            g.drawText (noteName (n), (int) r.getX() + 2, (int) (y0 + (y1 - y0) * 0.5f - 5), 30, 10, juce::Justification::centredRight);
        }
    }
    const float x0 = r.getX() + 36.0f, bw = (r.getWidth() - 38.0f) / (float) N;
    juce::Path det, out;
    bool dOn = false, oOn = false;
    for (int k = 0; k < N; ++k)
    {
        const auto& f = proc.hist[(start + k) % N];
        const float x = x0 + k * bw;
        if (f.det > 0) { const float y = yOf (f.det); if (! dOn) det.startNewSubPath (x, y); else det.lineTo (x, y); dOn = true; } else dOn = false;
        if (f.out > 0) { const float y = yOf (f.out); if (! oOn) out.startNewSubPath (x, y); else out.lineTo (x, y); oOn = true; } else oOn = false;
        if (f.target > 0 && f.det > 0)
        {
            const float yt = yOf (f.target);
            g.setColour (MAIN.withAlpha (0.45f)); g.fillRect (x, yt - 3.0f, bw + 0.5f, 6.0f);
        }
    }
    g.setColour (C::text.withAlpha (0.6f)); g.strokePath (det, juce::PathStrokeType (1.6f));
    glowPath (g, out, TUNE, 1.8f);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("grey = your voice   cyan = tuned   pink = target note", (int) x0 + 6, (int) r.getY() + 4, 320, 10, juce::Justification::centredLeft);
}

//==============================================================================
LumenVocalEditor::LumenVocalEditor (LumenVocalProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    pitch = std::make_unique<PitchView> (proc); addAndMakeVisible (*pitch); pitch->setBounds (26, 98, 690, 216);

    // TUNE section
    add<Toggle> (*this, owned, st, IDs::tune, "TUNE", TUNE)->setBounds (738, 98, 86, 26);
    auto* kb = add<Combo> (*this, owned, st, IDs::key, TUNE); kb->setBounds (832, 98, 70, 26);
    auto* sb = add<Combo> (*this, owned, st, IDs::scale, TUNE); sb->setBounds (908, 98, 186, 26);
    kb->setTooltip ("Key of the song (Smart Assist can find it)");
    const char* tid[] = { "retune", "amount", "transpose", "formant" };
    const char* tlb[] = { "RETUNE", "AMOUNT", "TRANSPOSE", "FORMANT" };
    for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, tid[i], tlb[i], TUNE, i >= 2)->setBounds (734 + i * 90, 214, 90, 100);

    // CLEAN / DYNAMICS / TONE
    const char* cid[] = { "low_cut", "deess", "deess_freq" };
    const char* clb[] = { "LOW CUT", "DE-ESS", "FREQ" };
    for (int i = 0; i < 3; ++i) add<Knob> (*this, owned, st, cid[i], clb[i], C::amber)->setBounds (26 + i * 86, 362, 86, 100);
    add<Knob> (*this, owned, st, IDs::comp, "COMPRESS", C::green)->setBounds (318, 352, 118, 122);
    const char* oid[] = { "body", "presence", "air", "warmth" };
    const char* olb[] = { "BODY", "PRESENCE", "AIR", "WARMTH" };
    for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, oid[i], olb[i], C::violet, i < 3)->setBounds (528 + i * 90, 362, 90, 100);

    // VOICES
    add<Knob> (*this, owned, st, IDs::dbl, "DOUBLE", MAIN)->setBounds (26, 548, 84, 100);
    add<Knob> (*this, owned, st, IDs::width, "WIDTH", MAIN)->setBounds (110, 548, 84, 100);
    auto* h1 = add<Combo> (*this, owned, st, IDs::h1, MAIN); h1->setBounds (204, 544, 110, 24);
    auto* h2 = add<Combo> (*this, owned, st, IDs::h2, MAIN); h2->setBounds (322, 544, 110, 24);
    h1->setTooltip ("Harmony voice 1: follows the key and scale (diatonic)");
    add<Knob> (*this, owned, st, IDs::h1Lvl, "HARM 1", MAIN)->setBounds (216, 574, 84, 92);
    add<Knob> (*this, owned, st, IDs::h2Lvl, "HARM 2", MAIN)->setBounds (334, 574, 84, 92);

    // SPACE
    add<Knob> (*this, owned, st, IDs::dly, "DELAY", C::blue)->setBounds (462, 548, 82, 100);
    auto* dd = add<Combo> (*this, owned, st, IDs::dlyDiv, C::blue); dd->setBounds (548, 544, 76, 24);
    add<Knob> (*this, owned, st, IDs::dlyFb, "FEEDBACK", C::blue)->setBounds (544, 574, 84, 92);
    add<Knob> (*this, owned, st, IDs::verb, "REVERB", C::blue)->setBounds (634, 548, 82, 100);
    add<Knob> (*this, owned, st, IDs::verbSize, "SIZE", C::blue)->setBounds (716, 548, 82, 100);
    auto* dk = add<Knob> (*this, owned, st, IDs::duck, "DUCKING", C::blue); dk->setBounds (798, 548, 82, 100);
    dk->slider.setTooltip ("Turns the delay and reverb down while you sing, back up in the gaps");

    // OUTPUT
    add<Knob> (*this, owned, st, IDs::in, "INPUT", C::text, true)->setBounds (912, 548, 92, 100);
    add<Knob> (*this, owned, st, IDs::out, "OUTPUT", C::text, true)->setBounds (1004, 548, 92, 100);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setColour (juce::ComboBox::textColourId, MAIN);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    addAndMakeVisible (presetBox); presetBox.setBounds (412, 18, 260, 28);
    for (auto* b : { &prevB, &nextB }) { b->setColour (juce::TextButton::buttonColourId, MAIN); addAndMakeVisible (*b); }
    prevB.setBounds (378, 18, 28, 28); nextB.setBounds (678, 18, 28, 28);
    prevB.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextB.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    for (auto* b : { &aB, &bB, &copyB }) addAndMakeVisible (*b);
    aB.setBounds (720, 18, 30, 28); bB.setBounds (752, 18, 30, 28); copyB.setBounds (786, 18, 40, 28);
    aB.onClick = [this] { proc.switchAB (0); };
    bB.onClick = [this] { proc.switchAB (1); };
    copyB.onClick = [this] { proc.copyAB(); };
    copyB.setColour (juce::TextButton::buttonColourId, C::dim);

    styleBox.addItemList ({ "Natural Pop", "EDM Lead", "Hard Tune", "Rap / Spoken", "Ballad" }, 1);
    styleBox.setSelectedId (2, juce::dontSendNotification);
    styleBox.setColour (juce::ComboBox::textColourId, C::cyan);
    addAndMakeVisible (styleBox); styleBox.setBounds (906, 362, 186, 26);
    assistB.setColour (juce::TextButton::buttonColourId, C::cyan);
    assistB.setTooltip ("Play 6 s of the vocal: finds the key, level, sibilance, dynamics and tone, then sets the whole chain for the chosen style");
    assistB.onClick = [this] { proc.startAssist ((LumenVocalProcessor::Style) (styleBox.getSelectedId() - 1)); };
    addAndMakeVisible (assistB); assistB.setBounds (906, 394, 90, 28);

    setSize (W, HH);
    startTimerHz (30);
}

LumenVocalEditor::~LumenVocalEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenVocalEditor::timerCallback()
{
    proc.finishAssistIfReady();
    if (proc.programVersion.load() != lastProgram) { lastProgram = proc.programVersion.load(); presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    const float d = -proc.deessGr.load(), c = proc.compGr.load();
    deessSm = d > deessSm ? d : deessSm * 0.85f; compSm = c > compSm ? c : compSm * 0.85f;
    aB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 0 ? MAIN : C::dim);
    bB.setColour (juce::TextButton::buttonColourId, proc.getABSlot() == 1 ? MAIN : C::dim);
    assistB.setEnabled (! proc.isAssisting());
    repaint();
}

void LumenVocalEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (MAIN.withAlpha (0.0f), 0, 64, TUNE.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, MAIN.withAlpha (0.75f)); line.addColour (0.75, TUNE.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (MAIN, 20, 0, TUNE, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logo.draw (g, { 146.0f, 18.0f, 170.0f, 28.0f }, "VOCAL", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("IN", 840, 18, 20, 10, juce::Justification::centredLeft);
    g.drawText ("OUT", 840, 36, 24, 10, juce::Justification::centredLeft);
    ledMeter (g, { 866.0f, 18.0f, 238.0f, 9.0f }, proc.inPk.load(), proc.inPk.load(), false, 32);
    ledMeter (g, { 866.0f, 36.0f, 238.0f, 9.0f }, proc.outPk.load(), proc.outPk.load(), false, 32);

    // ---- TUNE ----
    panel (g, { 16.0f, 72.0f, 1088.0f, 252.0f }, "PITCH", TUNE);
    {
        auto nr = juce::Rectangle<float> (738.0f, 134.0f, 356.0f, 70.0f);
        lcd (g, nr, TUNE);
        const float m = proc.curMidi.load();
        const int tgt = proc.curTarget.load();
        if (m > 0 && tgt > 0)
        {
            noteText.draw (g, nr.withWidth (150.0f).reduced (6.0f), noteName (tgt), TUNE, 3.4f, juce::Justification::centred, false, 10.5f);
            // cents meter: how far the singer is from the note (before tuning)
            const float cents = juce::jlimit (-50.0f, 50.0f, (m - (float) tgt) * 100.0f);
            auto cr = juce::Rectangle<float> (nr.getX() + 160.0f, nr.getY() + 18.0f, 180.0f, 14.0f);
            for (int k = -10; k <= 10; ++k)
            {
                const float cx = cr.getCentreX() + k * (cr.getWidth() / 21.0f);
                const bool lit = std::abs ((float) k * 5.0f - cents) < 3.0f;
                drawLed (g, { cx, cr.getCentreY() }, k == 0 ? 3.2f : 2.4f, std::abs (k) < 2 ? C::green : std::abs (k) < 5 ? C::amber : C::red, lit ? 1.0f : 0.07f);
            }
            g.setFont (mono (11.0f)); g.setColour (C::text.withAlpha (0.8f));
            g.drawText ((cents > 0 ? "+" : "") + juce::String ((int) std::round (cents)) + " ct  ->  0 ct", juce::Rectangle<float> (nr.getX() + 160.0f, nr.getY() + 38.0f, 180.0f, 16.0f), juce::Justification::centred);
        }
        else
        {
            g.setFont (font (11.0f)); g.setColour (C::dim);
            g.drawText ("no pitch (silence / breath / consonant)", nr, juce::Justification::centred);
        }
    }

    // ---- row 2 ----
    panel (g, { 16.0f, 332.0f, 270.0f, 158.0f }, "CLEAN", C::amber);
    panel (g, { 294.0f, 332.0f, 220.0f, 158.0f }, "DYNAMICS", C::green);
    panel (g, { 522.0f, 332.0f, 368.0f, 158.0f }, "TONE", C::violet);
    panel (g, { 898.0f, 332.0f, 206.0f, 158.0f }, "SMART ASSIST", C::cyan);
    grMeter (g, { 444.0f, 366.0f, 12.0f, 96.0f }, deessSm, C::amber);
    grMeter (g, { 466.0f, 366.0f, 12.0f, 96.0f }, compSm, C::green);
    g.setFont (font (8.0f)); g.setColour (C::dim);
    g.drawText ("DS", 438, 464, 24, 10, juce::Justification::centred);
    g.drawText ("GR", 460, 464, 24, 10, juce::Justification::centred);
    {
        auto sr = juce::Rectangle<float> (1000.0f, 394.0f, 92.0f, 28.0f);
        lcd (g, sr, C::cyan);
        if (proc.isAssisting())
        {
            const float pr = proc.assistProgress();
            for (int k = 0; k < 9; ++k) drawLed (g, { sr.getX() + 10.0f + k * 9.0f, sr.getCentreY() }, 2.6f, C::cyan, pr * 9.0f > (float) k ? 1.0f : 0.1f);
        }
        else status.draw (g, sr.reduced (3.0f, 2.0f), proc.getAssistResult().isEmpty() ? juce::String ("READY") : juce::String ("DONE"), C::cyan, 1.8f, juce::Justification::centred, false, 10.0f);
        g.setFont (font (9.0f));
        g.setColour (proc.getAssistResult().isNotEmpty() ? C::cyan.withAlpha (0.85f) : C::dim);
        g.drawFittedText (proc.getAssistResult().isNotEmpty() && ! proc.isAssisting() ? proc.getAssistResult()
                                                                                     : juce::String ("Pick a style, press ANALYZE and play 6 s of the vocal."),
                          juce::Rectangle<int> (906, 428, 190, 56), juce::Justification::topLeft, 4);
    }

    // ---- row 3 ----
    panel (g, { 16.0f, 498.0f, 428.0f, 214.0f }, "VOICES", MAIN);
    panel (g, { 452.0f, 498.0f, 438.0f, 214.0f }, "SPACE", C::blue);
    panel (g, { 898.0f, 498.0f, 206.0f, 214.0f }, "OUTPUT", C::text);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("HARMONY 1", 204, 530, 110, 12, juce::Justification::centredLeft);
    g.drawText ("HARMONY 2", 322, 530, 110, 12, juce::Justification::centredLeft);
    g.drawText ("TIME", 548, 530, 76, 12, juce::Justification::centredLeft);
    g.setFont (font (9.0f));
    g.drawFittedText ("Latency " + juce::String (proc.getLatencySamples()) + " smp (the DAW compensates it). For live monitoring sing through a send without Lumen Vocal.",
                      juce::Rectangle<int> (912, 656, 184, 50), juce::Justification::topLeft, 3);
    g.drawFittedText ("Doubles and harmonies are tuned to the key - set KEY / SCALE first (or use Smart Assist).",
                      juce::Rectangle<int> (26, 672, 410, 34), juce::Justification::topLeft, 2);
    g.drawFittedText ("Delay is synced to the song tempo. Ducking keeps the vocal dry while singing and lets the FX bloom in the gaps.",
                      juce::Rectangle<int> (462, 672, 420, 34), juce::Justification::topLeft, 2);
}
