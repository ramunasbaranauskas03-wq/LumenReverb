#include "PluginEditor.h"
using namespace lumen::led;
using namespace lumen::master;

static constexpr int W = 1280, HH = 840;
static const juce::Rectangle<int> kSpec { 12, 128, 916, 330 };
static const juce::Rectangle<int> kPanel { 12, 466, 916, 252 };
static const juce::Rectangle<int> kFoot { 12, 726, 916, 102 };
static const juce::Rectangle<int> kMeters { 940, 128, 328, 700 };

static juce::Colour moduleColour (int m)
{
    static const juce::Colour c[] { C::cyan, C::green, C::orange, C::amber, C::violet, C::red, C::magenta, C::orange, C::cyan, C::dim };
    return c[m];
}
static bool moduleOn (const LumenMasterProcessor& p, int m) { return p.param (moduleOnId (m)) > 0.5f; }

// ================================================================================================ ModuleCard
void ModuleCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = moduleOn (proc, module);
    const auto col = moduleColour (module);
    g.setColour (selected ? juce::Colour (0xff262b33) : C::panelTop); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (selected ? col.withAlpha (0.9f) : C::border); g.drawRoundedRectangle (r, 5.0f, selected ? 1.5f : 1.0f);
    // power LED
    auto led = juce::Rectangle<float> (r.getX() + 8, r.getY() + 9, 12, 12);
    g.setColour (on ? col : juce::Colour (0xff3a4049)); g.fillEllipse (led);
    if (on) { g.setColour (col.withAlpha (0.25f)); g.fillEllipse (led.expanded (3)); }
    g.setColour (on ? C::text : C::dim); g.setFont (font (10.5f));
    g.drawText (moduleName (module), r.withTrimmedLeft (26).withHeight (30), juce::Justification::centredLeft);
    // activity read-out
    juce::String t;
    auto dB1 = [] (float v) { return juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1); };
    switch (module)
    {
        case MEq:     t = proc.param (IDs::eqMode) > 0.5f ? "linear phase" : "natural phase"; break;
        case MShaper: t = juce::String (juce::roundToInt (proc.param (IDs::shAmount) * 100)) + " % to target"; break;
        case MBass:   t = proc.param (IDs::bassMono) > 0.5f ? "mono < " + juce::String (juce::roundToInt (proc.param (IDs::bassFreq))) + " Hz" : "stereo bass"; break;
        case MMulti:  t = dB1 (proc.multibandGr (0)) + " / " + dB1 (proc.multibandGr (3)) + " dB"; break;
        case MGlue:   t = dB1 (proc.moduleGr (MGlue)) + " dB GR"; break;
        case MSat:    t = juce::StringArray { "tape", "tube", "warm", "hard" }[juce::jlimit (0, 3, (int) proc.param (IDs::satMode))] + " +" + juce::String (proc.param (IDs::satDrive), 1) + " dB"; break;
        case MImager: t = "corr " + juce::String (proc.meters.corr.load(), 2); break;
        case MClip:   t = dB1 (proc.moduleGr (MClip)) + " dB clip"; break;
        case MMax:    t = dB1 (proc.moduleGr (MMax)) + " dB GR"; break;
        case MDither: t = juce::StringArray { "off", "16-bit TPDF", "16-bit shaped", "24-bit" }[juce::jlimit (0, 3, (int) proc.param (IDs::dither))]; break;
        default: break;
    }
    g.setColour (on ? col.interpolatedWith (C::text, 0.3f) : C::dim.withAlpha (0.6f)); g.setFont (mono (10.0f));
    g.drawText (t, r.withTrimmedTop (28).reduced (8, 0), juce::Justification::centredLeft);
    if (module < kModules - 1)
    {
        g.setColour (C::dim.withAlpha (0.5f));
        juce::Path a; const float y = r.getCentreY(), x = (float) getWidth();
        a.addTriangle (x - 1, y - 3, x - 1, y + 3, x + 3, y); g.fillPath (a);
    }
}

void ModuleCard::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < 28)
    {
        if (module == MDither) proc.setParam (IDs::dither, proc.param (IDs::dither) > 0.5f ? 0.0f : 2.0f);
        else proc.setParam (moduleOnId (module), moduleOn (proc, module) ? 0.0f : 1.0f);
    }
    ed.selectModule (module);
}

// ================================================================================================ SpectrumView
SpectrumView::SpectrumView (LumenMasterProcessor& p, LumenMasterEditor& e) : proc (p), ed (e)
{
    fft = std::make_unique<juce::dsp::FFT> (12);
    buf.assign (8192, 0.0f); win.resize (4096);
    for (int i = 0; i < 4096; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * (i + 0.5) / 4096));
    specIn.fill (-120.0f); specOut.fill (-120.0f);
    setTooltip ("Output spectrum (tilted +4.5 dB/oct so a balanced master looks flat), input in grey, genre target dashed.  EQ: drag the nodes, wheel = Q, double-click = reset");
}

float SpectrumView::xOf (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0) * getWidth()); }
double SpectrumView::fOf (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) x / getWidth())); }
float SpectrumView::yDb (double db) const { return (float) juce::jmap (juce::jlimit (-96.0, 0.0, db), -96.0, 0.0, (double) getHeight() - 16, 8.0); }
float SpectrumView::yEq (double db) const { return (float) juce::jmap (juce::jlimit (-15.0, 15.0, db), -15.0, 15.0, (double) getHeight() - 16, 8.0); }

