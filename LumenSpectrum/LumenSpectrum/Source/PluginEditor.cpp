#include "PluginEditor.h"

using namespace lumen::spec;
using namespace lumen::spec::ui;

namespace
{
enum View { Bars = 0, Curves, Ownership, Masking, Sonogram };
const char* viewNames[] { "BARS", "CURVES", "OWNERSHIP", "MASKING", "SONOGRAM" };
const char* viewTips[] {
    "Band bars coloured by frequency region, with the whole mix as a ghost behind - the classic 'what you hear' view",
    "Every instrument as its own coloured curve - see where they sit on top of each other",
    "Who owns each frequency: the share of energy each instrument has, stacked to 100%",
    "Clash finder: where two instruments fight for the same frequencies, ranked, with a fix for each",
    "Dominance sonogram: time scrolls left, colour = the loudest instrument at that frequency"
};
constexpr int kHeaderH = 104, kStripH = 74, kLegendH = 34, kSide = 16;

void fillCombo (juce::ComboBox& b, juce::AudioProcessorValueTreeState& st, const char* id)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (st.getParameter (id)))
        b.addItemList (p->choices, 1);
}
}

// ============================================================================ construction
LumenSpectrumEditor::LumenSpectrumEditor (LumenSpectrumProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    tracks.reserve (kMaxSlots);
    auto& st = proc.apvts;

    nameLabel.setEditable (false, true, false);
    nameLabel.setFont (font (15.0f, true));
    nameLabel.setTooltip ("Track name - taken from your DAW automatically. Double-click to rename.");
    nameLabel.onTextChange = [this] { proc.setTrackName (nameLabel.getText(), true); };
    addAndMakeVisible (nameLabel);

    struct CB { juce::ComboBox* box; const char* id; const char* tip; };
    const CB combos[] {
        { &roleBox,   ids::role,      "Instrument role: sets the colour and how masking fixes are suggested (auto-detected from the track name)" },
        { &colourBox, ids::colour,    "Curve colour: by role, your DAW track colour, or a custom colour" },
        { &targetBox, ids::target,    "Tonal-balance guide for the band strip. 'Snapshot' compares against your SNAP REF capture (e.g. a reference track)" },
        { &resBox,    ids::barRes,    "Bar width: 1/3, 1/6 or 1/12 octave" },
        { &barColBox, ids::barColour, "Colour bars by frequency band (as in the strip below) or by this instrument's colour" },
        { &fftBox,    ids::fft,       "FFT size (session-wide): bigger = sharper low end, slower response" },
        { &avgBox,    ids::avg,       "Averaging (session-wide): Fast for transients, Slow / Long for tonal balance" },
        { &chanBox,   ids::chan,      "What is analysed (session-wide): stereo average, mid, side, left or right" },
        { &slopeBox,  ids::slope,     "Display tilt. 4.5 dB/oct makes a balanced mix look flat" },
        { &rangeBox,  ids::range,     "Vertical range of the display" },
        { &smoothBox, ids::smooth,    "Spectral smoothing for curves, ownership and masking: 1/12 octave reads like Pro-Q, 1/3 octave shows broad tonal balance" },
    };
    for (auto& c : combos)
    {
        fillCombo (*c.box, st, c.id);
        c.box->setTooltip (c.tip);
        addAndMakeVisible (*c.box);
        comboAtt.push_back (std::make_unique<APVTS::ComboBoxAttachment> (st, c.id, *c.box));
    }

    struct TB { juce::TextButton* b; const char* id; const char* tip; };
    const TB toggles[] {
        { &peakBtn,   ids::peak,   "Peak-hold line" },
        { &othersBtn, ids::others, "Show the other Lumen Spectrum instances in this session" },
    };
    for (auto& t : toggles)
    {
        t.b->setClickingTogglesState (true);
        t.b->setTooltip (t.tip);
        addAndMakeVisible (*t.b);
        btnAtt.push_back (std::make_unique<APVTS::ButtonAttachment> (st, t.id, *t.b));
    }

    freezeBtn.setClickingTogglesState (true);
    freezeBtn.setColour (juce::TextButton::buttonOnColourId, C::warn);
    freezeBtn.setTooltip ("Freeze the display to study it");
    freezeBtn.onClick = [this] { frozen = freezeBtn.getToggleState(); };
    addAndMakeVisible (freezeBtn);

    snapBtn.setTooltip ("Capture this track's current spectrum as a reference: shown dashed, and usable as the target. Play a reference track through this instance, then press.");
    snapBtn.onClick = [this]
    {
        proc.captureSnapshot();
        if (auto* tp = proc.apvts.getParameter (ids::target))
            tp->setValueNotifyingHost (tp->convertTo0to1 ((float) (kNumGenres + 1)));
    };
    addAndMakeVisible (snapBtn);
    clearBtn.setTooltip ("Clear the snapshot");
    clearBtn.onClick = [this] { proc.clearSnapshot(); repaint(); };
    addAndMakeVisible (clearBtn);

    for (int i = 0; i < 5; ++i)
    {
        viewBtn[i].setButtonText (viewNames[i]);
        viewBtn[i].setTooltip (viewTips[i]);
        viewBtn[i].onClick = [this, i]
        {
            if (auto* vp = proc.apvts.getParameter (ids::view))
                vp->setValueNotifyingHost (vp->convertTo0to1 ((float) i));
            syncControls();
            resized();
        };
        addAndMakeVisible (viewBtn[i]);
    }

    const int w0 = proc.editorW, h0 = proc.editorH;   // read before the limits trigger resized()
    setResizable (true, true);
    setResizeLimits (1180, 640, 2400, 1500);
    setSize (w0, h0);
    syncControls();
    startTimerHz (30);
}

LumenSpectrumEditor::~LumenSpectrumEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LumenSpectrumEditor::syncControls()
{
    const int v = view();
    for (int i = 0; i < 5; ++i) viewBtn[i].setToggleState (i == v, juce::dontSendNotification);
    resBox.setVisible (v == Bars);
    barColBox.setVisible (v == Bars);
    smoothBox.setVisible (v != Bars);
    if (! nameLabel.isBeingEdited())
    {
        auto n = proc.getTrackName();
        nameLabel.setText (n.isNotEmpty() ? n : juce::String ("Untitled track"), juce::dontSendNotification);
    }
}

