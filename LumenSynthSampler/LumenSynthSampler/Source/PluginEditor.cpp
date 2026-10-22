#include "PluginEditor.h"
using namespace lumen::led;

static constexpr int W = 1280, HH = 868;
static const juce::Colour layerCol[3] { C::cyan, C::magenta, C::amber };

// ================================================================================================ WaveDrop
void WaveDrop::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    lcd (g, r, layerCol[layer]);
    const auto s = proc.slot (layer);
    const auto col = layerCol[layer];
    if (s.src == nullptr)
    {
        g.setColour (hover ? col : C::dim); g.setFont (font (13.0f));
        g.drawText ("DROP SAMPLE HERE", r.withTrimmedBottom (18), juce::Justification::centred);
        g.setFont (font (10.0f, false)); g.drawText ("bass / pluck / lead / pad  -  or click", r.withTrimmedTop (30), juce::Justification::centred);
        return;
    }
    if (builtVersion != proc.sampleVersion.load() || builtW != getWidth())
    {
        builtVersion = proc.sampleVersion.load(); builtW = getWidth();
        const int w = std::max (1, getWidth() - 4); mn.assign ((size_t) w, 0); mx.assign ((size_t) w, 0);
        const float* d = s.src->data (0, 0);
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((juce::int64) s.src->length * x / w), b = std::max (a + 1, (int) ((juce::int64) s.src->length * (x + 1) / w));
            float lo = 0, hi = 0; const int step = std::max (1, (b - a) / 512);
            for (int i = a; i < std::min (b, s.src->length); i += step) { lo = std::min (lo, d[i]); hi = std::max (hi, d[i]); }
            mn[(size_t) x] = lo; mx[(size_t) x] = hi;
        }
    }
    const auto area = r.reduced (2, 16);
    const float cy = area.getCentreY(), amp = area.getHeight() * 0.48f;
    // loop region + start
    const float x0 = area.getX();
    if (proc.param (ssp::L (layer, "loop")) > 0.5f)
    {
        const float a = x0 + area.getWidth() * proc.param (ssp::L (layer, "lstart")), b = x0 + area.getWidth() * proc.param (ssp::L (layer, "lend"));
        g.setColour (C::green.withAlpha (0.12f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (a, area.getY(), b, area.getBottom()));
        g.setColour (C::green.withAlpha (0.8f)); g.drawVerticalLine ((int) a, area.getY(), area.getBottom()); g.drawVerticalLine ((int) b, area.getY(), area.getBottom());
    }
    juce::Path p;
    for (size_t x = 0; x < mx.size(); ++x) p.addRectangle (x0 + (float) x, cy - mx[x] * amp, 1.0f, std::max (1.0f, (mx[x] - mn[x]) * amp));
    g.setColour (col.withAlpha (0.85f)); g.fillPath (p);
    const float st = x0 + area.getWidth() * proc.param (ssp::L (layer, "start"));
    g.setColour (C::text.withAlpha (0.7f)); g.drawVerticalLine ((int) st, area.getY(), area.getBottom());
    g.setColour (C::text.withAlpha (0.85f)); g.setFont (mono (10.0f));
    g.drawText (proc.layerInfo (layer), r.reduced (6, 2), juce::Justification::topLeft);
    if (s.rs && s.rs->valid)
    {
        g.setColour (C::green); g.setFont (font (9.5f));
        g.drawText ("SYNTH READY  " + juce::String (s.rs->K) + " cycles", r.reduced (6, 2), juce::Justification::bottomRight);
    }
}

void WaveDrop::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { proc.clearLayer (layer); repaint(); return; }
    ed.loadInto (layer, {});
}
void WaveDrop::filesDropped (const juce::StringArray& f, int, int) { hover = false; if (! f.isEmpty()) ed.loadInto (layer, juce::File (f[0])); }