void SpectrumView::update()
{
    const double fs = proc.sampleRate();
    const double norm = 2.0 / 2048.0;     // sum of the Hann window
    for (int which = 0; which < 2; ++which)
    {
        proc.copyScope (which == 1, buf.data(), 4096);
        for (int i = 0; i < 4096; ++i) buf[(size_t) i] *= win[(size_t) i];
        std::fill (buf.begin() + 4096, buf.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (buf.data(), true);
        auto& dst = which == 1 ? specOut : specIn;
        for (int k = 0; k < kPts; ++k)
        {
            const double f = 20.0 * std::pow (1000.0, (k + 0.5) / kPts);
            const double lo = f * std::pow (2.0, -1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
            const int b0 = std::max (1, (int) (lo / fs * 4096)), b1 = std::max (b0, std::min (2047, (int) (hi / fs * 4096)));
            double p = 0; for (int b = b0; b <= b1; ++b) p += (double) buf[(size_t) b] * buf[(size_t) b];
            p = std::sqrt (p / (b1 - b0 + 1));
            double db = 20.0 * std::log10 (p * norm + 1e-9) + 4.5 * std::log2 (f / 1000.0);       // display tilt
            auto& v = dst[(size_t) k];
            v = (float) (db > v ? v + (db - v) * 0.6 : v + (db - v) * 0.15);
        }
    }
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::field); g.fillRoundedRectangle (r, 4.0f);
    // grid
    g.setFont (mono (9.5f));
    for (double f : { 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xOf (f);
        g.setColour (C::border.withAlpha (0.5f)); g.drawVerticalLine ((int) x, 4.0f, r.getBottom() - 14);
        g.setColour (C::dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 18, r.getBottom() - 14, 36, 12), juce::Justification::centred);
    }
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = yEq (db);
        g.setColour (db == 0 ? C::border.brighter (0.15f) : C::border.withAlpha (0.35f)); g.drawHorizontalLine ((int) y, 0, r.getRight());
        g.setColour (C::dim.withAlpha (0.7f)); g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (r.getRight() - 30, y - 7, 26, 12), juce::Justification::centredRight);
    }
    // spectra
    auto specPath = [&] (const std::array<float, kPts>& s, bool fill)
    {
        juce::Path p; p.startNewSubPath (0, r.getBottom() - 14);
        for (int k = 0; k < kPts; ++k) p.lineTo ((k + 0.5f) / kPts * r.getWidth(), yDb (s[(size_t) k]));
        p.lineTo (r.getRight(), r.getBottom() - 14);
        if (fill) p.closeSubPath();
        return p;
    };
    g.setColour (C::dim.withAlpha (0.45f)); g.strokePath (specPath (specIn, false), juce::PathStrokeType (1.0f));
    auto po = specPath (specOut, true);
    g.setGradientFill (juce::ColourGradient (C::cyan.withAlpha (0.35f), 0, 0, C::cyan.withAlpha (0.04f), 0, r.getBottom(), false)); g.fillPath (po);
    g.setColour (C::cyan.withAlpha (0.9f)); g.strokePath (specPath (specOut, false), juce::PathStrokeType (1.3f));
    // genre target (aligned to the output level)
    {
        const auto& tg = targetCurve ((int) proc.param (IDs::genre));
        double off = 0; int n = 0;
        for (int k = 0; k < kPts; ++k) { const double f = 20.0 * std::pow (1000.0, (k + 0.5) / kPts); if (f > 60 && f < 12000) { off += specOut[(size_t) k] - (targetDb (tg, f) + 4.5 * std::log2 (f / 1000.0)); ++n; } }
        off = n ? off / n : 0;
        if (specOut[(size_t) kPts / 2] > -100)
        {
            juce::Path t;
            for (int k = 0; k < kPts; ++k)
            {
                const double f = 20.0 * std::pow (1000.0, (k + 0.5) / kPts);
                const float y = yDb (targetDb (tg, f) + 4.5 * std::log2 (f / 1000.0) + off), x = (k + 0.5f) / kPts * r.getWidth();
                if (k == 0) t.startNewSubPath (x, y); else t.lineTo (x, y);
            }
            juce::Path dashed; const float dl[] { 5.0f, 4.0f };
            juce::PathStrokeType (1.4f).createDashedStroke (dashed, t, dl, 2);
            g.setColour (C::amber.withAlpha (0.75f)); g.fillPath (dashed);
        }
    }
    const int sel = ed.selectedModule();
    // smart balance curve
    if (moduleOn (proc, MShaper) || sel == MShaper)
    {
        float c[SpectralShaper::kBands]; proc.getBalanceCurve (c);
        juce::Path p;
        for (int b = 0; b < SpectralShaper::kBands; ++b) { const float x = xOf (SpectralShaper::bandFreq (b)), y = yEq (c[b]); if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
        g.setColour (C::green.withAlpha (sel == MShaper ? 0.95f : 0.5f)); g.strokePath (p, juce::PathStrokeType (sel == MShaper ? 2.0f : 1.2f));
    }
    // EQ curve(s) + nodes
    if (moduleOn (proc, MEq) || sel == MEq)
    {
        bool anyMs = false; for (int b = 0; b < kEqBands; ++b) anyMs |= (int) proc.param (IDs::eq (b, "ch")) != 0;
        for (int role = 0; role < (anyMs ? 2 : 1); ++role)
        {
            juce::Path p;
            for (int x = 0; x <= (int) r.getWidth(); x += 2) { const float y = yEq (proc.eqResponseDb (fOf ((float) x), role)); if (x == 0) p.startNewSubPath ((float) x, y); else p.lineTo ((float) x, y); }
            const auto col = role == 0 ? (anyMs ? C::amber : C::cyan) : C::violet;
            g.setColour (col.withAlpha (sel == MEq ? 1.0f : 0.5f));
            g.strokePath (p, juce::PathStrokeType (sel == MEq ? 2.2f : 1.3f));
        }
        if (sel == MEq)
            for (int b = 0; b < kEqBands; ++b)
            {
                const bool on = proc.param (IDs::eq (b, "on")) > 0.5f;
                const int type = (int) proc.param (IDs::eq (b, "type")), ch = (int) proc.param (IDs::eq (b, "ch"));
                const float x = xOf (proc.param (IDs::eq (b, "freq"))), y = (type == LowCut || type == HighCut) ? yEq (0) : yEq (proc.param (IDs::eq (b, "gain")));
                const auto col = ch == 0 ? C::cyan : ch == 1 ? C::amber : C::violet;
                const float rad = b == drag || b == hover ? 9.0f : 7.5f;
                g.setColour (on ? col : C::dim); g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
                g.setColour (C::field); g.setFont (font (10.0f)); g.drawText (juce::String (b + 1), juce::Rectangle<float> (x - rad, y - rad, rad * 2, rad * 2), juce::Justification::centred);
            }
    }
    // legend
    g.setFont (font (10.0f, false));
    auto lg = juce::Rectangle<float> (10, 8, 400, 14);
    auto item = [&] (juce::Colour c, const juce::String& t) { g.setColour (c); g.fillRect (lg.getX(), lg.getCentreY() - 1, 12.0f, 2.0f); g.setColour (C::dim); g.drawText (t, lg.withTrimmedLeft (16), juce::Justification::centredLeft); lg.translate (16 + juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 14, 0); };
    item (C::cyan, "output"); item (C::dim, "input"); item (C::amber, "target: " + genreNames()[(int) proc.param (IDs::genre)]);
    if (moduleOn (proc, MShaper)) item (C::green, "smart balance");
    if (hover >= 0 || drag >= 0)
    {
        const int b = drag >= 0 ? drag : hover;
        juce::String t; t << "Band " << (b + 1) << "   " << proc.apvts.getParameter (IDs::eq (b, "freq"))->getCurrentValueAsText() << "   "
                          << proc.apvts.getParameter (IDs::eq (b, "gain"))->getCurrentValueAsText() << "   Q " << proc.apvts.getParameter (IDs::eq (b, "q"))->getCurrentValueAsText();
        g.setColour (C::text); g.setFont (mono (11.0f)); g.drawText (t, r.reduced (10, 6).withHeight (16).translated (0, 16), juce::Justification::topRight);
    }
}

