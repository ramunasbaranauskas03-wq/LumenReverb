#include "PluginEditor.h"

using namespace lumen::led;
using Proc = LumenMeterProcessor;

namespace
{
constexpr int W = 1120, HH = 680;
const juce::Colour MAIN = C::green;
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}
juce::String fmt (float v, int dec = 1) { return v < -69.0f ? juce::String ("--.-") : juce::String (v, dec); }
juce::Colour stateCol (int s) { return s == 0 ? C::green : s == 1 ? C::amber : s == 2 ? C::red : C::cyan; }
}

LumenMeterEditor::LumenMeterEditor (LumenMeterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    fbuf.assign (8192 * 2, 0.0f); win.resize (8192);
    for (int i = 0; i < 8192; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 8192.0f);
    std::fill (std::begin (spec), std::end (spec), -120.0f); std::fill (std::begin (specPeak), std::end (specPeak), -120.0f);

    const char* tabs[] { "LOUDNESS & DELIVERY", "ANALYZER & REFERENCE" };
    for (int i = 0; i < 2; ++i) { tabB[i].setButtonText (tabs[i]); tabB[i].onClick = [this, i] { setTab (i); }; addAndMakeVisible (tabB[i]); tabB[i].setBounds (16 + i * 206, 152, 200, 28); }
    resetB.setColour (juce::TextButton::buttonColourId, C::red); resetB.onClick = [this] { proc.resetMeter(); for (auto& v : specAvg) v = 0; avgW = 0; };
    resetB.setTooltip ("Reset all measurements (also automatic when playback starts, if AUTO RESET is on)");
    addAndMakeVisible (resetB); resetB.setBounds (1024, 152, 80, 28);
    auto* ar = add<Toggle> (*this, owned, st, IDs::autoReset, "AUTO RESET", MAIN); ar->setBounds (790, 154, 112, 24);
    ar->setTooltip ("Start a fresh measurement every time playback starts");
    auto* hd = add<Toggle> (*this, owned, st, IDs::hold, "HOLD", C::amber); hd->setBounds (908, 154, 110, 24);
    hd->setTooltip ("Freeze the loudness history graph");

    auto* tg = add<Combo> (*this, owned, st, IDs::target, MAIN); tg->setBounds (800, 222, 290, 26); loudComps.push_back (tg);
    tg->setTooltip ("Delivery target: the history graph, the checks and PREVIEW use it");
    auto* pvw = add<Toggle> (*this, owned, st, IDs::preview, "PREVIEW NORMALISATION", C::cyan); pvw->setBounds (800, 256, 290, 26); loudComps.push_back (pvw);
    pvw->setTooltip ("Hear your track the way the selected platform will play it (its loudness normalisation applied to the output)");

    loadRefB.setColour (juce::TextButton::buttonColourId, C::cyan); loadRefB.setBounds (26, 214, 140, 24); anaComps.push_back (&loadRefB); addAndMakeVisible (loadRefB);
    loadRefB.setTooltip ("Load a reference track (WAV / AIFF / MP3 / FLAC) - or drop it anywhere on the plugin");
    loadRefB.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        { if (fc.getResult().existsAsFile()) { juce::String e; proc.loadReference (fc.getResult(), e); } });
    };
    auto* ab = add<Toggle> (*this, owned, st, IDs::refAB, "LISTEN: REFERENCE", C::cyan); ab->setBounds (172, 214, 160, 24); anaComps.push_back (ab);
    ab->setTooltip ("A/B: the output plays the reference (follows the song position) instead of your mix");
    auto* mt = add<Toggle> (*this, owned, st, IDs::refMatch, "LEVEL MATCH", C::cyan); mt->setBounds (338, 214, 120, 24); anaComps.push_back (mt);
    mt->setTooltip ("Reference is played at YOUR loudness - compare tone and punch, not volume");
    auto* sl = add<Toggle> (*this, owned, st, IDs::slope, "4.5 dB SLOPE", MAIN); sl->setBounds (464, 214, 120, 24); anaComps.push_back (sl);
    sl->setTooltip ("Tilt the spectrum by +4.5 dB/oct: a balanced mix then looks flat");

    setSize (W, HH);
    setTab (0);
    startTimerHz (30);
}

LumenMeterEditor::~LumenMeterEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenMeterEditor::setTab (int t)
{
    tab = t;
    for (auto* c : loudComps) c->setVisible (t == 0);
    for (auto* c : anaComps) c->setVisible (t == 1);
    for (int i = 0; i < 2; ++i) tabB[i].setColour (juce::TextButton::buttonColourId, i == t ? MAIN : C::dim);
    repaint();
}

void LumenMeterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty()) return;
    juce::String e;
    if (proc.loadReference (juce::File (files[0]), e)) setTab (1);
}