// ============================================================================ layout
void LumenSpectrumEditor::resized()
{
    proc.editorW = getWidth(); proc.editorH = getHeight();
    auto r = getLocalBounds();
    layoutHeader (r.removeFromTop (kHeaderH).reduced (kSide, 0));
    r.reduce (kSide, 0);
    legendArea = r.removeFromBottom (kLegendH + 6).withTrimmedTop (6);
    stripArea  = r.removeFromBottom (kStripH + 8).withTrimmedTop (8);
    displayArea = r;

    auto pr = displayArea;
    if (view() == Masking) listArea = pr.removeFromRight (310).reduced (8, 10); else listArea = {};
    plot = pr.withTrimmedLeft (46).withTrimmedRight (16).withTrimmedTop (16).withTrimmedBottom (26);
    // strip boxes align to the plot, so the plot's x-span is shared with the strip
    stripArea = stripArea.withX (plot.getX()).withWidth (plot.getWidth());
}

void LumenSpectrumEditor::layoutHeader (juce::Rectangle<int> h)
{
    auto row1 = h.withHeight (30).withY (12);
    row1.removeFromLeft (176);                           // brand
    nameLabel.setBounds (row1.removeFromLeft (170));    // colour dot sits just left of it
    row1.removeFromLeft (8);
    roleBox.setBounds (row1.removeFromLeft (140));
    row1.removeFromLeft (6);
    colourBox.setBounds (row1.removeFromLeft (130));
    clearBtn.setBounds (row1.removeFromRight (28));
    row1.removeFromRight (4);
    snapBtn.setBounds (row1.removeFromRight (84));
    row1.removeFromRight (6);
    targetBox.setBounds (row1.removeFromRight (160));
    row1.removeFromRight (12);
    freezeBtn.setBounds (row1.removeFromRight (70));
    row1.removeFromRight (6);
    othersBtn.setBounds (row1.removeFromRight (70));
    row1.removeFromRight (6);
    peakBtn.setBounds (row1.removeFromRight (56));

    auto row2 = h.withHeight (26).withY (66);
    const int tabW[] { 70, 76, 96, 82, 92 };
    for (int i = 0; i < 5; ++i) { viewBtn[i].setBounds (row2.removeFromLeft (tabW[i])); row2.removeFromLeft (4); }
    rangeBox.setBounds (row2.removeFromRight (72)); row2.removeFromRight (6);
    slopeBox.setBounds (row2.removeFromRight (92)); row2.removeFromRight (6);
    chanBox.setBounds (row2.removeFromRight (78));  row2.removeFromRight (6);
    avgBox.setBounds (row2.removeFromRight (80));   row2.removeFromRight (6);
    fftBox.setBounds (row2.removeFromRight (70));   row2.removeFromRight (6);
    auto slot = row2.removeFromRight (232);           // Bars: resolution + bar colours, other views: smoothing
    smoothBox.setBounds (slot.withTrimmedLeft (slot.getWidth() - 110));
    barColBox.setBounds (slot.removeFromRight (130));
    slot.removeFromRight (6);
    resBox.setBounds (slot);
}

// ============================================================================ geometry
int LumenSpectrumEditor::view() const { return juce::jlimit (0, 4, choice (ids::view)); }
float LumenSpectrumEditor::xOfFreq (float f) const
{
    return (float) plot.getX() + (float) plot.getWidth() * std::log (f / kFMin) / std::log (kFMax / kFMin);
}
float LumenSpectrumEditor::freqOfX (float x) const
{
    return kFMin * std::pow (kFMax / kFMin, (x - (float) plot.getX()) / (float) plot.getWidth());
}
float LumenSpectrumEditor::tilt (float f) const
{
    static const float slopes[] { 0.0f, 3.0f, 4.5f };
    return slopes[juce::jlimit (0, 2, choice (ids::slope))] * std::log2 (f / 1000.0f);
}
float LumenSpectrumEditor::rangeDb() const
{
    static const float r[] { 48.0f, 72.0f, 96.0f, 120.0f };
    return r[juce::jlimit (0, 3, choice (ids::range))];
}
float LumenSpectrumEditor::topDb() const { return displayTop; }
float LumenSpectrumEditor::yOfDb (float db) const
{
    return (float) plot.getY() + (topDb() - db) / rangeDb() * (float) plot.getHeight();
}

// ============================================================================ data
void LumenSpectrumEditor::timerCallback()
{
    syncControls();
    if (! frozen)
    {
        gather();
        computeDerived();
    }
    repaint (displayArea.getUnion (stripArea).getUnion (legendArea).expanded (4));
}

void LumenSpectrumEditor::gather()
{
    auto& hub = *proc.hub;
    const bool showOthers = param (ids::others);
    const auto now = juce::Time::currentTimeMillis();
    const int me = proc.getSlotIndex();
    tracks.clear();
    selfIdx = masterIdx = -1;
    std::set<int> alive;
    static const int radii[] { 0, 1, 2, 4, 8 };   // points per half-window (48 points per octave)
    const int smoothR = view() == Bars ? 0 : radii[juce::jlimit (0, 4, choice (ids::smooth))];

    for (int i = 0; i < kMaxSlots; ++i)
    {
        auto& s = hub.slot (i);
        if (! s.used.load()) continue;
        const bool self = i == me;
        if (! self && ! showOthers) continue;
        tracks.emplace_back();
        auto& t = tracks.back();
        t.slot = i; t.uid = s.uid.load(); t.self = self;
        t.role = juce::jlimit (0, kNumRoles - 1, s.role.load());
        t.col = juce::Colour (s.argb.load());
        t.name = s.getName();
        t.idle = now - s.lastSoundMs.load() > 2000;
        t.bus = roles()[(size_t) t.role].isBus;
        t.master = t.role == kRoleMaster;
        t.visible = self || hidden.count (t.uid) == 0;
        auto it = peakStore.find (t.uid);
        if (it == peakStore.end()) { it = peakStore.emplace (t.uid, std::array<float, P> {}).first; it->second.fill (-140.0f); }
        for (int k = 0; k < P; ++k)
        {
            t.db[k] = s.spec[k].load (std::memory_order_relaxed);
            it->second[(size_t) k] = juce::jmax (t.db[k], it->second[(size_t) k] - 0.35f);
            t.pk[k] = it->second[(size_t) k];
        }
        if (smoothR > 0) { smoothSpectrum (t.db, smoothR); smoothSpectrum (t.pk, smoothR); }
        alive.insert (t.uid);
        if (self) selfIdx = (int) tracks.size() - 1;
        if (t.master && t.visible && masterIdx < 0) masterIdx = (int) tracks.size() - 1;
    }
    for (auto it = peakStore.begin(); it != peakStore.end();)
        it = alive.count (it->first) ? std::next (it) : peakStore.erase (it);

    // meters from this instance's own audio
    if (me >= 0)
    {
        auto& s = hub.slot (me);
        const int wp = s.writePos.load (std::memory_order_acquire);
        constexpr int n = 4096;
        double ll = 0, rr = 0, lr = 0; float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const int idx = (wp - n + i) & (kRing - 1);
            const float l = s.ring[0][idx], r = s.ring[1][idx];
            ll += (double) l * l; rr += (double) r * r; lr += (double) l * r;
            pk = juce::jmax (pk, std::abs (l), std::abs (r));
        }
        corr = ll * rr > 1e-18 ? (float) (lr / std::sqrt (ll * rr)) : 1.0f;
        rmsDb = 10.0f * std::log10 ((float) ((ll + rr) / (2.0 * n)) + 1e-14f);
        const float pdb = juce::Decibels::gainToDecibels (pk, -120.0f);
        peakDb = pdb > peakDb ? pdb : peakDb - 0.4f;
    }
}