int SpectrumView::nodeAt (juce::Point<float> p) const
{
    if (ed.selectedModule() != MEq) return -1;
    int best = -1; float bd = 14.0f;
    for (int b = 0; b < kEqBands; ++b)
    {
        const int type = (int) proc.param (IDs::eq (b, "type"));
        const float x = xOf (proc.param (IDs::eq (b, "freq"))), y = (type == LowCut || type == HighCut) ? yEq (0) : yEq (proc.param (IDs::eq (b, "gain")));
        const float d = p.getDistanceFrom ({ x, y });
        if (d < bd) { bd = d; best = b; }
    }
    return best;
}
void SpectrumView::mouseMove (const juce::MouseEvent& e) { const int n = nodeAt (e.position); if (n != hover) { hover = n; repaint(); } }
void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    drag = nodeAt (e.position);
    if (drag >= 0)
    {
        for (auto* w : { "freq", "gain" }) proc.apvts.getParameter (IDs::eq (drag, w))->beginChangeGesture();
        if (proc.param (IDs::eq (drag, "on")) < 0.5f) proc.setParam (IDs::eq (drag, "on"), 1);
    }
}
void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto set = [&] (const char* w, float v) { if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (IDs::eq (drag, w)))) rp->setValueNotifyingHost (rp->convertTo0to1 (v)); };
    set ("freq", (float) fOf (e.position.x));
    const int type = (int) proc.param (IDs::eq (drag, "type"));
    if (type != LowCut && type != HighCut)
        set ("gain", (float) juce::jlimit (-12.0, 12.0, juce::jmap ((double) e.position.y, (double) getHeight() - 16, 8.0, -15.0, 15.0)));
    repaint();
}
void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0) for (auto* w : { "freq", "gain" }) proc.apvts.getParameter (IDs::eq (drag, w))->endChangeGesture();
    drag = -1;
}
void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e) { const int n = nodeAt (e.position); if (n >= 0) proc.setParam (IDs::eq (n, "gain"), 0); }
void SpectrumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int n = nodeAt (e.position); if (n < 0) return;
    proc.setParam (IDs::eq (n, "q"), juce::jlimit (0.1f, 10.0f, proc.param (IDs::eq (n, "q")) * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f)));
}

// ================================================================================================ Editor
LumenMasterEditor::Knob& LumenMasterEditor::knob (int mod, const juce::String& id, const juce::String& lbl, juce::Colour c, int x, int y, bool bip, int w)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip);
    k->setBounds (kPanel.getX() + x, kPanel.getY() + y, w, 70); addChildComponent (*k);
    auto* raw = k.get(); panel[mod].push_back (raw); owned.push_back (std::move (k));
    return *raw;
}
LumenMasterEditor::Toggle& LumenMasterEditor::toggle (int mod, const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r)
{
    auto t = std::make_unique<Toggle> (proc.apvts, id, txt, c, txt.isEmpty());
    t->setBounds (r.translated (kPanel.getX(), kPanel.getY())); addChildComponent (*t);
    auto* raw = t.get(); panel[mod].push_back (raw); owned.push_back (std::move (t));
    return *raw;
}
LumenMasterEditor::Combo& LumenMasterEditor::combo (int mod, const juce::String& id, juce::Colour c, juce::Rectangle<int> r)
{
    auto t = std::make_unique<Combo> (proc.apvts, id, c);
    t->setBounds (r.translated (kPanel.getX(), kPanel.getY())); addChildComponent (*t);
    auto* raw = t.get(); panel[mod].push_back (raw); owned.push_back (std::move (t));
    return *raw;
}