void LumenMeterEditor::analyse()
{
    constexpr int N = 8192;
    const int pos = proc.ringPos.load (std::memory_order_acquire);
    for (int i = 0; i < N; ++i)
    {
        const int k = (pos - N + i + Proc::kRing) & (Proc::kRing - 1);
        fbuf[(size_t) i] = 0.5f * (proc.ring[0][k] + proc.ring[1][k]) * win[(size_t) i];
    }
    std::fill (fbuf.begin() + N, fbuf.end(), 0.0f);
    fft.performRealOnlyForwardTransform (fbuf.data(), true);
    const double rate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const double norm = 1.0 / ((N / 4.0) * (N / 4.0));
    double tot = 0;
    float now[Proc::kSpecBands];
    for (int b = 0; b < Proc::kSpecBands; ++b)
    {
        const double f0 = Proc::bandFreq (b) / std::pow (2.0, 1.0 / 12.0), f1 = Proc::bandFreq (b) * std::pow (2.0, 1.0 / 12.0);
        const int k0 = std::max (1, (int) std::floor (f0 * N / rate)), k1 = std::max (k0, std::min (N / 2, (int) std::ceil (f1 * N / rate)));
        double s = 0; for (int k = k0; k <= k1; ++k) s += (double) fbuf[(size_t) (2 * k)] * fbuf[(size_t) (2 * k)] + (double) fbuf[(size_t) (2 * k + 1)] * fbuf[(size_t) (2 * k + 1)];
        now[b] = (float) (10.0 * std::log10 (s * norm + 1e-14));
        tot += s;
    }
    for (int b = 0; b < Proc::kSpecBands; ++b)
    {
        spec[b] += (now[b] - spec[b]) * (now[b] > spec[b] ? 0.5f : 0.12f);
        specPeak[b] = std::max (now[b], specPeak[b] - 0.15f);
    }
    // long-term average (only while there is signal)
    if (tot * norm > 1e-9 && pv (IDs::hold) < 0.5f)
    {
        for (int b = 0; b < Proc::kSpecBands; ++b) specAvg[b] += std::pow (10.0, now[b] / 10.0);
        avgW += 1.0;
    }
}