void LumenSpectrumEditor::smoothSpectrum (float* db, int radius)
{
    float pw[P], cs[P + 1];
    for (int k = 0; k < P; ++k) pw[k] = dbToPow (db[k]);
    cs[0] = 0; for (int k = 0; k < P; ++k) cs[k + 1] = cs[k] + pw[k];   // power average over a fractional-octave window
    for (int k = 0; k < P; ++k)
    {
        const int a = juce::jmax (0, k - radius), b = juce::jmin (P - 1, k + radius);
        db[k] = powToDb ((cs[b + 1] - cs[a]) / (float) (b - a + 1));
    }
}

void LumenSpectrumEditor::computeDerived()
{
    // the mix = sum of all visible instruments (busses excluded so nothing counts twice)
    numInstruments = 0;
    std::fill (std::begin (mixPow), std::end (mixPow), 0.0f);
    for (auto& t : tracks)
        if (t.visible && ! t.bus)
        {
            ++numInstruments;
            for (int k = 0; k < P; ++k) mixPow[k] += dbToPow (t.db[k]);
        }
    if (numInstruments == 0)
    {
        const Track* src = masterIdx >= 0 ? &tracks[(size_t) masterIdx] : selfTrack();
        if (src != nullptr) for (int k = 0; k < P; ++k) mixPow[k] = dbToPow (src->db[k]);
    }
    for (int k = 0; k < P; ++k) mixDb[k] = powToDb (mixPow[k]);
    haveMix = numInstruments > 0 || masterIdx >= 0;

    // ---- auto top of the display (fast up, slow down, 6 dB steps)
    float mx = -200.0f;
    for (auto& t : tracks)
        if (t.visible)
            for (int k = 0; k < P; k += 2) mx = juce::jmax (mx, t.db[k] + tilt (pointFreq ((float) k)));
    for (int k = 0; k < P; k += 2) mx = juce::jmax (mx, mixDb[k] + tilt (pointFreq ((float) k)));
    const float want = juce::jlimit (-60.0f, 24.0f, std::ceil ((mx + 4.0f) / 6.0f) * 6.0f);
    displayTop = want > displayTop ? want : juce::jmax (want, displayTop - 0.25f);

    // ---- masking: pairs of instruments that are both loud where the mix is loud
    clashes.clear();
    std::fill (std::begin (heat), std::end (heat), 0.0f);
    std::vector<int> inst;
    for (int i = 0; i < (int) tracks.size(); ++i)
        if (tracks[(size_t) i].visible && ! tracks[(size_t) i].bus && ! tracks[(size_t) i].idle) inst.push_back (i);
    float c[P];
    for (size_t ia = 0; ia < inst.size(); ++ia)
        for (size_t ib = ia + 1; ib < inst.size(); ++ib)
        {
            auto& A = tracks[(size_t) inst[ia]];
            auto& B = tracks[(size_t) inst[ib]];
            for (int k = 0; k < P; ++k)
            {
                const float f = pointFreq ((float) k);
                if (f < 25.0f || f > 16000.0f || mixDb[k] < -85.0f) { c[k] = 0; continue; }
                const float w = juce::jmin (A.db[k], B.db[k]) - (mixDb[k] - 10.0f);
                c[k] = juce::jlimit (0.0f, 1.0f, w / 7.0f);
                heat[k] = juce::jmax (heat[k], c[k]);
            }
            int k = 0;
            while (k < P)
            {
                if (c[k] < 0.3f) { ++k; continue; }
                Clash cl; cl.a = inst[ia]; cl.b = inst[ib]; cl.p0 = k;
                float sum = 0, best = -1; int n = 0;
                while (k < P && c[k] >= 0.3f)
                {
                    const float f = pointFreq ((float) k);
                    const float wf = (f >= 40.0f && f <= 300.0f) ? 1.5f : (f >= 2000.0f && f <= 5000.0f) ? 1.2f : 1.0f;
                    cl.score += c[k] * wf; sum += c[k]; ++n;
                    if (c[k] > best) { best = c[k]; cl.pPeak = k; }
                    ++k;
                }
                cl.p1 = k - 1;
                cl.mean = sum / (float) n;
                if (n >= 4) clashes.push_back (cl);
            }
        }
    std::sort (clashes.begin(), clashes.end(), [] (const Clash& x, const Clash& y) { return x.score > y.score; });
    {
        std::vector<Clash> one;   // keep the worst region of each pair
        for (auto& cl : clashes)
            if (std::none_of (one.begin(), one.end(), [&] (const Clash& o) { return o.a == cl.a && o.b == cl.b; })) one.push_back (cl);
        clashes = std::move (one);
    }
    if (clashes.size() > 6) clashes.resize (6);

    // ---- dominance sonogram column
    const int sw = 640;
    if (! sono.isValid()) sono = juce::Image (juce::Image::RGB, sw, P, true);
    sono.moveImageSection (0, 0, 1, 0, sw - 1, P);
    {
        juce::Image::BitmapData bd (sono, sw - 1, 0, 1, P, juce::Image::BitmapData::writeOnly);
        const float bottom = topDb() - rangeDb();
        for (int k = 0; k < P; ++k)
        {
            const Track* dom = nullptr; float best = -1e9f;
            for (auto& t : tracks)
                if (t.visible && (numInstruments == 0 || ! t.bus) && t.db[k] > best) { best = t.db[k]; dom = &t; }
            const float lvl = mixDb[k] + tilt (pointFreq ((float) k));
            float b = juce::jlimit (0.0f, 1.0f, (lvl - bottom) / rangeDb());
            b = std::pow (b, 1.6f);
            const auto col = dom != nullptr ? dom->col : C::accent;
            bd.setPixelColour (0, P - 1 - k, juce::Colour::fromFloatRGBA (col.getFloatRed() * b, col.getFloatGreen() * b, col.getFloatBlue() * b, 1.0f));
        }
    }
}