LumenMasterEditor::LumenMasterEditor (LumenMasterProcessor& p) : AudioProcessorEditor (&p), proc (p), spectrum (p, *this)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (spectrum); spectrum.setBounds (kSpec);
    for (int m = 0; m < kModules; ++m)
    {
        cards.push_back (std::make_unique<ModuleCard> (p, *this, m));
        addAndMakeVisible (*cards.back());
        const float cw = (W - 24) / (float) kModules;
        cards.back()->setBounds ((int) (12 + m * cw), 64, (int) cw - 6, 56);
        cards.back()->setTooltip ("Click the dot to switch " + juce::String (moduleName (m)) + " on/off, click the card to edit it");
    }
    // header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setColour (juce::ComboBox::textColourId, C::text);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) { proc.setCurrentProgram (i); showMessage ("Preset: " + proc.getProgramName (i)); } };
    addAndMakeVisible (presetBox); presetBox.setBounds (238, 15, 196, 26);
    genreBox = std::make_unique<Combo> (proc.apvts, IDs::genre, C::amber); addAndMakeVisible (*genreBox); genreBox->setBounds (440, 15, 184, 26);
    genreBox->setTooltip ("Genre target: tonal balance curve for Smart Balance / Assistant, and the club loudness");
    destBox = std::make_unique<Combo> (proc.apvts, IDs::platform, C::green); addAndMakeVisible (*destBox); destBox->setBounds (630, 15, 126, 26);
    destBox->setTooltip ("Where the master goes: sets the loudness target and the true-peak ceiling the Assistant uses");
    for (auto* b : { &assistBtn, &refBtn, &abA, &abB, &abCopy, &resetBtn }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    assistBtn.setBounds (762, 13, 158, 30); refBtn.setBounds (926, 13, 48, 30);
    abA.setBounds (982, 13, 30, 30); abB.setBounds (1014, 13, 30, 30); abCopy.setBounds (1046, 13, 50, 30);
    assistBtn.setTooltip ("Play the loudest part of your track: Lumen Master listens 15 s, then sets EQ, balance, glue, width and the exact gain for your loudness target");
    refBtn.setTooltip ("Load a reference track (WAV / AIFF / FLAC / MP3): its tonal balance and loudness become the target");
    assistBtn.onClick = [this]
    {
        const int st = proc.assistState();
        if (st == LumenMasterProcessor::Listening || st == LumenMasterProcessor::Analysing) proc.cancelAssistant();
        else proc.startAssistant();
    };
    refBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Reference track", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        { if (fc.getResult().existsAsFile()) { juce::String m; proc.loadReference (fc.getResult(), m); showMessage (m); } });
    };
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); };
    abCopy.onClick = [this] { proc.copyAB(); showMessage (juce::String ("Copied ") + (proc.getABSlot() == 0 ? "A to B" : "B to A")); };
    bypassT = std::make_unique<Toggle> (proc.apvts, IDs::bypass, "BYPASS", C::red); addAndMakeVisible (*bypassT); bypassT->setBounds (1104, 13, 82, 30);
    matchT = std::make_unique<Toggle> (proc.apvts, IDs::match, "MATCH", C::green); addAndMakeVisible (*matchT); matchT->setBounds (1190, 13, 78, 30);
    bypassT->setTooltip ("Hear the unprocessed mix (time aligned)"); matchT->setTooltip ("Level-matched compare: bypass plays as loud as the master, so you judge the sound, not the volume");
    // global controls (meter column bottom)
    inKnob = std::make_unique<Knob> (proc.apvts, IDs::inGain, "INPUT", C::cyan, true); addAndMakeVisible (*inKnob); inKnob->setBounds (948, 742, 58, 70);
    outKnob = std::make_unique<Knob> (proc.apvts, IDs::outGain, "OUTPUT", C::cyan, true); addAndMakeVisible (*outKnob); outKnob->setBounds (1008, 742, 58, 70);
    customKnob = std::make_unique<Knob> (proc.apvts, IDs::customLufs, "CUSTOM", C::green); addChildComponent (*customKnob); customKnob->setBounds (1068, 742, 58, 70);
    lowLatT = std::make_unique<Toggle> (proc.apvts, IDs::lowLat, "LOW LATENCY", C::amber); addAndMakeVisible (*lowLatT); lowLatT->setBounds (1132, 744, 128, 24);
    lowLatT->setTooltip ("For producing with the master on: natural-phase EQ and no Smart Balance, ~5 ms latency. Turn off for the final master.");
    monoT = std::make_unique<Toggle> (proc.apvts, IDs::mono, "MONO CHECK", C::violet); addAndMakeVisible (*monoT); monoT->setBounds (1132, 772, 128, 24);
    resetBtn.setBounds (1132, 800, 128, 24); resetBtn.onClick = [this] { proc.resetMeters(); histShort.clear(); histGr.clear(); };
    buildPanels();
    selectModule (MMax);
    setSize (W, HH);
    startTimerHz (30);
}

LumenMasterEditor::~LumenMasterEditor() { setLookAndFeel (nullptr); }