std::vector<LumenMeterEditor::Check> LumenMeterEditor::computeChecks() const
{
    std::vector<Check> c;
    const auto& pl = platforms()[(size_t) juce::jlimit (0, (int) platforms().size() - 1, (int) pv (IDs::target))];
    const auto s = snap;
    if (! s.valid) { c.push_back ({ 3, "WAITING", "play your track - the checks fill in after a few seconds" }); return c; }
    // true peak
    if (s.tpMax <= pl.tpLimit) c.push_back ({ 0, "TRUE PEAK", juce::String (s.tpMax, 1) + " dBTP - safe for " + pl.name });
    else if (s.tpMax <= 0.0f) c.push_back ({ 1, "TRUE PEAK", juce::String (s.tpMax, 1) + " dBTP - above " + juce::String (pl.tpLimit, 1) + ", codecs (MP3 / AAC) may distort" });
    else c.push_back ({ 2, "TRUE PEAK", "+" + juce::String (s.tpMax, 1) + " dBTP - inter-sample clipping! lower the limiter ceiling" });
    // loudness vs target
    const float g = platformGain (pl, s.i, s.tpMax);
    const float d = s.i - pl.lufs;
    if (pl.policy == 0)
        c.push_back ({ std::abs (d) < 2.0f ? 0 : 3, "LOUDNESS", juce::String (s.i, 1) + " LUFS (" + pl.name + " typical " + juce::String (pl.lufs, 0) + ") - " + pl.note });
    else if (g < -2.0f) c.push_back ({ 1, "LOUDNESS", juce::String (s.i, 1) + " LUFS: " + pl.name + " turns it DOWN " + juce::String (-g, 1) + " dB - the extra limiting buys you nothing there" });
    else if (g > 2.0f || (d < -2.0f && pl.policy == 1)) c.push_back ({ 1, "LOUDNESS", juce::String (s.i, 1) + " LUFS: quieter than " + pl.name + "'s " + juce::String (pl.lufs, 0) + " LUFS" + (g > 0.05f ? " (turned up " + juce::String (g, 1) + " dB)" : juce::String (" - it will sound quieter than other tracks")) });
    else c.push_back ({ 0, "LOUDNESS", juce::String (s.i, 1) + " LUFS - right on target for " + pl.name });
    // dynamics
    const float plr = s.tpMax - s.i;
    c.push_back ({ plr >= 8.0f ? 0 : plr >= 6.0f ? 0 : plr >= 4.0f ? 1 : 2, "DYNAMICS",
                   "PLR " + juce::String (plr, 1) + " dB" + (plr >= 8.0f ? " - punchy, open" : plr >= 6.0f ? " - typical loud EDM master" : plr >= 4.0f ? " - heavily limited, transients suffer" : " - crushed: lower the limiter drive") });
    // mono bass / phase
    c.push_back ({ s.corrLow >= 0.9f ? 0 : s.corrLow >= 0.6f ? 1 : 2, "MONO BASS", "correlation below 150 Hz " + juce::String (s.corrLow, 2) + (s.corrLow >= 0.9f ? " - club safe" : " - make the lows more mono (Lumen MonoFilter)") });
    c.push_back ({ s.corr >= 0.3f ? 0 : s.corr >= 0.0f ? 1 : 2, "PHASE", "overall correlation " + juce::String (s.corr, 2) + (s.corr >= 0.3f ? " - mono compatible" : s.corr >= 0.0f ? " - very wide, check in mono" : " - out of phase: parts cancel in mono") });
    c.push_back ({ s.clips == 0 ? 0 : 2, "CLIPPING", s.clips == 0 ? juce::String ("no samples at full scale") : juce::String (s.clips) + " samples hit 0 dBFS" });
    c.push_back ({ std::abs (s.dc) < 0.001f ? 0 : 1, "DC OFFSET", std::abs (s.dc) < 0.001f ? juce::String ("none") : "DC " + juce::String (juce::Decibels::gainToDecibels (std::abs (s.dc)), 1) + " dB - add a high-pass" });
    // tonal balance: slope of the long-term average 60 Hz .. 12 kHz (dB / octave)
    if (avgW > 30)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (int b = 0; b < Proc::kSpecBands; ++b)
        {
            const float f = Proc::bandFreq (b);
            if (f < 60.0f || f > 12000.0f) continue;
            const double x = std::log2 (f / 1000.0), y = 10.0 * std::log10 (specAvg[b] / avgW + 1e-14);
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
        }
        const double slope = (n * sxy - sx * sy) / std::max (1e-9, n * sxx - sx * sx);
        // band power per 1/6 octave -> spectral density slope = band slope - 3 dB/oct; balanced mixes sit near -4.5 dB/oct
        const double rel = slope - 3.0;           // per-Hz slope in dB/oct
        const int stt = rel > -3.0 ? 1 : rel < -6.0 ? 1 : 0;
        c.push_back ({ stt, "TONAL BALANCE", "spectral slope " + juce::String (rel, 1) + " dB/oct" + (rel > -3.0 ? " - bright / thin" : rel < -6.0 ? " - dark / bass-heavy" : " - balanced (pink-ish)") });
    }
    if (auto rf = proc.getReference())
    {
        const float dl = s.i - rf->integrated;
        c.push_back ({ std::abs (dl) <= 1.0f ? 0 : 3, "VS REFERENCE", "you " + juce::String (s.i, 1) + " / ref " + juce::String (rf->integrated, 1) + " LUFS (" + (dl >= 0 ? "+" : "") + juce::String (dl, 1) + " dB), PLR you "
                       + juce::String (plr, 1) + " / ref " + juce::String (rf->truePeak - rf->integrated, 1) });
    }
    return c;
}

void LumenMeterEditor::timerCallback()
{
    snap = proc.snapshot();
    analyse();
    repaint();
}

void LumenMeterEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    juce::ColourGradient vig (juce::Colours::transparentBlack, W * 0.5f, HH * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, W, 64);
    juce::ColourGradient line (C::green.withAlpha (0.0f), 0, 64, C::cyan.withAlpha (0.0f), (float) W, 64, false);
    line.addColour (0.25, C::green.withAlpha (0.75f)); line.addColour (0.75, C::cyan.withAlpha (0.75f));
    g.setGradientFill (line); g.fillRect (0, 63, W, 2);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (MAIN.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        juce::ColourGradient lg (C::green, 20, 0, C::cyan, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        dm.draw (g, { 146.0f, 18.0f, 220.0f, 28.0f }, "METER", MAIN, 1.85f, juce::Justification::centredLeft, false, 10.5f);
    }
    const auto& pl = platforms()[(size_t) juce::jlimit (0, (int) platforms().size() - 1, (int) pv (IDs::target))];
    g.setFont (font (10.0f)); g.setColour (C::dim);
    g.drawText ("ITU-R BS.1770-4  /  EBU R128", 400, 24, 260, 16, juce::Justification::centredLeft);
    {
        const float gd = proc.appliedGainDb.load();
        const bool ref = pv (IDs::refAB) > 0.5f && proc.getReference() != nullptr, prv = pv (IDs::preview) > 0.5f;
        if (ref || prv)
        {
            g.setColour (ref ? C::cyan : C::amber); g.setFont (font (11.0f));
            g.drawText (ref ? "OUTPUT: REFERENCE  " + juce::String (gd, 1) + " dB" : "OUTPUT: " + juce::String (pl.name).toUpperCase() + " PREVIEW  " + juce::String (gd, 1) + " dB", 700, 24, 400, 16, juce::Justification::centredRight);
        }
    }

    // ---- numbers strip ----
    struct Num { const char* t; float v; const char* unit; juce::Colour c; };
    const float plr = snap.valid ? snap.tpMax - snap.i : -100.0f;
    const Num nums[] {
        { "INTEGRATED", snap.i, "LUFS", std::abs (snap.i - pl.lufs) <= 1.0f ? C::green : snap.i > pl.lufs ? C::amber : C::cyan },
        { "SHORT-TERM", snap.s, "LUFS", C::magenta }, { "MOMENTARY", snap.m, "LUFS", C::cyan },
        { "RANGE (LRA)", snap.valid ? snap.lra : -100.0f, "LU", C::violet },
        { "TRUE PEAK MAX", snap.tpMax, "dBTP", snap.tpMax > 0.0f ? C::red : snap.tpMax > pl.tpLimit ? C::amber : C::green },
        { "PLR", plr, "dB", plr < 4.0f && snap.valid ? C::red : plr < 6.0f && snap.valid ? C::amber : C::green } };
    for (int k = 0; k < 6; ++k)
    {
        auto r = juce::Rectangle<float> (16.0f + k * 182.0f, 72.0f, 176.0f, 72.0f);
        lcd (g, r, nums[k].c);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText (nums[k].t, r.withHeight (16.0f).translated (8, 3), juce::Justification::centredLeft);
        g.drawText (nums[k].unit, r.withTrimmedTop (r.getHeight() - 18.0f).withTrimmedRight (8.0f), juce::Justification::centredRight);
        g.setFont (mono (30.0f)); g.setColour (nums[k].c.interpolatedWith (juce::Colours::white, 0.15f));
        g.drawText (fmt (nums[k].v), r.withTrimmedTop (16.0f).withTrimmedLeft (8.0f).withTrimmedRight (40.0f), juce::Justification::centredLeft);
    }
    if (tab == 0) paintLoudness (g); else paintAnalyzer (g);
}