juce::String LumenSpectrumEditor::clashAdvice (const Clash& c) const
{
    const auto& A = tracks[(size_t) c.a];
    const auto& B = tracks[(size_t) c.b];
    const bool aWins = roles()[(size_t) A.role].priority <= roles()[(size_t) B.role].priority;
    const auto& win = aWins ? A : B;
    const auto& lose = aWins ? B : A;
    const float f = pointFreq ((float) c.pPeak);
    const float bw = juce::jmax (1.0f, pointFreq ((float) c.p1) - pointFreq ((float) c.p0));
    const float q = juce::jlimit (0.7f, 8.0f, f / bw);
    const juce::String cut = c.mean > 0.75f ? "3-5 dB" : c.mean > 0.5f ? "2-3 dB" : "1-2 dB";
    if (f < 160.0f && (win.role == 0 || lose.role == 0))
        return "Sidechain " + lose.name + " to " + win.name + ", or cut it " + cut + " at " + freqText (f);
    juce::String s = "Cut " + lose.name + " " + cut + " at " + freqText (f) + " (Q " + juce::String (q, 1) + ")";
    if (f >= 300.0f) s << ", or pan them apart";
    return s;
}

// ============================================================================ painting
void LumenSpectrumEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);

    // brand
    g.setColour (C::dim);
    g.setFont (font (12.0f, true));
    g.drawText ("L U M E N", kSide, 10, 160, 16, juce::Justification::left);
    g.setColour (C::text);
    g.setFont (font (20.0f, true));
    g.drawText ("SPECTRUM", kSide, 24, 160, 22, juce::Justification::left);
    if (auto* s = selfTrack())
    {
        g.setColour (s->col);
        g.fillEllipse ((float) nameLabel.getX() - 12.0f, (float) nameLabel.getBounds().getCentreY() - 5.0f, 10.0f, 10.0f);
    }
    for (auto* c : { (juce::Component*) &smoothBox, (juce::Component*) &rangeBox, (juce::Component*) &slopeBox, (juce::Component*) &chanBox, (juce::Component*) &avgBox,
                     (juce::Component*) &fftBox, (juce::Component*) &barColBox, (juce::Component*) &resBox })
        if (c->isVisible())
        {
            const auto* nm = c == &smoothBox ? "Smoothing" : c == &rangeBox ? "Range" : c == &slopeBox ? "Slope" : c == &chanBox ? "Channel" : c == &avgBox ? "Averaging"
                           : c == &fftBox ? "FFT" : c == &barColBox ? "Bar colours" : "Resolution";
            caption (g, *c, nm);
        }
    caption (g, viewBtn[0], "View");

    // display panel
    g.setColour (C::panel);
    g.fillRoundedRectangle (displayArea.toFloat(), 12.0f);
    g.setColour (C::line);
    g.drawRoundedRectangle (displayArea.toFloat().reduced (0.5f), 12.0f, 1.0f);

    if (selfTrack() == nullptr)
    {
        drawEmptyHint (g, "More than 32 Lumen Spectrum instances are open - close one to analyse this track.");
        return;
    }

    switch (view())
    {
        case Bars:      drawBars (g); break;
        case Curves:    drawCurves (g, false); break;
        case Ownership: drawOwnership (g); break;
        case Masking:   drawMasking (g); break;
        case Sonogram:  drawSonogram (g); break;
        default: break;
    }
    drawHover (g);
    drawBandStrip (g);
    drawLegend (g);

    if (frozen)
    {
        g.setColour (C::warn);
        g.setFont (font (11.0f, true));
        g.drawText ("FROZEN", plot.getRight() - 70, plot.getY() + 4, 66, 14, juce::Justification::right);
    }
}

void LumenSpectrumEditor::drawEmptyHint (juce::Graphics& g, const juce::String& text)
{
    g.setColour (C::dim);
    g.setFont (font (13.5f));
    g.drawFittedText (text, plot.reduced (40), juce::Justification::centred, 3);
}

void LumenSpectrumEditor::drawGrid (juce::Graphics& g, bool dbAxis, bool percentAxis)
{
    // faint region tint + dashed region borders (as in the band strip)
    for (auto& r : regions())
    {
        const float x0 = xOfFreq (r.lo), x1 = xOfFreq (r.hi);
        g.setColour (juce::Colour (r.argb).withAlpha (0.035f));
        g.fillRect (juce::Rectangle<float> (x0, (float) plot.getY(), x1 - x0, (float) plot.getHeight()));
    }
    const float dash[] { 3.0f, 4.0f };
    for (int i = 1; i < kNumRegions; ++i)
    {
        const float x = xOfFreq (regions()[(size_t) i].lo);
        g.setColour (C::faint.withAlpha (0.55f));
        g.drawDashedLine (juce::Line<float> (x, (float) plot.getY(), x, (float) plot.getBottom()), dash, 2, 1.0f);
    }
    // frequency labels
    g.setFont (font (10.5f, true));
    for (float f : { 20.0f, 30.0f, 40.0f, 60.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 6000.0f, 10000.0f, 20000.0f })
    {
        const float x = xOfFreq (f);
        g.setColour (C::grid);
        if (f > 20.0f && f < 20000.0f) g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());
        g.setColour (C::dim);
        const auto s = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
        auto just = f <= 20.0f ? juce::Justification::left : f >= 20000.0f ? juce::Justification::right : juce::Justification::centred;
        const float lx = f <= 20.0f ? x : f >= 20000.0f ? x - 40.0f : x - 20.0f;
        g.drawText (s, juce::Rectangle<float> (lx, (float) plot.getBottom() + 6.0f, 40.0f, 14.0f), just);
    }
    if (dbAxis)
    {
        const float bottom = topDb() - rangeDb();
        const float step = rangeDb() > 72.0f ? 24.0f : 12.0f;
        for (float d = topDb(); d >= bottom - 0.1f; d -= step)
        {
            const float y = yOfDb (d);
            g.setColour (C::grid);
            g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
            g.setColour (C::faint);
            g.drawText (juce::String ((int) d), plot.getX() - 44, (int) y - 7, 38, 14, juce::Justification::right);
        }
    }
    if (percentAxis)
        for (int pc = 0; pc <= 100; pc += 25)
        {
            const float y = (float) plot.getBottom() - (float) plot.getHeight() * (float) pc / 100.0f;
            g.setColour (C::grid);
            g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
            g.setColour (C::faint);
            g.drawText (juce::String (pc) + "%", plot.getX() - 44, (int) y - 7, 38, 14, juce::Justification::right);
        }
}