void LumenMasterEditor::buildPanels()
{
    const int y0 = 36;
    // ---- EQ: 6 band strips
    combo (MEq, IDs::eqMode, C::cyan, { 786, 5, 118, 22 });
    for (int b = 0; b < kEqBands; ++b)
    {
        const int x = 10 + b * 150;
        toggle (MEq, IDs::eq (b, "on"), "", C::cyan, { x, y0, 24, 24 });
        combo (MEq, IDs::eq (b, "type"), C::cyan, { x + 28, y0 + 1, 112, 22 });
        combo (MEq, IDs::eq (b, "ch"), C::amber, { x, y0 + 30, 68, 22 });
        combo (MEq, IDs::eq (b, "slope"), C::dim, { x + 72, y0 + 30, 68, 22 });
        knob (MEq, IDs::eq (b, "freq"), "FREQ", C::cyan, x - 2, y0 + 60, false, 48);
        knob (MEq, IDs::eq (b, "gain"), "GAIN", C::cyan, x + 46, y0 + 60, true, 48);
        knob (MEq, IDs::eq (b, "q"), "Q", C::cyan, x + 94, y0 + 60, false, 48);
    }
    // ---- Smart balance
    {
        const char* lb[] { "AMOUNT", "TAME RES", "RECOVER", "SPEED", "LOW", "HIGH" };
        const juce::String ids[] { IDs::shAmount, IDs::shTame, IDs::shBoost, IDs::shSpeed, IDs::shLow, IDs::shHigh };
        for (int i = 0; i < 6; ++i) knob (MShaper, ids[i], lb[i], C::green, 16 + i * 72, y0 + 10, false, 64);
    }
    // ---- Bass
    knob (MBass, IDs::bassFreq, "FREQ", C::orange, 16, y0 + 10, false, 64);
    knob (MBass, IDs::bassPunch, "PUNCH", C::orange, 88, y0 + 10, true, 64);
    knob (MBass, IDs::bassSustain, "SUSTAIN", C::orange, 160, y0 + 10, true, 64);
    knob (MBass, IDs::bassGain, "GAIN", C::orange, 232, y0 + 10, true, 64);
    toggle (MBass, IDs::bassMono, "MONO BASS", C::orange, { 310, y0 + 26, 130, 26 });
    // ---- Multiband
    knob (MMulti, IDs::mbX1, "X-OVER 1", C::amber, 10, y0 + 4, false, 60);
    knob (MMulti, IDs::mbX2, "X-OVER 2", C::amber, 72, y0 + 4, false, 60);
    knob (MMulti, IDs::mbX3, "X-OVER 3", C::amber, 134, y0 + 4, false, 60);
    knob (MMulti, IDs::mbAttack, "ATTACK", C::amber, 10, y0 + 112, false, 60);
    knob (MMulti, IDs::mbRelease, "RELEASE", C::amber, 72, y0 + 112, false, 60);
    knob (MMulti, IDs::mbMix, "MIX", C::amber, 134, y0 + 112, false, 60);
    for (int b = 0; b < 4; ++b)
    {
        const int x = 214 + b * 174;
        knob (MMulti, IDs::mb (b, "thr"), "THRESH", C::amber, x, y0 + 4, false, 54);
        knob (MMulti, IDs::mb (b, "ratio"), "RATIO", C::amber, x + 54, y0 + 4, false, 54);
        knob (MMulti, IDs::mb (b, "gain"), "GAIN", C::amber, x + 108, y0 + 4, true, 54);
    }
    // ---- Glue
    knob (MGlue, IDs::glueThr, "THRESHOLD", C::violet, 16, y0 + 10, false, 70);
    combo (MGlue, IDs::glueRatio, C::violet, { 100, y0 + 4, 104, 24 });
    combo (MGlue, IDs::glueAttack, C::violet, { 100, y0 + 34, 104, 24 });
    combo (MGlue, IDs::glueRelease, C::violet, { 100, y0 + 64, 104, 24 });
    combo (MGlue, IDs::glueSc, C::violet, { 100, y0 + 94, 104, 24 });
    knob (MGlue, IDs::glueMakeup, "MAKEUP", C::violet, 216, y0 + 10, false, 64);
    knob (MGlue, IDs::glueMix, "MIX", C::violet, 286, y0 + 10, false, 64);
    // ---- Saturate
    combo (MSat, IDs::satMode, C::red, { 16, y0 + 24, 100, 24 });
    knob (MSat, IDs::satDrive, "DRIVE", C::red, 128, y0 + 10, false, 64);
    knob (MSat, IDs::satTone, "TONE", C::red, 200, y0 + 10, true, 64);
    knob (MSat, IDs::satExciter, "EXCITER", C::red, 272, y0 + 10, false, 64);
    knob (MSat, IDs::satMix, "MIX", C::red, 344, y0 + 10, false, 64);
    // ---- Imager
    knob (MImager, IDs::imgX1, "LOW X", C::magenta, 16, y0 + 10, false, 64);
    knob (MImager, IDs::imgX2, "HIGH X", C::magenta, 88, y0 + 10, false, 64);
    knob (MImager, IDs::imgLow, "WIDTH LOW", C::magenta, 176, y0 + 10, false, 70);
    knob (MImager, IDs::imgMid, "WIDTH MID", C::magenta, 252, y0 + 10, false, 70);
    knob (MImager, IDs::imgHigh, "WIDTH HIGH", C::magenta, 328, y0 + 10, false, 70);
    // ---- Clipper
    knob (MClip, IDs::clipDrive, "DRIVE", C::orange, 16, y0 + 10, false, 64);
    knob (MClip, IDs::clipSoft, "SOFTNESS", C::orange, 88, y0 + 10, false, 64);
    // ---- Maximizer
    knob (MMax, IDs::maxDrive, "GAIN", C::cyan, 16, y0 + 4, false, 84);
    knob (MMax, IDs::maxCeiling, "CEILING", C::cyan, 104, y0 + 4, false, 64);
    knob (MMax, IDs::maxRelease, "RELEASE", C::cyan, 172, y0 + 4, false, 64);
    knob (MMax, IDs::maxLink, "LINK", C::cyan, 240, y0 + 4, false, 64);
    combo (MMax, IDs::maxMode, C::cyan, { 16, y0 + 96, 140, 24 });
    toggle (MMax, IDs::maxDelta, "DELTA", C::amber, { 164, y0 + 96, 92, 24 });
    // ---- Dither
    combo (MDither, IDs::dither, C::dim, { 16, y0 + 24, 160, 24 });
}

void LumenMasterEditor::selectModule (int m)
{
    selMod = m;
    for (int k = 0; k < kModules; ++k) { cards[(size_t) k]->selected = k == m; cards[(size_t) k]->repaint(); for (auto* c : panel[k]) c->setVisible (k == m); }
    repaint();
}