void LumenMeterEditor::paintLoudness (juce::Graphics& g)
{
    const auto& pl = platforms()[(size_t) juce::jlimit (0, (int) platforms().size() - 1, (int) pv (IDs::target))];
    // ---- history ----
    panel (g, { 16.0f, 186.0f, 760.0f, 262.0f }, "LOUDNESS HISTORY (120 s)", MAIN);
    auto r = juce::Rectangle<float> (26.0f, 210.0f, 690.0f, 228.0f);
    lcd (g, r, MAIN);
    const float lo = -40.0f, hi = 0.0f;
    auto yOf = [&] (float l) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, (l - lo) / (hi - lo)) * r.getHeight(); };
    g.setFont (font (8.5f));
    for (float l = -36.0f; l < 0.0f; l += 6.0f) { const float y = yOf (l); g.setColour (C::dim.withAlpha (0.15f)); g.fillRect (r.getX(), y, r.getWidth(), 1.0f); g.setColour (C::dim); g.drawText (juce::String ((int) l), (int) r.getRight() - 26, (int) y - 11, 22, 10, juce::Justification::centredRight); }
    g.setColour (C::amber.withAlpha (0.12f)); g.fillRect (r.getX(), yOf (pl.lufs + 1.0f), r.getWidth(), yOf (pl.lufs - 1.0f) - yOf (pl.lufs + 1.0f));
    g.setColour (C::amber.withAlpha (0.8f)); g.fillRect (r.getX(), yOf (pl.lufs), r.getWidth(), 1.0f);
    g.drawText ("TARGET " + juce::String (pl.lufs, 0), (int) r.getX() + 6, (int) yOf (pl.lufs) - 12, 100, 10, juce::Justification::centredLeft);
    const int cnt = proc.histCount.load(), hp = proc.histPos.load();
    auto pathOf = [&] (const float* h)
    {
        juce::Path p; bool st = false;
        for (int k = 0; k < cnt; ++k)
        {
            const int idx = (hp - cnt + k + Proc::kHist) % Proc::kHist;
            const float x = r.getRight() - (float) (cnt - k) / (float) Proc::kHist * r.getWidth();
            const float v = h[idx];
            if (v < -69.0f) { st = false; continue; }
            if (! st) { p.startNewSubPath (x, yOf (v)); st = true; } else p.lineTo (x, yOf (v));
        }
        return p;
    };
    g.setColour (C::cyan.withAlpha (0.45f)); g.strokePath (pathOf (proc.histM), juce::PathStrokeType (1.0f));
    glowPath (g, pathOf (proc.histS), C::magenta, 1.8f, 0.9f);
    if (snap.valid) { g.setColour (C::green); const float y = yOf (snap.i); for (float x = r.getX(); x < r.getRight(); x += 8.0f) g.fillRect (x, y, 4.0f, 1.5f); }
    g.setFont (font (8.5f));
    g.setColour (C::cyan); g.drawText ("MOMENTARY", (int) r.getX() + 6, (int) r.getY() + 4, 70, 10, juce::Justification::centredLeft);
    g.setColour (C::magenta); g.drawText ("SHORT-TERM", (int) r.getX() + 76, (int) r.getY() + 4, 80, 10, juce::Justification::centredLeft);
    g.setColour (C::green); g.drawText ("INTEGRATED", (int) r.getX() + 156, (int) r.getY() + 4, 80, 10, juce::Justification::centredLeft);
    // M / S bars
    for (int k = 0; k < 2; ++k)
    {
        const float v = k == 0 ? snap.m : snap.s;
        auto b = juce::Rectangle<float> (724.0f + k * 22.0f, 210.0f, 16.0f, 228.0f);
        g.setColour (juce::Colour (0xff030507)); g.fillRoundedRectangle (b, 3.0f);
        const int segs = 38;
        for (int sgi = 0; sgi < segs; ++sgi)
        {
            const float l = lo + (hi - lo) * (sgi + 0.5f) / segs;
            const bool lit = v >= l;
            const juce::Colour col = l > pl.lufs + 1.0f ? C::amber : l > pl.lufs - 1.0f ? C::green : (k == 0 ? C::cyan : C::magenta);
            auto s = juce::Rectangle<float> (b.getX() + 2.0f, b.getBottom() - (sgi + 1) * b.getHeight() / segs + 1.0f, b.getWidth() - 4.0f, b.getHeight() / segs - 2.0f);
            g.setColour (lit ? col : col.withAlpha (0.08f)); g.fillRoundedRectangle (s, 1.0f);
        }
        g.setColour (C::dim); g.setFont (font (8.0f)); g.drawText (k == 0 ? "M" : "S", (int) b.getX(), (int) b.getBottom() + 1, 16, 8, juce::Justification::centred);
    }

    // ---- target panel ----
    panel (g, { 784.0f, 186.0f, 320.0f, 262.0f }, "TARGET / PREVIEW", MAIN);
    g.setFont (font (9.5f)); g.setColour (C::dim);
    g.drawText ("DELIVER FOR", 800, 206, 200, 14, juce::Justification::centredLeft);
    {
        const float gn = platformGain (pl, snap.i, snap.tpMax);
        auto box = juce::Rectangle<float> (800.0f, 292.0f, 290.0f, 142.0f);
        lcd (g, box, C::cyan);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText ("TARGET  " + juce::String (pl.lufs, 0) + " LUFS  /  " + juce::String (pl.tpLimit, 1) + " dBTP", (int) box.getX() + 10, (int) box.getY() + 6, 270, 12, juce::Justification::centredLeft);
        g.drawText (juce::String (pl.name).toUpperCase() + " WILL CHANGE YOUR TRACK BY", (int) box.getX() + 10, (int) box.getY() + 24, 270, 12, juce::Justification::centredLeft);
        g.setFont (mono (34.0f));
        g.setColour (! snap.valid ? C::dim : gn < -2.0f ? C::amber : std::abs (gn) <= 2.0f ? C::green : C::cyan);
        g.drawText (snap.valid ? (gn > 0 ? "+" : "") + juce::String (gn, 1) + " dB" : juce::String ("--"), (int) box.getX() + 10, (int) box.getY() + 40, 270, 46, juce::Justification::centredLeft);
        g.setFont (font (9.0f)); g.setColour (C::text.withAlpha (0.7f));
        g.drawFittedText (pl.note, juce::Rectangle<int> ((int) box.getX() + 10, (int) box.getY() + 90, 270, 44), juce::Justification::topLeft, 3);
    }

    // ---- platforms ----
    panel (g, { 16.0f, 456.0f, 540.0f, 212.0f }, "STREAMING / DELIVERY", MAIN);
    g.setFont (font (8.5f)); g.setColour (C::dim);
    g.drawText ("PLATFORM", 30, 476, 160, 12, juce::Justification::centredLeft);
    g.drawText ("TARGET", 200, 476, 70, 12, juce::Justification::centredLeft);
    g.drawText ("PLAYBACK CHANGE", 280, 476, 120, 12, juce::Justification::centredLeft);
    g.drawText ("RESULT", 410, 476, 120, 12, juce::Justification::centredLeft);
    const auto& ps = platforms();
    const int sel = (int) pv (IDs::target);
    for (size_t k = 0; k < ps.size(); ++k)
    {
        const float y = 492.0f + (float) k * 19.0f;
        const auto& p = ps[k];
        const float gn = platformGain (p, snap.i, snap.tpMax);
        if ((int) k == sel) { g.setColour (MAIN.withAlpha (0.1f)); g.fillRoundedRectangle (24.0f, y - 1.0f, 524.0f, 18.0f, 3.0f); }
        const int stt = ! snap.valid ? 3 : p.policy == 0 ? 3 : gn < -2.0f ? 1 : gn > 2.0f ? 1 : 0;
        drawLed (g, { 34.0f, y + 8.0f }, 3.0f, stateCol (stt), snap.valid ? 1.0f : 0.2f);
        g.setFont (font (10.0f)); g.setColour (C::text.withAlpha ((int) k == sel ? 1.0f : 0.75f));
        g.drawText (p.name, 46, (int) y, 150, 16, juce::Justification::centredLeft);
        g.setFont (mono (10.0f));
        g.drawText (juce::String (p.lufs, 0) + " LUFS", 200, (int) y, 80, 16, juce::Justification::centredLeft);
        g.setColour (stateCol (stt));
        g.drawText (! snap.valid ? juce::String ("--") : p.policy == 0 ? juce::String ("not normalised") : (gn > 0 ? "+" : "") + juce::String (gn, 1) + " dB", 280, (int) y, 120, 16, juce::Justification::centredLeft);
        g.setColour (C::text.withAlpha (0.6f));
        g.drawText (snap.valid ? fmt (snap.i + gn) + " LUFS" : juce::String ("--"), 410, (int) y, 130, 16, juce::Justification::centredLeft);
    }

    // ---- checks ----
    panel (g, { 564.0f, 456.0f, 540.0f, 212.0f }, "SMART CHECKS", MAIN);
    const auto checks = computeChecks();
    for (size_t k = 0; k < checks.size() && k < 9; ++k)
    {
        const float y = 478.0f + (float) k * 20.5f;
        drawLed (g, { 580.0f, y + 8.0f }, 3.4f, stateCol (checks[k].state), 1.0f);
        g.setFont (font (9.5f)); g.setColour (stateCol (checks[k].state).interpolatedWith (juce::Colours::white, 0.3f));
        g.drawText (checks[k].title, 592, (int) y, 110, 16, juce::Justification::centredLeft);
        g.setFont (font (9.0f)); g.setColour (C::text.withAlpha (0.75f));
        g.drawFittedText (checks[k].text, juce::Rectangle<int> (700, (int) y, 396, 16), juce::Justification::centredLeft, 1);
    }
}