// ================================================================================================ Editor
LumenSynthSamplerEditor::Knob& LumenSynthSamplerEditor::knob (int t, const juce::String& id, const juce::String& lbl, juce::Colour c, int x, int y, bool bip, int w)
{
    auto k = std::make_unique<Knob> (proc.apvts, id, lbl, c, bip); k->setBounds (x, y, w, 70); addChildComponent (*k);
    auto* raw = k.get(); tabComps[t].push_back (raw); owned.push_back (std::move (k)); return *raw;
}
LumenSynthSamplerEditor::Toggle& LumenSynthSamplerEditor::toggle (int t, const juce::String& id, const juce::String& txt, juce::Colour c, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Toggle> (proc.apvts, id, txt, c, txt.isEmpty()); k->setBounds (r); addChildComponent (*k);
    auto* raw = k.get(); tabComps[t].push_back (raw); owned.push_back (std::move (k)); return *raw;
}
LumenSynthSamplerEditor::Combo& LumenSynthSamplerEditor::combo (int t, const juce::String& id, juce::Colour c, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Combo> (proc.apvts, id, c); k->setBounds (r); addChildComponent (*k);
    auto* raw = k.get(); tabComps[t].push_back (raw); owned.push_back (std::move (k)); return *raw;
}
void LumenSynthSamplerEditor::box (int t, juce::Rectangle<int> r, const juce::String& title, juce::Colour c, const juce::String& onId)
{
    boxes.push_back ({ t, r.toFloat(), title, c, onId });
    if (onId.isNotEmpty()) toggle (t, onId, "", c, { r.getRight() - 28, r.getY() + 3, 22, 22 });
}