void LumenMasterEditor::timerCallback()
{
    spectrum.update();
    for (auto& c : cards) c->repaint();
    histShort.push_back (proc.meters.outS.load()); if (histShort.size() > 300) histShort.erase (histShort.begin());
    histGr.push_back (proc.moduleGr (MMax)); if (histGr.size() > 380) histGr.erase (histGr.begin());
    pkIn = std::max (proc.meters.inPk.load(), pkIn * 0.88f); pkOutL = std::max (proc.meters.outPkL.load(), pkOutL * 0.88f); pkOutR = std::max (proc.meters.outPkR.load(), pkOutR * 0.88f);
    const int st = proc.assistState();
    assistBtn.setButtonText (st == LumenMasterProcessor::Listening ? "LISTENING... STOP" : st == LumenMasterProcessor::Analysing ? "ANALYSING..." : "MASTER ASSISTANT");
    assistBtn.setToggleState (st == LumenMasterProcessor::Listening || st == LumenMasterProcessor::Analysing, juce::dontSendNotification);
    if (st != lastAssist) { lastAssist = st; if (st == LumenMasterProcessor::Done || st == LumenMasterProcessor::Failed) { selectModule (MMax); showMessage (proc.assistMessage()); } }
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    customKnob->setVisible ((int) pv (IDs::platform) == 6);
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1) presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    if (msg.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime > 20000) msg.clear();
    repaint (kMeters.expanded (4)); repaint (kFoot); repaint (kPanel); repaint (0, 0, W, 60);
}

void LumenMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (22.0f)); g.drawText ("LUMEN", 16, 13, 90, 30, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("MASTER", 98, 13, 120, 30, juce::Justification::centredLeft);

    // module panel
    const auto col = moduleColour (selMod);
    lumen::led::panel (g, kPanel.toFloat(), moduleName (selMod), col, moduleOn (proc, selMod));
    drawPanelExtras (g, kPanel.toFloat());
    // footer: assistant / messages
    {
        auto f = kFoot.toFloat();
        lumen::led::panel (g, f, "MASTER ASSISTANT", C::cyan, true);
        auto r = f.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (10);
        lcd (g, r, C::cyan);
        const int st = proc.assistState();
        g.setFont (mono (11.0f));
        if (st == LumenMasterProcessor::Listening)
        {
            const float p = proc.assistProgress();
            auto bar = r.reduced (10).removeFromBottom (8);
            g.setColour (C::track); g.fillRoundedRectangle (bar, 3); g.setColour (C::cyan); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * p), 3);
            g.setColour (C::text); g.drawText (proc.assistMessage() + "   " + juce::String (juce::roundToInt (p * 100)) + " %", r.reduced (10, 6).withTrimmedBottom (14), juce::Justification::centredLeft);
        }
        else if (st == LumenMasterProcessor::Analysing) { g.setColour (C::amber); g.drawText ("Analysing: tonal balance, dynamics, stereo, then rendering the chain to hit the loudness target exactly...", r.reduced (10, 6), juce::Justification::centredLeft); }
        else
        {
            juce::String t = msg.isNotEmpty() ? msg : juce::String ("Target: ") + genreNames()[(int) pv (IDs::genre)] + "  -  " + platformNames()[(int) pv (IDs::platform)]
                + "  -  " + juce::String (proc.targetLufs(), 1) + " LUFS integrated, ceiling " + juce::String (proc.targetCeiling(), 1) + " dBTP.   Press MASTER ASSISTANT and play the drop.";
            g.setColour (msg.isNotEmpty() ? C::text : C::dim); g.drawFittedText (t, r.reduced (10, 4).toNearestInt(), juce::Justification::centredLeft, 3, 0.85f);
        }
    }
    drawMeters (g, kMeters.toFloat());
}