void LumenMeterEditor::paintAnalyzer (juce::Graphics& g)
{
    panel (g, { 16.0f, 186.0f, 740.0f, 482.0f }, "SPECTRUM / REFERENCE", MAIN);
    auto r = juce::Rectangle<float> (26.0f, 246.0f, 720.0f, 412.0f);
    lcd (g, r, MAIN);
    const bool slope = pv (IDs::slope) > 0.5f;
    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log10 (f / 20.0f) / 3.0f; };
    const float top = slope ? 0.0f : -10.0f, bot = top - 72.0f;
    const float specH = r.getHeight() * 0.72f;
    auto yOf = [&] (float db) { return r.getY() + 8.0f + juce::jlimit (0.0f, 1.0f, (top - db) / (top - bot)) * specH; };
    auto tilt = [&] (int b) { return slope ? 4.5f * std::log2 (Proc::bandFreq (b) / 1000.0f) - 3.0f * std::log2 (Proc::bandFreq (b) / 1000.0f) : 0.0f; };
    g.setFont (font (8.5f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (f); g.setColour (C::dim.withAlpha (0.15f)); g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        g.setColour (C::dim); g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x + 2, (int) r.getBottom() - 12, 30, 10, juce::Justification::centredLeft);
    }
    for (float db = top - 12.0f; db > bot; db -= 12.0f) { const float y = yOf (db); g.setColour (C::dim.withAlpha (0.12f)); g.fillRect (r.getX(), y, r.getWidth(), 1.0f); }
    auto curve = [&] (const float* v, float offset)
    {
        juce::Path p;
        for (int b = 0; b < Proc::kSpecBands; ++b)
        {
            const float x = xOf (Proc::bandFreq (b)), y = yOf (v[b] + tilt (b) + offset);
            if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    // mix: live + peak + long-term average
    {
        auto p = curve (spec, 0.0f);
        juce::Path fill (p); fill.lineTo (xOf (Proc::bandFreq (Proc::kSpecBands - 1)), yOf (bot)); fill.lineTo (xOf (20.0f), yOf (bot)); fill.closeSubPath();
        juce::ColourGradient fg (MAIN.withAlpha (0.25f), 0, r.getY(), MAIN.withAlpha (0.02f), 0, r.getY() + specH, false);
        g.setGradientFill (fg); g.fillPath (fill);
        g.setColour (MAIN.withAlpha (0.85f)); g.strokePath (p, juce::PathStrokeType (1.4f));
        g.setColour (C::text.withAlpha (0.25f)); g.strokePath (curve (specPeak, 0.0f), juce::PathStrokeType (0.8f));
    }
    float avg[Proc::kSpecBands];
    const bool haveAvg = avgW > 10;
    for (int b = 0; b < Proc::kSpecBands; ++b) avg[b] = haveAvg ? (float) (10.0 * std::log10 (specAvg[b] / avgW + 1e-14)) : -120.0f;
    if (haveAvg) glowPath (g, curve (avg, 0.0f), C::green.interpolatedWith (juce::Colours::white, 0.4f), 1.2f, 0.6f);
    const auto rf = proc.getReference();
    g.setFont (font (9.0f));
    if (rf != nullptr)
    {
        // reference at YOUR loudness
        const float off = snap.valid ? snap.i - rf->integrated : 0.0f;
        float rv[Proc::kSpecBands]; for (int b = 0; b < Proc::kSpecBands; ++b) rv[b] = rf->spectrum[(size_t) b];
        juce::Path rp = curve (rv, off);
        juce::Path dashed; const float dl[] { 5.0f, 3.0f }; juce::PathStrokeType (1.6f).createDashedStroke (dashed, rp, dl, 2);
        g.setColour (C::cyan); g.fillPath (dashed);
        // difference (you - reference), bottom strip, +-12 dB
        auto dr = juce::Rectangle<float> (r.getX(), r.getY() + specH + 22.0f, r.getWidth(), r.getHeight() - specH - 36.0f);
        g.setColour (C::dim.withAlpha (0.2f)); g.fillRect (dr.getX(), dr.getCentreY(), dr.getWidth(), 1.0f);
        if (haveAvg)
        {
            juce::Path dp;
            for (int b = 0; b < Proc::kSpecBands; ++b)
            {
                const float d = juce::jlimit (-12.0f, 12.0f, avg[b] - (rv[b] + off));
                const float x = xOf (Proc::bandFreq (b)), y = dr.getCentreY() - d / 12.0f * dr.getHeight() * 0.5f;
                if (b == 0) dp.startNewSubPath (x, y); else dp.lineTo (x, y);
            }
            glowPath (g, dp, C::amber, 1.4f, 0.8f);
        }
        g.setColour (C::amber); g.drawText ("YOU - REFERENCE (+-12 dB)", (int) dr.getX() + 6, (int) dr.getY() - 2, 200, 10, juce::Justification::centredLeft);
        g.setColour (C::cyan);
        g.drawText ("REF: " + rf->name + "   " + juce::String (rf->integrated, 1) + " LUFS   TP " + juce::String (rf->truePeak, 1) + "   LRA " + juce::String (rf->lra, 1),
                    470, 196, 280, 14, juce::Justification::centredRight);
    }
    else { g.setColour (C::dim); g.drawText ("drop a reference track anywhere on the plugin", 300, 196, 450, 14, juce::Justification::centredRight); }
    g.setFont (font (8.5f));
    g.setColour (MAIN); g.drawText ("LIVE", (int) r.getX() + 6, (int) r.getY() + 4, 40, 10, juce::Justification::centredLeft);
    g.setColour (C::green.interpolatedWith (juce::Colours::white, 0.4f)); g.drawText ("AVERAGE", (int) r.getX() + 40, (int) r.getY() + 4, 60, 10, juce::Justification::centredLeft);
    if (rf != nullptr) { g.setColour (C::cyan); g.drawText ("REFERENCE (level matched)", (int) r.getX() + 100, (int) r.getY() + 4, 160, 10, juce::Justification::centredLeft); }

    // ---- stereo ----
    panel (g, { 764.0f, 186.0f, 340.0f, 482.0f }, "STEREO", MAIN);
    {
        auto sr = juce::Rectangle<float> (784.0f, 210.0f, 300.0f, 260.0f);
        if (scope.getWidth() != (int) sr.getWidth()) scope = juce::Image (juce::Image::ARGB, (int) sr.getWidth(), (int) sr.getHeight(), true);
        {
            juce::Image::BitmapData bd (scope, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < bd.height; ++y) for (int x = 0; x < bd.width; ++x) { auto* px = bd.getPixelPointer (x, y); for (int c = 0; c < 4; ++c) px[c] = (juce::uint8) (px[c] * 0.75f); }
            juce::Graphics sg (scope);
            const int pos = proc.ringPos.load();
            float pk = 1e-3f; for (int i = 0; i < 2048; ++i) { const int k = (pos - 2048 + i + Proc::kRing) & (Proc::kRing - 1); pk = std::max ({ pk, std::abs (proc.ring[0][k]), std::abs (proc.ring[1][k]) }); }
            const float sc = 0.45f * sr.getHeight() / std::max (0.2f, pk);
            for (int i = 0; i < 2048; ++i)
            {
                const int k = (pos - 2048 + i + Proc::kRing) & (Proc::kRing - 1);
                const float l = proc.ring[0][k], rr = proc.ring[1][k];
                const float x = sr.getWidth() * 0.5f + (rr - l) * 0.7071f * sc, y = sr.getHeight() * 0.55f - (l + rr) * 0.7071f * sc;
                sg.setColour (C::green.interpolatedWith (C::cyan, juce::jlimit (0.0f, 1.0f, 0.5f + (rr - l) * 2.0f)).withAlpha (0.35f));
                sg.fillRect (x - 0.8f, y - 0.8f, 1.6f, 1.6f);
            }
        }
        g.setColour (juce::Colour (0xff020306)); g.fillRoundedRectangle (sr, 6.0f);
        g.setColour (C::border);
        g.drawLine (sr.getCentreX(), sr.getY() + 8, sr.getCentreX(), sr.getBottom() - 8, 1.0f);
        g.drawLine (sr.getCentreX() - 110, sr.getY() + 0.55f * sr.getHeight() - 110, sr.getCentreX() + 110, sr.getY() + 0.55f * sr.getHeight() + 110, 1.0f);
        g.drawLine (sr.getCentreX() + 110, sr.getY() + 0.55f * sr.getHeight() - 110, sr.getCentreX() - 110, sr.getY() + 0.55f * sr.getHeight() + 110, 1.0f);
        g.drawImageAt (scope, (int) sr.getX(), (int) sr.getY());
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText ("L", (int) sr.getX() + 24, (int) sr.getY() + 10, 12, 12, juce::Justification::centred);
        g.drawText ("R", (int) sr.getRight() - 36, (int) sr.getY() + 10, 12, 12, juce::Justification::centred);
        g.drawText ("M", (int) sr.getCentreX() - 6, (int) sr.getY() + 4, 12, 12, juce::Justification::centred);
    }
    auto corrBar = [&] (juce::Rectangle<float> b, float c, const juce::String& t)
    {
        g.setColour (juce::Colour (0xff030508)); g.fillRoundedRectangle (b, 3.0f);
        const float x0 = b.getCentreX(), x1 = b.getX() + (c + 1.0f) * 0.5f * b.getWidth();
        const auto col = c < 0 ? C::red : c < 0.3f ? C::amber : C::green;
        g.setColour (col.withAlpha (0.85f)); g.fillRect (std::min (x0, x1), b.getY() + 3.0f, std::abs (x1 - x0), b.getHeight() - 6.0f);
        g.setColour (C::text.withAlpha (0.4f)); g.fillRect (x0, b.getY(), 1.0f, b.getHeight());
        g.setFont (font (8.5f)); g.setColour (C::dim); g.drawText (t, (int) b.getX(), (int) b.getY() - 13, 200, 11, juce::Justification::centredLeft);
        g.setFont (mono (10.0f)); g.setColour (col); g.drawText (juce::String (c, 2), (int) b.getRight() - 60, (int) b.getY() - 14, 60, 12, juce::Justification::centredRight);
    };
    corrBar ({ 784.0f, 494.0f, 300.0f, 14.0f }, snap.corr, "CORRELATION");
    corrBar ({ 784.0f, 532.0f, 300.0f, 14.0f }, snap.corrLow, "CORRELATION BELOW 150 Hz");
    const char* bn[] { "LOW < 200", "MID", "HIGH > 3k" };
    for (int b = 0; b < 3; ++b)
    {
        const float w = proc.bandWidth[b].load();
        auto bb = juce::Rectangle<float> (784.0f + b * 102.0f, 572.0f, 94.0f, 84.0f);
        lcd (g, bb, C::violet);
        g.setFont (font (8.5f)); g.setColour (C::dim); g.drawText (bn[b], bb.withHeight (14.0f).translated (0, 3), juce::Justification::centred);
        g.setFont (mono (18.0f)); g.setColour (b == 0 && w > 0.3f ? C::amber : C::violet.interpolatedWith (juce::Colours::white, 0.3f));
        g.drawText (juce::String (juce::roundToInt (w * 100)) + "%", bb.withTrimmedTop (18.0f).withHeight (30.0f), juce::Justification::centred);
        g.setFont (font (8.0f)); g.setColour (C::dim); g.drawText ("SIDE / MID", bb.withTrimmedTop (56.0f).withHeight (12.0f), juce::Justification::centred);
    }
}