LumenSynthSamplerEditor::LumenSynthSamplerEditor (LumenSynthSamplerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);
    // ---- header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedId() - 1; if (i >= 0 && i != proc.getCurrentProgram()) { proc.setCurrentProgram (i); showMessage ("Preset: " + proc.getProgramName (i) + "  (your samples stay loaded)"); } };
    addAndMakeVisible (presetBox); presetBox.setBounds (330, 15, 230, 26);
    const char* tn[] { "MAIN", "MOD + ARP", "FX" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setButtonText (tn[i]); tabs[i].getProperties().set ("tab", true); tabs[i].setColour (juce::TextButton::buttonColourId, C::cyan);
        tabs[i].onClick = [this, i] { selectTab (i); };
        addAndMakeVisible (tabs[i]); tabs[i].setBounds (580 + i * 104, 14, 100, 28);
    }
    for (auto* b : { &abA, &abB, &abCopy }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    abA.setBounds (912, 14, 30, 28); abB.setBounds (944, 14, 30, 28); abCopy.setBounds (976, 14, 52, 28);
    abA.onClick = [this] { proc.switchAB (0); }; abB.onClick = [this] { proc.switchAB (1); };
    abCopy.onClick = [this] { proc.copyAB(); showMessage (juce::String ("Copied ") + (proc.getABSlot() == 0 ? "A to B" : "B to A")); };

    // ================= MAIN
    for (int k = 0; k < 3; ++k)
    {
        const int x = 12 + k * 422, y = 64;
        const auto c = layerCol[k];
        box (0, { x, y, 414, 330 }, juce::String ("LAYER ") + juce::String::charToString ((juce::juce_wchar) ('A' + k)), c, ssp::L (k, "on"));
        auto wd = std::make_unique<WaveDrop> (proc, *this, k); wd->setBounds (x + 10, y + 30, 394, 104); addChildComponent (*wd); tabComps[0].push_back (wd.get()); owned.push_back (std::move (wd));
        combo (0, ssp::L (k, "mode"), c, { x + 10, y + 140, 160, 24 }).setTooltip ("SYNTH: plays the sample like a synth - any note keeps the original length and tone (no chipmunk).  CLASSIC: tape-style repitch.  STRETCH: grains for chords / vocals / noise");
        toggle (0, ssp::L (k, "exact"), "EXACT ROOT", c, { x + 176, y + 140, 96, 24 }).setTooltip ("At the root note the original file is played bit-identical");
        toggle (0, ssp::L (k, "loop"), "LOOP", C::green, { x + 276, y + 140, 62, 24 });
        toggle (0, ssp::L (k, "rev"), "REV", C::orange, { x + 342, y + 140, 62, 24 });
        const char* r1[] { "root", "oct", "semi", "fine", "level", "pan" }; const char* l1[] { "ROOT", "OCT", "SEMI", "FINE", "LEVEL", "PAN" };
        for (int i = 0; i < 6; ++i) knob (0, ssp::L (k, r1[i]), l1[i], c, x + 12 + i * 65, y + 170, i >= 1 && i != 4 && i != 0, 62);
        const char* r2[] { "start", "speed", "trans", "lstart", "lend", "xfade" }; const char* l2[] { "START/POS", "SPEED", "TRANSIENT", "LOOP ST", "LOOP END", "X-FADE" };
        for (int i = 0; i < 6; ++i) knob (0, ssp::L (k, r2[i]), l2[i], i >= 3 ? C::green : c, x + 12 + i * 65, y + 246, false, 62);
    }
    // lower row
    {
        const int y = 402;
        box (0, { 12, y, 240, 362 }, "SUB + NOISE", C::violet);
        toggle (0, "sub_on", "SUB", C::violet, { 24, y + 34, 70, 24 });
        combo (0, "sub_shape", C::violet, { 98, y + 34, 70, 24 }); combo (0, "sub_oct", C::violet, { 172, y + 34, 70, 24 });
        knob (0, "sub_level", "SUB LEVEL", C::violet, 24, y + 66, false, 70);
        toggle (0, "noise_on", "NOISE", C::violet, { 24, y + 156, 90, 24 });
        knob (0, "noise_level", "NOISE LVL", C::violet, 24, y + 188, false, 70); knob (0, "noise_color", "COLOR", C::violet, 100, y + 188, false, 70);
        box (0, { 260, y, 300, 362 }, "FILTER", C::amber, "f_on");
        combo (0, "f_type", C::amber, { 272, y + 34, 140, 24 });
        knob (0, "cutoff", "CUTOFF", C::amber, 272, y + 66, false, 66); knob (0, "res", "RESO", C::amber, 340, y + 66, false, 66); knob (0, "drive", "DRIVE", C::amber, 408, y + 66, false, 66);
        knob (0, "f_env", "ENV AMT", C::amber, 272, y + 146, true, 66); knob (0, "key_track", "KEY TRACK", C::amber, 340, y + 146, false, 66);
        const char* ed[3] { "AMP ENVELOPE", "FILTER ENVELOPE", "" };
        for (int e = 0; e < 2; ++e)
        {
            const int x = 568 + e * 262;
            box (0, { x, y, 254, 362 }, ed[e], e == 0 ? C::cyan : C::amber);
            const char* w[] { "a", "d", "s", "r" }; const char* lb[] { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
            for (int i = 0; i < 4; ++i) knob (0, ssp::env (e, w[i]), lb[i], e == 0 ? C::cyan : C::amber, x + 10 + i * 59, y + 160, false, 58);
        }
        knob (0, "vel_sens", "VEL > VOL", C::cyan, 578, y + 240, false, 58);
        box (0, { 1092, y, 176, 362 }, "VOICE", C::cyan);
        knob (0, "poly", "VOICES", C::cyan, 1100, y + 30, false, 54); knob (0, "glide", "GLIDE", C::cyan, 1156, y + 30, false, 54); knob (0, "bend", "BEND", C::cyan, 1212, y + 30, false, 54);
        toggle (0, "legato", "MONO / LEGATO", C::cyan, { 1104, y + 108, 152, 24 });
        knob (0, "uni", "UNISON", C::violet, 1100, y + 146, false, 54); knob (0, "uni_det", "DETUNE", C::violet, 1156, y + 146, false, 54); knob (0, "uni_spread", "SPREAD", C::violet, 1212, y + 146, false, 54);
    }
    // ================= MOD + ARP
    for (int l = 0; l < 2; ++l)
    {
        const int x = 12 + l * 318;
        box (1, { x, 64, 310, 190 }, "LFO " + juce::String (l + 1), l == 0 ? C::violet : C::magenta);
        const auto c = l == 0 ? C::violet : C::magenta;
        combo (1, ssp::lfo (l, "shape"), c, { x + 12, 98, 130, 24 });
        toggle (1, ssp::lfo (l, "sync"), "SYNC", c, { x + 12, 128, 62, 24 }); combo (1, ssp::lfo (l, "div"), c, { x + 78, 128, 64, 24 });
        toggle (1, ssp::lfo (l, "retrig"), "RETRIG", c, { x + 12, 158, 130, 24 });
        knob (1, ssp::lfo (l, "rate"), "RATE", c, x + 160, 96, false, 70);
    }
    box (1, { 648, 64, 300, 190 }, "MOD ENVELOPE", C::green);
    { const char* w[] { "a", "d", "s", "r" }; const char* lb[] { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
      for (int i = 0; i < 4; ++i) knob (1, ssp::env (2, w[i]), lb[i], C::green, 660 + i * 70, 110, false, 64); }
    box (1, { 956, 64, 312, 190 }, "MACROS", C::orange);
    for (int m = 0; m < 4; ++m) knob (1, ssp::macro (m), "MACRO " + juce::String (m + 1), C::orange, 966 + m * 74, 110, false, 70);
    box (1, { 12, 262, 830, 360 }, "MOD MATRIX", C::cyan);
    for (int s = 0; s < lumen::ss::kSlots; ++s)
    {
        const int col = s / 4, row = s % 4, x = 24 + col * 410, y = 296 + row * 80;
        combo (1, ssp::mod (s, "src"), C::cyan, { x, y + 8, 150, 24 }); combo (1, ssp::mod (s, "dst"), C::amber, { x + 156, y + 8, 160, 24 });
        knob (1, ssp::mod (s, "amt"), "AMOUNT", C::cyan, x + 326, y - 4, true, 60);
    }
    box (1, { 850, 262, 418, 360 }, "ARPEGGIATOR / CHORD", C::magenta, "arp_on");
    combo (1, "arp_mode", C::magenta, { 864, 298, 130, 24 }); combo (1, "arp_rate", C::magenta, { 1000, 298, 90, 24 }); combo (1, "arp_chord", C::magenta, { 1096, 298, 120, 24 });
    knob (1, "arp_oct", "OCTAVES", C::magenta, 864, 340, false, 70); knob (1, "arp_gate", "GATE", C::magenta, 940, 340, false, 70); knob (1, "arp_swing", "SWING", C::magenta, 1016, 340, false, 70);
    // ================= FX
    {
        struct FxDef { const char* id; const char* name; std::vector<std::pair<const char*, const char*>> knobs; const char* combo; const char* tog; };
        const std::vector<FxDef> defs {
            { "hyp", "HYPER / DIMENSION", { { "hyp_detune", "DETUNE" }, { "hyp_dim", "DIMENSION" }, { "hyp_mix", "MIX" } }, nullptr, nullptr },
            { "dist", "DISTORTION", { { "dist_drive", "DRIVE" }, { "dist_mix", "MIX" } }, "dist_mode", nullptr },
            { "fla", "FLANGER", { { "fla_rate", "RATE" }, { "fla_depth", "DEPTH" }, { "fla_fb", "FEEDBACK" }, { "fla_mix", "MIX" } }, nullptr, nullptr },
            { "pha", "PHASER", { { "pha_rate", "RATE" }, { "pha_depth", "DEPTH" }, { "pha_fb", "FEEDBACK" }, { "pha_mix", "MIX" } }, nullptr, nullptr },
            { "cho", "CHORUS", { { "cho_rate", "RATE" }, { "cho_depth", "DEPTH" }, { "cho_mix", "MIX" } }, nullptr, nullptr },
            { "dly", "DELAY", { { "dly_fb", "FEEDBACK" }, { "dly_tone", "TONE" }, { "dly_mix", "MIX" } }, "dly_div", "dly_ping" },
            { "cmp", "COMPRESSOR", { { "cmp_thresh", "THRESH" }, { "cmp_ratio", "RATIO" }, { "cmp_attack", "ATTACK" }, { "cmp_release", "RELEASE" }, { "cmp_gain", "GAIN" } }, nullptr, nullptr },
            { "rev", "REVERB", { { "rev_size", "SIZE" }, { "rev_decay", "DECAY" }, { "rev_damp", "DAMP" }, { "rev_mix", "MIX" } }, nullptr, nullptr },
            { "eq", "EQ", { { "eq_low", "LOW" }, { "eq_mid", "MID" }, { "eq_mfreq", "FREQ" }, { "eq_high", "HIGH" } }, nullptr, nullptr },
            { "ott", "OTT", { { "ott_depth", "DEPTH" }, { "ott_time", "TIME" } }, nullptr, nullptr },
            { "pump", "PUMP (sidechain)", { { "pump_depth", "DEPTH" }, { "pump_curve", "RELEASE" } }, "pump_div", nullptr },
        };
        static const juce::Colour fc[] { C::violet, C::red, C::magenta, C::magenta, C::cyan, C::green, C::amber, C::cyan, C::amber, C::orange, C::green };
        for (size_t i = 0; i < defs.size(); ++i)
        {
            const int col = (int) i % 4, row = (int) i / 4, x = 12 + col * 316, y = 64 + row * 236;
            const auto& d = defs[i]; const auto c = fc[i];
            box (2, { x, y, 308, 228 }, d.name, c, juce::String (d.id) + "_on");
            int ky = y + 40;
            if (d.combo) { combo (2, d.combo, c, { x + 12, y + 36, 110, 24 }); ky = y + 74; }
            if (d.tog) toggle (2, d.tog, "PING-PONG", c, { x + 128, y + 36, 110, 24 });
            for (size_t k = 0; k < d.knobs.size(); ++k) knob (2, d.knobs[k].first, d.knobs[k].second, c, x + 10 + (int) k * 59, ky, juce::String (d.knobs[k].first).startsWith ("eq_l") || juce::String (d.knobs[k].first) == "eq_mid" || juce::String (d.knobs[k].first) == "eq_high", 58);
        }
        const int x = 12 + 3 * 316, y = 64 + 2 * 236;
        box (2, { x, y, 308, 228 }, "OUTPUT", C::cyan);
        knob (2, "vol", "VOLUME", C::cyan, x + 12, y + 40, false, 70);
        toggle (2, "clip", "SOFT CLIP", C::red, { x + 100, y + 60, 120, 24 });
    }
    // keyboard
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, C::cyan.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, C::cyan.withAlpha (0.25f));
    keyboard.setLowestVisibleKey (24); keyboard.setKeyWidth (17.0f);
    addAndMakeVisible (keyboard); keyboard.setBounds (12, 778, W - 24, 82);
    selectTab (0);
    setSize (W, HH);
    startTimerHz (30);
}

LumenSynthSamplerEditor::~LumenSynthSamplerEditor() { setLookAndFeel (nullptr); }

void LumenSynthSamplerEditor::selectTab (int t)
{
    tab = t;
    for (int i = 0; i < 3; ++i) { tabs[i].setToggleState (i == t, juce::dontSendNotification); for (auto* c : tabComps[i]) c->setVisible (i == t); }
    repaint();
}

void LumenSynthSamplerEditor::loadInto (int layer, const juce::File& f)
{
    if (f == juce::File())
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample into layer " + juce::String::charToString ((juce::juce_wchar) ('A' + layer)),
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, layer] (const juce::FileChooser& fc)
        { if (fc.getResult().existsAsFile()) loadInto (layer, fc.getResult()); });
        return;
    }
    juce::String err;
    if (proc.loadFile (layer, f, err)) showMessage ("Layer " + juce::String::charToString ((juce::juce_wchar) ('A' + layer)) + ": " + f.getFileName() + "  - analysing for SYNTH mode (plays right away)");
    else showMessage (err);
    repaint();
}