// ---------------------------------------------------------------------------- BARS
void LumenSpectrumEditor::drawBars (juce::Graphics& g)
{
    drawGrid (g, true, false);
    const auto& me = *selfTrack();
    const int per = choice (ids::barRes) == 0 ? 3 : choice (ids::barRes) == 1 ? 6 : 12;
    const bool byBand = choice (ids::barColour) == 0;
    const bool showPeak = param (ids::peak);
    const bool snap = proc.hasSnapshot();
    const auto& sn = proc.getSnapshot();

    // ghost = the whole mix (master instance if there is one, else the sum of all instruments)
    const float* ghost = nullptr;
    float ghostBuf[P];
    if (masterIdx >= 0 && ! me.master) ghost = tracks[(size_t) masterIdx].db;
    else if (numInstruments > 1 || (numInstruments == 1 && me.bus)) ghost = mixDb;
    if (ghost != nullptr) std::copy (ghost, ghost + P, ghostBuf);

    auto bandValue = [] (const float* db, float lo, float hi)
    {
        const int k0 = juce::jlimit (0, P - 1, (int) std::ceil (freqToPoint (lo)));
        const int k1 = juce::jlimit (k0, P - 1, (int) std::floor (freqToPoint (hi)));
        float s = 0; int n = 0;
        for (int k = k0; k <= k1; ++k) { s += dbToPow (db[k]); ++n; }
        return powToDb (s / (float) juce::jmax (1, n));
    };

    const float bottomY = (float) plot.getBottom();
    const int nBars = per * 10;   // 20 Hz .. 20.48 kHz
    for (int i = 0; i < nBars; ++i)
    {
        const float fc = kFMin * std::pow (2.0f, ((float) i + 0.5f) / (float) per);
        if (fc > kFMax) break;
        const float lo = kFMin * std::pow (2.0f, (float) i / (float) per);
        const float hi = juce::jmin (kFMax, kFMin * std::pow (2.0f, (float) (i + 1) / (float) per));
        const float x0 = xOfFreq (lo), x1 = xOfFreq (hi);
        const float gap = juce::jmax (1.0f, (x1 - x0) * 0.22f);
        auto col = byBand ? spectrumColour (fc) : me.col;
        const float tl = tilt (fc);
        const float radius = juce::jmin (4.0f, (x1 - x0 - gap) * 0.3f);

        if (ghost != nullptr)
        {
            const float yg = juce::jlimit ((float) plot.getY(), bottomY, yOfDb (bandValue (ghostBuf, lo, hi) + tl));
            g.setColour (juce::Colour (0xff232836));
            g.fillRoundedRectangle (juce::Rectangle<float> (x0 + gap * 0.5f, yg, x1 - x0 - gap, bottomY - yg + radius), radius);
        }
        const float y = juce::jlimit ((float) plot.getY(), bottomY, yOfDb (bandValue (me.db, lo, hi) + tl));
        if (y < bottomY - 0.5f)
        {
            auto rect = juce::Rectangle<float> (x0 + gap * 0.5f, y, x1 - x0 - gap, bottomY - y);
            juce::ColourGradient grad (col.brighter (0.12f), 0, y, col.withMultipliedAlpha (0.72f), 0, bottomY, false);
            g.setGradientFill (grad);
            juce::Path bar;
            bar.addRoundedRectangle (rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight() + radius, radius, radius, true, true, false, false);
            g.saveState();
            g.reduceClipRegion (plot);
            g.fillPath (bar);
            g.restoreState();
        }
        if (showPeak)
        {
            const float yp = juce::jlimit ((float) plot.getY(), bottomY, yOfDb (bandValue (me.pk, lo, hi) + tl));
            if (yp < bottomY - 1.0f)
            {
                g.setColour (col.brighter (0.4f).withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (x0 + gap * 0.5f, yp - 1.0f, x1 - x0 - gap, 2.0f));
            }
        }
        if (snap)
        {
            const float ys = juce::jlimit ((float) plot.getY(), bottomY, yOfDb (bandValue (sn.data(), lo, hi) + tl));
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawHorizontalLine ((int) ys, x0 + gap * 0.5f, x1 - gap * 0.5f);
        }
    }

    // legend like the original: whole mix / what you hear
    g.setFont (font (11.0f));
    auto lg = juce::Rectangle<int> (plot.getX() + 8, plot.getY() + 4, 420, 14);
    if (ghost != nullptr)
    {
        g.setColour (juce::Colour (0xff2c3242)); g.fillRoundedRectangle (lg.removeFromLeft (14).toFloat().reduced (1), 3.0f);
        g.setColour (C::dim); g.drawText ("Whole mix", lg.removeFromLeft (80).withTrimmedLeft (6), juce::Justification::left);
    }
    for (int i = 0; i < kNumRegions; ++i) { g.setColour (juce::Colour (regions()[(size_t) i].argb)); g.fillRect (lg.removeFromLeft (5).toFloat().reduced (0, 1)); }
    g.setColour (C::dim); g.drawText (me.name, lg.withTrimmedLeft (6), juce::Justification::left);
}