void LumenMasterEditor::drawPanelExtras (juce::Graphics& g, juce::Rectangle<float> p)
{
    const float px = p.getX(), py = p.getY();
    g.setFont (font (10.5f, false));
    auto note = [&] (juce::Rectangle<float> r, const juce::String& t) { g.setColour (C::dim); g.drawFittedText (t, r.toNearestInt(), juce::Justification::topLeft, 8, 0.9f); };
    auto grBar = [&] (juce::Rectangle<float> r, float db, juce::Colour c, const juce::String& label)
    {
        g.setColour (C::field); g.fillRoundedRectangle (r, 3);
        const float t = juce::jlimit (0.0f, 1.0f, -db / 12.0f);
        g.setColour (c); g.fillRoundedRectangle (r.withHeight (r.getHeight() * t), 3);
        g.setColour (C::dim); g.setFont (mono (9.5f));
        g.drawText (label, r.withY (r.getBottom() + 2).withHeight (12), juce::Justification::centred);
        g.drawText (juce::String (db, 1), r.withY (r.getBottom() + 14).withHeight (12), juce::Justification::centred);
    };
    switch (selMod)
    {
        case MEq:
            note ({ px + 10, py + 214, 890, 30 }, "Drag the numbered nodes in the analyser.  Linear phase = no phase shift (mastering grade, adds latency), Natural = analog-style.  Channel: Stereo / Mid / Side per band.");
            break;
        case MShaper:
            note ({ px + 470, py + 40, 430, 180 }, "Smart Balance listens continuously and gently steers the tonal balance toward the selected target ("
                  + genreNames()[(int) pv (IDs::genre)] + ").  AMOUNT = how far, TAME RES = pulls down harsh resonances that stick out, RECOVER = how much it may lift masked areas, "
                  "SPEED = how fast it follows the music.  LOW / HIGH limit its range.  Same gain on L and R, so the stereo image never moves.  The green line in the analyser is what it is doing right now.");
            break;
        case MBass:
            note ({ px + 470, py + 40, 430, 180 }, "Low-end control below FREQ.  PUNCH emphasises kick / bass attacks, SUSTAIN lengthens or tightens the tails, GAIN sets the overall low-end level.  "
                  "MONO BASS makes everything below FREQ mono - essential for club systems and vinyl.");
            grBar ({ px + 450, py + 44, 14, 120 }, -std::abs (proc.moduleGr (MBass)), C::orange, "dB");
            break;
        case MMulti:
            for (int b = 0; b < 4; ++b) grBar ({ px + 214 + b * 174 + 60.0f, py + 126, 42, 70 }, proc.multibandGr (b), C::amber, juce::StringArray { "LOW", "LOW-MID", "HI-MID", "HIGH" }[b]);
            break;
        case MGlue:
            grBar ({ px + 380, py + 44, 22, 150 }, proc.moduleGr (MGlue), C::violet, "GR");
            note ({ px + 430, py + 40, 470, 180 }, "SSL-style bus compressor that glues the mix together.  Aim for 1-2 dB of gain reduction on the loudest parts.  "
                  "Auto release follows the music; the side-chain high-pass keeps the kick from pumping the whole master.");
            break;
        case MSat:
            note ({ px + 430, py + 40, 470, 180 }, "4x oversampled harmonic saturation.  Tape = smooth and glued, Tube = warm even harmonics, Warm = soft cubic, Hard = edgy.  "
                  "TONE tilts which frequencies saturate most, EXCITER adds airy harmonics above 4.5 kHz, MIX blends it in parallel.  When off it is bit-transparent.");
            break;
        case MImager:
            note ({ px + 420, py + 40, 480, 120 }, "Width per band (phase aligned, mono compatible).  100 % = unchanged.  Keep LOW at or below 100 % for a solid club low end; widen MID / HIGH for size.");
            {
                auto r = juce::Rectangle<float> (px + 420, py + 150, 400, 16);
                const float c = proc.meters.corr.load();
                g.setColour (C::field); g.fillRoundedRectangle (r, 3);
                const float x = r.getX() + (c + 1.0f) * 0.5f * r.getWidth();
                g.setColour (c < 0 ? C::red : c < 0.3f ? C::amber : C::green); g.fillRoundedRectangle (juce::Rectangle<float> (x - 4, r.getY(), 8, r.getHeight()), 2);
                g.setColour (C::dim); g.setFont (mono (9.5f)); g.drawText ("-1        correlation        +1", r.translated (0, 18), juce::Justification::centred);
            }
            break;
        case MClip:
            grBar ({ px + 180, py + 44, 22, 150 }, proc.moduleGr (MClip), C::orange, "CLIP");
            note ({ px + 230, py + 40, 670, 180 }, "4x oversampled soft clipper in front of the maximizer.  It shaves the very fastest peaks (kick / snare transients) so the limiter has less to do - "
                  "the modern way to get loud EDM masters that still punch.  1-3 dB of DRIVE is usually inaudible.  SOFTNESS = knee.");
            break;
        case MMax:
        {
            auto r = juce::Rectangle<float> (px + 330, py + 36, 570, 150);
            g.setColour (C::field); g.fillRoundedRectangle (r, 4);
            g.setColour (C::border.withAlpha (0.5f));
            for (int db = 3; db <= 12; db += 3) { const float y = r.getY() + r.getHeight() * db / 12.0f; g.drawHorizontalLine ((int) y, r.getX(), r.getRight()); }
            juce::Path pth; pth.startNewSubPath (r.getRight(), r.getY());
            for (int i = (int) histGr.size() - 1, k = 0; i >= 0; --i, ++k)
            {
                const float x = r.getRight() - k * (r.getWidth() / 380.0f), y = r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -histGr[(size_t) i] / 12.0f);
                pth.lineTo (x, y);
            }
            pth.lineTo (r.getRight() - histGr.size() * (r.getWidth() / 380.0f), r.getY()); pth.closeSubPath();
            g.setColour (C::cyan.withAlpha (0.35f)); g.fillPath (pth);
            g.setColour (C::dim); g.setFont (mono (9.5f)); g.drawText ("gain reduction (0 .. -12 dB)", r.reduced (6, 4), juce::Justification::bottomLeft);
            note ({ px + 330, py + 192, 570, 50 }, "True-peak lookahead limiter (ISP safe).  Transparent = clean, Punchy = faster, Loud = with transient shaving, Smooth = slow and gentle.  "
                  "DELTA lets you hear only what the limiter removes.");
            break;
        }
        case MDither:
            note ({ px + 200, py + 40, 700, 180 }, "Dither only when you bounce to 16-bit (CD / some distributors) - choose 16-bit Shaped.  For 24-bit or 32-bit float exports leave it OFF.  "
                  "It must be the very last thing in the chain (it is).");
            break;
        default: break;
    }
}