void LumenSynthSamplerEditor::timerCallback()
{
    meter = std::max (proc.outPeak.load(), meter * 0.86f);
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1) presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification); abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    if (msg.isNotEmpty() && juce::Time::getMillisecondCounter() - msgTime > 8000) msg.clear();
    for (auto& o : owned) if (auto* w = dynamic_cast<WaveDrop*> (o.get())) if (w->isVisible()) w->repaint();
    repaint (0, 0, W, 60); repaint (0, 760, W, 20);
}

void LumenSynthSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::panelTop); g.fillRect (0, 0, W, 56);
    g.setColour (C::border); g.drawHorizontalLine (56, 0, (float) W);
    g.setColour (C::text); g.setFont (font (22.0f)); g.drawText ("LUMEN", 16, 13, 90, 30, juce::Justification::centredLeft);
    g.setColour (C::cyan); g.drawText ("SYNTH SAMPLER", 98, 13, 220, 30, juce::Justification::centredLeft);
    ledMeter (g, { (float) W - 228, 18, 210, 8 }, meter, meter, false, 0);
    g.setColour (C::dim); g.setFont (mono (10.0f));
    g.drawText (juce::String (proc.voicesActive.load()) + " voices", W - 228, 30, 210, 14, juce::Justification::centredLeft);
    for (auto& b : boxes) if (b.tab == tab) panel (g, b.r, b.title, b.col, b.onId.isEmpty() || pv (b.onId) > 0.5f);
    // envelope drawings on the MAIN tab
    if (tab == 0)
    {
        for (int e = 0; e < 2; ++e)
        {
            auto r = juce::Rectangle<float> (578.0f + e * 262, 438, 234, 110);
            lcd (g, r, C::cyan);
            const float a = pv (ssp::env (e, "a")), d = pv (ssp::env (e, "d")), s = pv (ssp::env (e, "s")), rl = pv (ssp::env (e, "r"));
            const float tot = std::max (0.05f, a + d + 0.4f + rl);
            auto X = [&] (float t) { return r.getX() + 6 + (r.getWidth() - 12) * t / tot; };
            auto Y = [&] (float v) { return r.getBottom() - 8 - (r.getHeight() - 16) * v; };
            juce::Path p; p.startNewSubPath (X (0), Y (0)); p.lineTo (X (a), Y (1)); p.quadraticTo (X (a + d * 0.3f), Y (s), X (a + d), Y (s)); p.lineTo (X (a + d + 0.4f), Y (s)); p.quadraticTo (X (a + d + 0.4f + rl * 0.2f), Y (0), X (tot), Y (0));
            glowPath (g, p, e == 0 ? C::cyan : C::amber, 1.6f);
        }
        g.setColour (C::dim); g.setFont (font (10.0f, false));
        g.drawFittedText ("Root note + EXACT ROOT = the original file, bit-identical.  Every other key: SYNTH mode keeps the length, envelope and tone of the sample - only the pitch changes.",
                          juce::Rectangle<int> (24, 682, 216, 76), juce::Justification::topLeft, 6, 0.9f);
    }
    if (tab == 1)
    {
        g.setColour (C::dim); g.setFont (font (11.0f, false));
        g.drawFittedText ("MOD MATRIX: pick a source (LFO, envelope, velocity, mod wheel, macro...) and a destination.  Position = scan through the sample like a wavetable (SYNTH mode), "
                          "Speed = how fast the sample's own movement plays (0 = frozen).  Macros can be automated / mapped in Ableton.  ARP + CHORD: one key plays a chord or an arpeggio, synced to the project tempo.",
                          juce::Rectangle<int> (14, 636, W - 28, 60), juce::Justification::topLeft, 3, 0.9f);
    }
    if (msg.isNotEmpty()) { g.setColour (C::amber); g.setFont (mono (11.0f)); g.drawText (msg, 14, 760, W - 28, 16, juce::Justification::centredLeft); }
}