// ---------------------------------------------------------------------------- CURVES
void LumenSpectrumEditor::drawCurves (juce::Graphics& g, bool maskingStyle)
{
    drawGrid (g, true, false);
    const float bottomY = (float) plot.getBottom();
    g.saveState();
    g.reduceClipRegion (plot);

    auto makePath = [&] (const float* db, bool closed)
    {
        juce::Path p;
        for (int k = 0; k < P; ++k)
        {
            const float x = xOfPoint (k);
            const float y = juce::jmin (bottomY + 2.0f, yOfDb (db[k] + tilt (pointFreq ((float) k))));
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (xOfPoint (P - 1), bottomY + 2.0f); p.lineTo (xOfPoint (0), bottomY + 2.0f); p.closeSubPath(); }
        return p;
    };

    // draw order: others (fills), master, self on top
    std::vector<const Track*> order;
    for (auto& t : tracks) if (t.visible && ! t.self) order.push_back (&t);
    if (! maskingStyle || ! selfTrack()->bus) order.push_back (selfTrack());

    for (auto* t : order)
    {
        if (maskingStyle && t->bus) continue;
        const bool self = t->self && ! maskingStyle;
        const float a = t->idle ? 0.35f : 1.0f;
        if (! t->master)
        {
            auto fill = makePath (t->db, true);
            const float alphaTop = (self ? 0.30f : 0.16f) * a;
            g.setGradientFill (juce::ColourGradient (t->col.withAlpha (alphaTop), 0, (float) plot.getY(), t->col.withAlpha (0.02f), 0, bottomY, false));
            g.fillPath (fill);
        }
        auto line = makePath (t->db, false);
        if (self)
        {
            g.setColour (t->col.withAlpha (0.18f));
            g.strokePath (line, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (t->master ? juce::Colours::white.withAlpha (0.8f * a) : t->col.withAlpha ((self ? 1.0f : 0.85f) * a));
        g.strokePath (line, juce::PathStrokeType (self ? 2.2f : t->master ? 1.2f : 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (self && param (ids::peak))
        {
            g.setColour (t->col.withAlpha (0.45f));
            g.strokePath (makePath (t->pk, false), juce::PathStrokeType (1.0f));
        }
    }
    if (proc.hasSnapshot() && ! maskingStyle)
    {
        juce::Path dashed;
        const float dl[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, makePath (proc.getSnapshot().data(), false), dl, 2);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillPath (dashed);
    }
    g.restoreState();
}

// ---------------------------------------------------------------------------- OWNERSHIP
void LumenSpectrumEditor::drawOwnership (juce::Graphics& g)
{
    drawGrid (g, false, true);
    std::vector<const Track*> inst;
    for (auto& t : tracks) if (t.visible && ! t.bus) inst.push_back (&t);
    if (inst.size() < 2)
    {
        drawEmptyHint (g, "Ownership needs two or more instruments.\nPut Lumen Spectrum on your kick, bass, synths, vocal... - each instance appears here in its own colour.");
        return;
    }
    const float h = (float) plot.getHeight(), y0 = (float) plot.getBottom();
    std::vector<float> lower ((size_t) P, 0.0f), upper ((size_t) P, 0.0f);
    std::vector<std::pair<float, int>> best (inst.size(), { 0.0f, 0 });
    for (size_t i = 0; i < inst.size(); ++i)
    {
        for (int k = 0; k < P; ++k)
        {
            const float share = mixPow[k] > 1e-13f ? dbToPow (inst[i]->db[k]) / mixPow[k] : 0.0f;
            upper[(size_t) k] = lower[(size_t) k] + share;
            if (share > best[i].first && k % 4 == 0) best[i] = { share, k };
        }
        juce::Path p;
        for (int k = 0; k < P; ++k)
        {
            const float x = xOfPoint (k), y = y0 - h * juce::jlimit (0.0f, 1.0f, upper[(size_t) k]);
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        for (int k = P - 1; k >= 0; --k) p.lineTo (xOfPoint (k), y0 - h * juce::jlimit (0.0f, 1.0f, lower[(size_t) k]));
        p.closeSubPath();
        g.setColour (inst[i]->col.withAlpha (inst[i]->self ? 0.95f : 0.78f));
        g.fillPath (p);
        g.setColour (C::panel.withAlpha (0.6f));
        g.strokePath (p, juce::PathStrokeType (0.8f));
        // remember the label spot: middle of its band at its strongest point
        best[i].first = best[i].first > 0.22f ? (lower[(size_t) best[i].second] + upper[(size_t) best[i].second]) * 0.5f : -1.0f;
        std::swap (lower, upper);
    }
    // darken frequencies where the mix is quiet (ownership of silence is meaningless)
    const float bottom = topDb() - rangeDb();
    for (int k = 0; k < P - 1; ++k)
    {
        const float lvl = mixDb[k] + tilt (pointFreq ((float) k));
        const float dark = 1.0f - juce::jlimit (0.0f, 1.0f, (lvl - bottom) / (rangeDb() * 0.45f));
        if (dark > 0.02f)
        {
            g.setColour (C::panel.withAlpha (dark * 0.85f));
            g.fillRect (juce::Rectangle<float> (xOfPoint (k), (float) plot.getY(), xOfPoint (k + 1) - xOfPoint (k) + 0.6f, h));
        }
    }
    g.setFont (font (12.0f, true));
    for (size_t i = 0; i < inst.size(); ++i)
        if (best[i].first >= 0)
        {
            const float x = juce::jlimit ((float) plot.getX() + 60.0f, (float) plot.getRight() - 60.0f, xOfPoint (best[i].second));
            const float y = y0 - h * best[i].first;
            g.setColour (C::bg.withAlpha (0.85f));
            g.drawText (inst[i]->name, juce::Rectangle<float> (x - 60, y - 8, 120, 16), juce::Justification::centred);
        }
}

// ---------------------------------------------------------------------------- MASKING
void LumenSpectrumEditor::drawMasking (juce::Graphics& g)
{
    // clash heat behind the curves
    for (int k = 0; k < P - 1; ++k)
        if (heat[k] > 0.05f)
        {
            const float x = xOfPoint (k), w = xOfPoint (k + 1) - x + 0.6f;
            g.setGradientFill (juce::ColourGradient (C::bad.withAlpha (0.42f * heat[k]), 0, (float) plot.getY(),
                                                     C::bad.withAlpha (0.08f * heat[k]), 0, (float) plot.getBottom(), false));
            g.fillRect (juce::Rectangle<float> (x, (float) plot.getY(), w, (float) plot.getHeight()));
        }
    drawCurves (g, true);

    // list panel
    auto L = listArea;
    g.setColour (C::panel2);
    g.fillRoundedRectangle (L.toFloat(), 10.0f);
    L.reduce (12, 10);
    g.setColour (C::text);
    g.setFont (font (13.0f, true));
    g.drawText ("CLASHES", L.removeFromTop (20), juce::Justification::left);
    g.setColour (C::dim);
    g.setFont (font (10.5f));
    g.drawText ("ranked by how much they cost your mix", L.removeFromTop (14), juce::Justification::left);
    L.removeFromTop (6);

    if (numInstruments < 2)
    {
        g.setFont (font (12.0f));
        g.drawFittedText ("Put Lumen Spectrum on at least two instruments (kick + bass is a good start) and play the track.", L.removeFromTop (80), juce::Justification::topLeft, 5);
        return;
    }
    if (clashes.empty())
    {
        g.setColour (C::good);
        g.setFont (font (12.5f, true));
        g.drawFittedText ("No serious clashes right now.", L.removeFromTop (40), juce::Justification::topLeft, 2);
        return;
    }
    for (size_t i = 0; i < clashes.size(); ++i)
    {
        const auto& c = clashes[i];
        auto row = L.removeFromTop (juce::jmin (78, L.getHeight()));
        if (row.getHeight() < 40) break;
        const auto& A = tracks[(size_t) c.a];
        const auto& B = tracks[(size_t) c.b];
        const auto sev = c.mean > 0.75f ? C::bad : c.mean > 0.5f ? C::warn : C::good;
        const juce::String sevT = c.mean > 0.75f ? "HIGH" : c.mean > 0.5f ? "MED" : "LOW";

        // number marker over the plot
        const float x0 = xOfPoint (c.p0), x1 = xOfPoint (c.p1);
        g.setColour (sev.withAlpha (0.9f));
        g.drawLine (x0, (float) plot.getY() + 22.0f, x1, (float) plot.getY() + 22.0f, 2.0f);
        g.fillEllipse ((x0 + x1) * 0.5f - 8.0f, (float) plot.getY() + 14.0f, 16.0f, 16.0f);
        g.setColour (C::bg);
        g.setFont (font (10.5f, true));
        g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> ((x0 + x1) * 0.5f - 8.0f, (float) plot.getY() + 14.0f, 16.0f, 16.0f), juce::Justification::centred);

        // list row
        auto top = row.removeFromTop (18);
        g.setColour (sev);
        g.fillEllipse (top.removeFromLeft (16).toFloat().reduced (1));
        g.setColour (C::bg);
        g.drawText (juce::String ((int) i + 1), top.withX (top.getX() - 16).withWidth (16), juce::Justification::centred);
        top.removeFromLeft (6);
        g.setColour (sev);
        g.setFont (font (10.0f, true));
        g.drawText (sevT, top.removeFromRight (36), juce::Justification::right);
        g.setFont (font (12.5f, true));
        g.setColour (A.col); g.drawText (A.name, top.removeFromLeft (juce::jmin (100, top.getWidth() / 2 - 8)), juce::Justification::left);
        g.setColour (C::dim); g.drawText ("x", top.removeFromLeft (14), juce::Justification::centred);
        g.setColour (B.col); g.drawText (B.name, top, juce::Justification::left);
        g.setColour (C::dim);
        g.setFont (font (11.0f));
        g.drawText (freqText (pointFreq ((float) c.p0)) + " - " + freqText (pointFreq ((float) c.p1)) + "   peak " + freqText (pointFreq ((float) c.pPeak)),
                    row.removeFromTop (16).withTrimmedLeft (22), juce::Justification::left);
        g.setColour (C::text.withAlpha (0.85f));
        g.drawFittedText (clashAdvice (c), row.removeFromTop (32).withTrimmedLeft (22), juce::Justification::topLeft, 2);
        g.setColour (C::line);
        g.drawHorizontalLine (row.getBottom() - 2, (float) row.getX(), (float) row.getRight());
    }
}

// ---------------------------------------------------------------------------- SONOGRAM
void LumenSpectrumEditor::drawSonogram (juce::Graphics& g)
{
    if (sono.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (sono, plot.toFloat());
    }
    // frequency axis on the left (log, vertical)
    g.setFont (font (10.5f, true));
    for (float f : { 30.0f, 60.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 6000.0f, 10000.0f })
    {
        const float y = (float) plot.getBottom() - (float) plot.getHeight() * freqToPoint (f) / (float) (P - 1);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        g.setColour (C::dim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), plot.getX() - 44, (int) y - 7, 38, 14, juce::Justification::right);
    }
    g.setColour (C::dim);
    g.drawText ("now", plot.getRight() - 40, plot.getBottom() + 6, 40, 14, juce::Justification::right);
    g.drawText ("-21 s", plot.getX(), plot.getBottom() + 6, 60, 14, juce::Justification::left);
}

// ---------------------------------------------------------------------------- hover readout
void LumenSpectrumEditor::drawHover (juce::Graphics& g)
{
    if (! plot.toFloat().contains (hover)) return;
    const int v = view();
    float f = freqOfX (hover.x);
    if (v == Sonogram) f = pointFreq ((float) (P - 1) * ((float) plot.getBottom() - hover.y) / (float) plot.getHeight());
    f = juce::jlimit (kFMin, kFMax, f);
    const int k = juce::jlimit (0, P - 1, (int) std::lround (freqToPoint (f)));

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    if (v == Sonogram) g.drawHorizontalLine ((int) hover.y, (float) plot.getX(), (float) plot.getRight());
    else g.drawVerticalLine ((int) hover.x, (float) plot.getY(), (float) plot.getBottom());

    struct Row { juce::Colour c; juce::String n; float v; };
    std::vector<Row> rows;
    for (auto& t : tracks)
        if (t.visible && (v != Ownership || ! t.bus))
            rows.push_back ({ t.col, t.name, v == Ownership ? (mixPow[k] > 1e-13f ? 100.0f * dbToPow (t.db[k]) / mixPow[k] : 0.0f)
                                                             : t.db[k] + tilt (f) });
    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.v > b.v; });
    if (rows.size() > 7) rows.resize (7);

    const int w = 190, hgt = 26 + (int) rows.size() * 16;
    auto box = juce::Rectangle<int> ((int) hover.x + 14, (int) hover.y - hgt / 2, w, hgt);
    if (box.getRight() > plot.getRight()) box.setX ((int) hover.x - 14 - w);
    box = box.constrainedWithin (plot);
    g.setColour (C::bg.withAlpha (0.92f));
    g.fillRoundedRectangle (box.toFloat(), 7.0f);
    g.setColour (C::line);
    g.drawRoundedRectangle (box.toFloat(), 7.0f, 1.0f);
    auto r = box.reduced (9, 5);
    g.setColour (C::text);
    g.setFont (font (12.0f, true));
    g.drawText (freqText (f) + "   " + noteName (f), r.removeFromTop (18), juce::Justification::left);
    g.setFont (font (11.5f));
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (16);
        g.setColour (row.c); g.fillEllipse (line.removeFromLeft (10).toFloat().withSizeKeepingCentre (7, 7));
        line.removeFromLeft (5);
        g.setColour (C::dim);
        g.drawText (v == Ownership ? juce::String (juce::roundToInt (row.v)) + " %" : juce::String (row.v, 1) + " dB", line.removeFromRight (56), juce::Justification::right);
        g.setColour (C::text);
        g.drawText (row.n, line, juce::Justification::left);
    }
}