void LumenMasterEditor::drawMeters (juce::Graphics& g, juce::Rectangle<float> r)
{
    lumen::led::panel (g, r, "LOUDNESS", C::cyan, true);
    auto a = r.reduced (12, 0).withTrimmedTop (30);
    const float tgt = proc.targetLufs();
    const float I = proc.meters.outI.load();
    // integrated
    {
        auto box = a.removeFromTop (92);
        lcd (g, box, C::cyan);
        g.setColour (C::dim); g.setFont (font (10.0f)); g.drawText ("INTEGRATED", box.reduced (10, 6), juce::Justification::topLeft);
        g.setColour (I > -70 ? (std::abs (I - tgt) < 1.0f ? C::green : C::text) : C::dim); g.setFont (font (34.0f));
        g.drawText (I > -70 ? juce::String (I, 1) : juce::String ("--.-"), box.reduced (10, 0).withTrimmedTop (16).withTrimmedBottom (18), juce::Justification::centredLeft);
        g.setColour (C::dim); g.setFont (font (12.0f, false)); g.drawText ("LUFS", box.reduced (10, 0).withTrimmedTop (16).withTrimmedBottom (18).withTrimmedLeft (106), juce::Justification::centredLeft);
        g.setFont (mono (10.5f));
        g.setColour (C::amber); g.drawText ("target " + juce::String (tgt, 1) + (I > -70 ? "   (" + juce::String (I - tgt >= 0 ? "+" : "") + juce::String (I - tgt, 1) + ")" : juce::String()), box.reduced (10, 6), juce::Justification::bottomLeft);
    }
    a.removeFromTop (6);
    auto row = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto rr = a.removeFromTop (20);
        g.setColour (C::dim); g.setFont (font (10.5f)); g.drawText (k, rr, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (mono (12.0f)); g.drawText (v, rr, juce::Justification::centredRight);
    };
    auto fmt = [] (float v, const char* u) { return v > -70 ? juce::String (v, 1) + u : juce::String ("--") + u; };
    const float tp = proc.meters.tp.load(), S = proc.meters.outS.load();
    row ("SHORT-TERM", fmt (S, " LUFS"), C::text);
    row ("MOMENTARY", fmt (proc.meters.outM.load(), " LUFS"), C::text);
    row ("TRUE PEAK (max)", fmt (tp, " dBTP"), tp > proc.targetCeiling() + 0.05f ? C::red : C::green);
    row ("LOUDNESS RANGE", juce::String (proc.meters.lra.load(), 1) + " LU", C::text);
    row ("PLR (peak to loudness)", I > -70 ? juce::String (tp - I, 1) + " dB" : juce::String ("--"), C::text);
    row ("INPUT INTEGRATED", fmt (proc.meters.inI.load(), " LUFS"), C::dim);
    a.removeFromTop (6);
    // short-term history
    {
        auto h = a.removeFromTop (90);
        g.setColour (C::field); g.fillRoundedRectangle (h, 3);
        auto yOf = [&] (float l) { return juce::jmap (juce::jlimit (-30.0f, 0.0f, l), -30.0f, 0.0f, h.getBottom() - 2, h.getY() + 2); };
        g.setColour (C::amber.withAlpha (0.6f)); g.drawHorizontalLine ((int) yOf (tgt), h.getX(), h.getRight());
        juce::Path p; bool started = false;
        for (size_t i = 0; i < histShort.size(); ++i)
        {
            if (histShort[i] < -60) { started = false; continue; }
            const float x = h.getX() + h.getWidth() * (float) i / 300.0f, y = yOf (histShort[i]);
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (C::cyan); g.strokePath (p, juce::PathStrokeType (1.4f));
        g.setColour (C::dim); g.setFont (mono (9.0f)); g.drawText ("short-term, last 10 s", h.reduced (5, 3), juce::Justification::bottomLeft);
    }
    a.removeFromTop (8);
    // streaming penalties
    g.setColour (C::dim); g.setFont (font (10.0f)); g.drawText ("STREAMING TURN-DOWN", a.removeFromTop (16), juce::Justification::centredLeft);
    for (auto& pn : penalties())
    {
        auto rr = a.removeFromTop (17);
        const float d = I > -70 ? std::min (0.0f, pn.target - I) : 0.0f;
        g.setColour (C::text.withAlpha (0.8f)); g.setFont (font (10.5f, false)); g.drawText (pn.name, rr, juce::Justification::centredLeft);
        auto bar = rr.withTrimmedLeft (70).withTrimmedRight (64).reduced (0, 5);
        g.setColour (C::field); g.fillRoundedRectangle (bar, 2);
        g.setColour (d < -6 ? C::amber : C::green); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, -d / 12.0f)), 2);
        g.setColour (C::dim); g.setFont (mono (10.0f)); g.drawText (I > -70 ? juce::String (d, 1) + " dB" : juce::String ("--"), rr, juce::Justification::centredRight);
    }
    a.removeFromTop (8);
    // level bars + correlation
    auto lv = a.removeFromTop (56);
    ledMeter (g, lv.removeFromTop (8), pkIn, pkIn, false, 0); lv.removeFromTop (3);
    ledMeter (g, lv.removeFromTop (8), pkOutL, pkOutL, false, 0); lv.removeFromTop (3);
    ledMeter (g, lv.removeFromTop (8), pkOutR, pkOutR, false, 0);
    g.setColour (C::dim); g.setFont (mono (9.0f)); g.drawText ("IN   OUT L   OUT R", lv.withHeight (14), juce::Justification::centredLeft);
    {
        auto cr = juce::Rectangle<float> (a.getX(), a.getY() + 18, a.getWidth(), 12);
        const float c = proc.meters.corr.load();
        g.setColour (C::field); g.fillRoundedRectangle (cr, 3);
        const float x = cr.getX() + (c + 1.0f) * 0.5f * cr.getWidth();
        g.setColour (C::border); g.drawVerticalLine ((int) cr.getCentreX(), cr.getY(), cr.getBottom());
        g.setColour (c < 0 ? C::red : c < 0.3f ? C::amber : C::green); g.fillRoundedRectangle (juce::Rectangle<float> (x - 3, cr.getY(), 6, cr.getHeight()), 2);
        g.setColour (C::dim); g.setFont (mono (9.0f));
        g.drawText ("-1          PHASE CORRELATION          +1", cr.translated (0, 14), juce::Justification::centred);
        g.drawText ("latency " + juce::String (juce::roundToInt (proc.latencySamples() / proc.sampleRate() * 1000.0)) + " ms   " + juce::String (proc.sampleRate() / 1000.0, 1) + " kHz"
                    + (pv (IDs::lowLat) > 0.5f ? "   LOW LATENCY" : ""), cr.translated (0, 34), juce::Justification::centredLeft);
    }
    if (pv (IDs::bypass) > 0.5f) { g.setColour (C::red); g.drawText ("BYPASS " + (pv (IDs::match) > 0.5f ? "(matched " + juce::String (proc.meters.matchDb.load(), 1) + " dB)" : juce::String()), lv.withHeight (14), juce::Justification::centredRight); }
    else if (pv (IDs::mono) > 0.5f) { g.setColour (C::violet); g.drawText ("MONO CHECK ON", lv.withHeight (14), juce::Justification::centredRight); }
}