// ---------------------------------------------------------------------------- band strip (as in the original picture)
void LumenSpectrumEditor::drawBandStrip (juce::Graphics& g)
{
    const auto& me = *selfTrack();
    const int tgt = choice (ids::target);
    const bool snapTarget = tgt == kNumGenres + 1;
    // genre guides describe a whole mix: they apply on the Master / bus instances; a snapshot works anywhere
    const bool hasTarget = tgt > 0 && (snapTarget ? proc.hasSnapshot() : (me.master || me.bus));

    // per region: share of energy, and level relative to the mids (4.5 dB/oct weighted)
    auto analyse = [] (const float* db, float* share, float* rel)
    {
        float energy[kNumRegions] {}, lvl[kNumRegions] {}; int cnt[kNumRegions] {};
        float total = 0;
        for (int k = 0; k < P; ++k)
        {
            const float f = pointFreq ((float) k);
            const int r = regionOf (f);
            const float pw = dbToPow (db[k]);
            energy[r] += pw * f; total += pw * f;        // log-spaced points: dHz ~ f
            lvl[r] += pw * dbToPow (4.5f * std::log2 (f / 1000.0f)); ++cnt[r];
        }
        float L[kNumRegions];
        for (int r = 0; r < kNumRegions; ++r) { share[r] = total > 0 ? energy[r] / total : 0.0f; L[r] = powToDb (lvl[r] / (float) juce::jmax (1, cnt[r])); }
        for (int r = 0; r < kNumRegions; ++r) rel[r] = L[r] - L[3];
    };
    float share[kNumRegions], rel[kNumRegions], tShare[kNumRegions], tRel[kNumRegions] {};
    analyse (me.db, share, rel);
    if (snapTarget && proc.hasSnapshot()) analyse (proc.getSnapshot().data(), tShare, tRel);
    else if (tgt > 0 && tgt <= kNumGenres) for (int r = 0; r < kNumRegions; ++r) tRel[r] = genres()[(size_t) tgt - 1].rel[r];
    const bool silent = me.idle;

    for (int r = 0; r < kNumRegions; ++r)
    {
        const auto& R = regions()[(size_t) r];
        auto box = juce::Rectangle<float> (xOfFreq (R.lo) + 2.0f, (float) stripArea.getY(), xOfFreq (R.hi) - xOfFreq (R.lo) - 4.0f, (float) stripArea.getHeight());
        if (view() == Sonogram || view() == Ownership) box = box.withX (box.getX());
        const auto col = juce::Colour (R.argb);
        g.setColour (col.withAlpha (0.88f));
        g.fillRoundedRectangle (box, 9.0f);
        g.setColour (col.brighter (0.3f).withAlpha (0.5f));
        g.drawRoundedRectangle (box.reduced (0.5f), 9.0f, 1.0f);

        auto in = box.reduced (8.0f, 6.0f);
        const bool narrow = box.getWidth() < 110.0f;
        g.setColour (C::bg.withAlpha (0.9f));
        g.setFont (font (narrow ? 13.0f : 15.5f, true));
        g.drawFittedText (R.name, in.removeFromTop (narrow ? 32.0f : 22.0f).toNearestInt(), juce::Justification::centred, 2);
        g.setFont (font (11.0f, true));
        g.setColour (C::bg.withAlpha (0.7f));
        if (! narrow) g.drawText (freqText (R.lo) + " - " + freqText (R.hi), in.removeFromTop (14).toNearestInt(), juce::Justification::centred);
        const float pc = share[r] * 100.0f;
        juce::String info = silent ? juce::String ("--") : (pc < 10.0f ? juce::String (pc, 1) : juce::String (juce::roundToInt (pc))) + "%";
        if (hasTarget && ! silent && r != 3)
        {
            const float d = rel[r] - tRel[r];
            info << "  " << (d >= 0 ? "+" : "") << juce::String (d, 1) << " dB";
            const auto dc = std::abs (d) <= kTargetTolerance ? juce::Colour (0xff17643a) : std::abs (d) <= 2.0f * kTargetTolerance ? juce::Colour (0xff7a5200) : juce::Colour (0xff8a1626);
            g.setColour (dc);
        }
        else if (hasTarget && r == 3) info << "  ref";
        g.setFont (font (12.0f, true));
        g.drawText (info, in.toNearestInt(), juce::Justification::centred);
    }
}

// ---------------------------------------------------------------------------- legend + meters
void LumenSpectrumEditor::drawLegend (juce::Graphics& g)
{
    chipRects.clear();
    auto r = legendArea;
    // meters (right)
    auto m = r.removeFromRight (330);
    g.setFont (font (11.5f, true));
    auto meter = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
    {
        auto cell = m.removeFromLeft (110);
        g.setColour (C::faint); g.drawText (label, cell.removeFromLeft (44), juce::Justification::right);
        g.setColour (c); g.drawText (value, cell.withTrimmedLeft (6), juce::Justification::left);
    };
    meter ("PEAK", peakDb > -100.0f ? juce::String (peakDb, 1) : juce::String ("-inf"), peakDb > -0.3f ? C::bad : C::text);
    meter ("RMS",  rmsDb > -100.0f ? juce::String (rmsDb, 1) : juce::String ("-inf"), C::text);
    meter ("CORR", juce::String (corr, 2), corr < 0.0f ? C::bad : corr < 0.3f ? C::warn : C::good);

    // instance chips
    g.setFont (font (12.0f, true));
    int x = r.getX();
    for (auto& t : tracks)
    {
        const int w = juce::jmin (170, (int) juce::GlyphArrangement::getStringWidth (font (12.0f, true), t.name) + 36);
        if (x + w > r.getRight()) break;
        auto chip = juce::Rectangle<int> (x, r.getY() + 3, w, r.getHeight() - 6);
        x += w + 6;
        chipRects.push_back ({ chip, t.uid });
        const float a = t.visible ? (t.idle ? 0.5f : 1.0f) : 0.28f;
        g.setColour (t.self ? t.col.withAlpha (0.16f) : C::panel2);
        g.fillRoundedRectangle (chip.toFloat(), 8.0f);
        g.setColour (t.self ? t.col.withAlpha (0.8f) : C::line);
        g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (t.col.withAlpha (a));
        g.fillEllipse (juce::Rectangle<float> ((float) chip.getX() + 10.0f, (float) chip.getCentreY() - 4.5f, 9.0f, 9.0f));
        g.setColour (C::text.withAlpha (a));
        g.drawText (t.name, chip.withTrimmedLeft (26).withTrimmedRight (6), juce::Justification::left);
    }
    if (tracks.size() <= 1)
    {
        g.setColour (C::faint);
        g.setFont (font (11.5f));
        g.drawText ("Add Lumen Spectrum to other tracks - they appear here, each in its own colour.", juce::Rectangle<int> (x, r.getY(), juce::jmax (0, r.getRight() - x), r.getHeight()), juce::Justification::left);
    }
}

// ============================================================================ mouse
void LumenSpectrumEditor::mouseMove (const juce::MouseEvent& e)
{
    hover = e.position;
    if (! plot.contains (e.getPosition())) hover = { -1, -1 };
}
void LumenSpectrumEditor::mouseExit (const juce::MouseEvent&) { hover = { -1, -1 }; }

void LumenSpectrumEditor::mouseDown (const juce::MouseEvent& e)
{
    for (auto& [rect, uid] : chipRects)
        if (rect.contains (e.getPosition()))
        {
            for (auto& t : tracks) if (t.uid == uid && t.self) return;   // this track is always shown
            if (hidden.count (uid)) hidden.erase (uid); else hidden.insert (uid);
            return;
        }
}

void LumenSpectrumEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click a chip: solo it together with this track (double-click again to show all)
    for (auto& [rect, uid] : chipRects)
        if (rect.contains (e.getPosition()))
        {
            bool soloed = true;
            for (auto& t : tracks) if (! t.self && t.uid != uid && hidden.count (t.uid) == 0) soloed = false;
            hidden.clear();
            if (! soloed) for (auto& t : tracks) if (! t.self && t.uid != uid) hidden.insert (t.uid);
            return;
        }
}
