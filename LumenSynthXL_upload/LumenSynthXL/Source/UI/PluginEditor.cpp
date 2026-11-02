#include "PluginEditor.h"
#include "PresetBrowser.h"

namespace lxl::ui
{
using Tickers = std::vector<std::function<void()>>;

/** Base for pages: owns its child components and registers live-update callbacks. */
class Page : public juce::Component
{
public:
    Page (LumenSynthXLProcessor& p, Tickers& t) : proc (p), tickers (t) {}

    template <typename T, typename... Args>
    T& add (juce::Component& parent, Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        parent.addAndMakeVisible (c);
        return *c;
    }
    Knob& knob (juce::Component& parent, const juce::String& id, const juce::String& label, bool bipolar = false)
    {
        auto& k = add<Knob> (parent, proc, id, label, bipolar);
        auto* kp = &k;
        tickers.push_back ([kp] { if (gForceTick || kp->isShowing()) kp->tick(); });
        return k;
    }
    Choice& choice (juce::Component& parent, const juce::String& id, const juce::String& label = {})
    {
        return add<Choice> (parent, proc.apvts, id, label);
    }
    Panel& panel (juce::Component& parent, const juce::String& title, const juce::String& power = {})
    {
        return add<Panel> (parent, title, power.isNotEmpty() ? &proc.apvts : nullptr, power);
    }
    template <typename V>
    V& view (juce::Component& parent, V* v)
    {
        owned.add (v);
        parent.addAndMakeVisible (v);
        tickers.push_back ([v] { if (gForceTick || v->isShowing()) v->tick(); });
        return *v;
    }

protected:
    LumenSynthXLProcessor& proc;
    Tickers& tickers;
    juce::OwnedArray<juce::Component> owned;
};

// ================================================================== SYNTH page
class SynthPage : public Page
{
public:
    SynthPage (LumenSynthXLProcessor& p, Tickers& t) : Page (p, t)
    {
        for (int o = 0; o < kNumOsc; ++o) buildOsc (o);
        buildSubNoise();
        for (int f = 0; f < kNumFilters; ++f) buildFilter (f);
        buildRouting();
        buildInserts();
        buildAmp();
        buildVoice();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto row1 = r.removeFromTop (250); r.removeFromTop (8);
        auto row2 = r.removeFromTop (150); r.removeFromTop (8);
        auto row3 = r;
        oscPanel[0]->setBounds (row1.removeFromLeft ((row1.getWidth() - 10) / 2)); row1.removeFromLeft (10);
        oscPanel[1]->setBounds (row1);
        subNoise->setBounds (row2.removeFromLeft (336)); row2.removeFromLeft (6);
        filPanel[0]->setBounds (row2.removeFromLeft (326)); row2.removeFromLeft (6);
        filPanel[1]->setBounds (row2.removeFromLeft (326)); row2.removeFromLeft (6);
        routing->setBounds (row2);
        inserts->setBounds (row3.removeFromLeft (380)); row3.removeFromLeft (6);
        ampPanel->setBounds (row3.removeFromLeft (506)); row3.removeFromLeft (6);
        voice->setBounds (row3);
    }

private:
    void buildOsc (int o)
    {
        auto& pnl = panel (*this, juce::String ("Oscillator ") + (o == 0 ? "A" : "B"), oscId (o, "on"));
        oscPanel[o] = &pnl;
        auto& wt = view (pnl, new WavetableView (proc, o));
        auto& table = choice (pnl, oscId (o, "table"), "Wavetable");
        auto& import = add<juce::TextButton> (pnl, "+");
        import.setTooltip ("Import a wavetable or sample (WAV / AIFF / FLAC)");
        import.onClick = [this, o] { importTable (o); };
        auto& mode = choice (pnl, oscId (o, "mode"), "Mode");
        auto& route = choice (pnl, oscId (o, "route"), "Route");
        std::vector<Knob*> k1 { &knob (pnl, oscId (o, "pos"), "Position"), &knob (pnl, oscId (o, "warp"), "Warp"),
                                &knob (pnl, oscId (o, "oct"), "Octave", true), &knob (pnl, oscId (o, "semi"), "Semi", true),
                                &knob (pnl, oscId (o, "fine"), "Fine", true), &knob (pnl, oscId (o, "level"), "Level"),
                                &knob (pnl, oscId (o, "pan"), "Pan", true) };
        std::vector<Knob*> k2 { &knob (pnl, oscId (o, "voices"), "Unison"), &knob (pnl, oscId (o, "detune"), "Detune"),
                                &knob (pnl, oscId (o, "spread"), "Spread"), &knob (pnl, oscId (o, "blend"), "Blend"),
                                &knob (pnl, oscId (o, "fm"), "FM"), &knob (pnl, oscId (o, "fmratio"), "Ratio"),
                                &knob (pnl, oscId (o, "ring"), "Ring") };
        auto* pp = &pnl; auto* wtp = &wt; auto* tp = &table; auto* ip = &import; auto* mp = &mode; auto* rp = &route;
        pnl.onLayout = [pp, wtp, tp, ip, mp, rp, k1, k2]
        {
            auto c = pp->content();
            wtp->setBounds (c.removeFromLeft (212));
            c.removeFromLeft (10);
            auto top = c.removeFromTop (36);
            tp->setBounds (top.removeFromLeft (128)); top.removeFromLeft (2);
            ip->setBounds (top.removeFromLeft (22).withTrimmedTop (13)); top.removeFromLeft (6);
            mp->setBounds (top.removeFromLeft (96)); top.removeFromLeft (6);
            rp->setBounds (top);
            c.removeFromTop (6);
            const int h = c.getHeight() / 2;
            auto r1 = c.removeFromTop (h), r2 = c;
            const int w = r1.getWidth() / 7;
            for (auto* k : k1) k->setBounds (r1.removeFromLeft (w));
            for (auto* k : k2) k->setBounds (r2.removeFromLeft (w));
        };
    }

    void importTable (int o)
    {
        chooser = std::make_unique<juce::FileChooser> ("Import wavetable", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, o] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            const auto err = proc.importWavetable (o, f);
            if (err.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", err);
        });
    }

    void buildSubNoise()
    {
        auto& pnl = panel (*this, "Sub  &  Noise");
        subNoise = &pnl;
        auto& subOn = add<Toggle> (pnl, proc.apvts, "sub_on", "Sub");
        auto& subShape = choice (pnl, "sub_shape");
        auto& subOct = choice (pnl, "sub_oct");
        auto& subRoute = choice (pnl, "sub_route");
        auto& subLvl = knob (pnl, "sub_level", "Level");
        auto& nOn = add<Toggle> (pnl, proc.apvts, "noise_on", "Noise");
        auto& nType = choice (pnl, "noise_type");
        auto& nRoute = choice (pnl, "noise_route");
        auto& nCol = knob (pnl, "noise_color", "Color", true);
        auto& nLvl = knob (pnl, "noise_level", "Level");
        auto* pp = &pnl;
        juce::Component* sc[] = { &subOn, &subShape, &subOct, &subRoute, &subLvl, &nOn, &nType, &nRoute, &nCol, &nLvl };
        std::vector<juce::Component*> v (std::begin (sc), std::end (sc));
        pnl.onLayout = [pp, v]
        {
            auto c = pp->content();
            auto left = c.removeFromLeft (c.getWidth() / 2 - 6); c.removeFromLeft (12);
            auto right = c;
            // Sub: toggle / shape | oct / route + level
            v[0]->setBounds (left.removeFromTop (18)); left.removeFromTop (5);
            auto row = left.removeFromTop (22);
            v[1]->setBounds (row.removeFromLeft (row.getWidth() / 2 - 2)); row.removeFromLeft (4); v[2]->setBounds (row);
            left.removeFromTop (5);
            v[3]->setBounds (left.removeFromLeft (left.getWidth() / 2 - 2).removeFromTop (22));
            v[4]->setBounds (left.withTrimmedLeft (4));
            // Noise: toggle / type | route / colour + level
            v[5]->setBounds (right.removeFromTop (18)); right.removeFromTop (5);
            auto row2 = right.removeFromTop (22);
            v[6]->setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 2)); row2.removeFromLeft (4); v[7]->setBounds (row2);
            right.removeFromTop (5);
            layoutRow (right, { v[8], v[9] }, 0);
        };
    }

    void buildFilter (int f)
    {
        auto& pnl = panel (*this, juce::String ("Filter ") + (f == 0 ? "A" : "B"), filId (f, "on"));
        filPanel[f] = &pnl;
        auto& type = choice (pnl, filId (f, "type"));
        auto& fv = view (pnl, new FilterView (proc, f));
        auto& cut = knob (pnl, filId (f, "cutoff"), "Cutoff");
        auto& res = knob (pnl, filId (f, "reso"), "Reso");
        auto& drv = knob (pnl, filId (f, "drive"), "Drive");
        auto& mix = knob (pnl, filId (f, "mix"), "Mix");
        auto& key = knob (pnl, filId (f, "key"), "Key");
        auto* pp = &pnl; auto* tp = &type; auto* vp = &fv;
        Knob* ks[] = { &cut, &res, &drv, &mix, &key };
        std::vector<Knob*> kv (std::begin (ks), std::end (ks));
        pnl.onLayout = [pp, tp, vp, kv]
        {
            auto c = pp->content();
            auto left = c.removeFromLeft (128);
            tp->setBounds (left.removeFromTop (22)); left.removeFromTop (4);
            vp->setBounds (left);
            c.removeFromLeft (6);
            auto r1 = c.removeFromTop (c.getHeight() / 2), r2 = c;
            layoutRow (r1, { kv[0], kv[1], kv[2] }, 0);
            layoutRow (r2.withTrimmedRight (r2.getWidth() / 3), { kv[3], kv[4] }, 0);
        };
    }

    void buildRouting()
    {
        auto& pnl = panel (*this, "Routing");
        routing = &pnl;
        auto& st = choice (pnl, "filter_struct", "Filters");
        auto& fb = knob (pnl, "fb_amount", "Feedback");
        auto& tr = knob (pnl, "voice_pitch", "Transpose", true);
        fb.setTooltip ("Feedback sends the filtered voice back into Filter A (Massive-style grit).");
        auto* pp = &pnl; auto* sp = &st; auto* fp = &fb; auto* tp = &tr;
        pnl.onLayout = [pp, sp, fp, tp]
        {
            auto c = pp->content();
            sp->setBounds (c.removeFromTop (36).withWidth (110));
            c.removeFromTop (4);
            layoutRow (c, { fp, tp }, 0);
        };
    }

    void buildInserts()
    {
        auto& pnl = panel (*this, "Insert FX");
        inserts = &pnl;
        std::vector<juce::Component*> v;
        for (int k = 0; k < kNumInserts; ++k)
        {
            v.push_back (&choice (pnl, insId (k, "type"), "Insert " + juce::String (k + 1)));
            v.push_back (&choice (pnl, insId (k, "pos")));
            v.push_back (&knob (pnl, insId (k, "amount"), "Amount"));
            v.push_back (&knob (pnl, insId (k, "mix"), "Mix"));
        }
        auto* pp = &pnl;
        pnl.onLayout = [pp, v]
        {
            auto c = pp->content();
            for (int k = 0; k < kNumInserts; ++k)
            {
                auto a = c.removeFromLeft (c.getWidth() / (kNumInserts - k));
                a.removeFromRight (k == 0 ? 8 : 0);
                auto boxes = a.removeFromLeft (98);
                v[(size_t) k * 4]->setBounds (boxes.removeFromTop (35)); boxes.removeFromTop (5);
                v[(size_t) k * 4 + 1]->setBounds (boxes.removeFromTop (22));
                layoutRow (a.withTrimmedLeft (4), { v[(size_t) k * 4 + 2], v[(size_t) k * 4 + 3] }, 0);
            }
        };
    }

    void buildAmp()
    {
        auto& pnl = panel (*this, "Amp");
        ampPanel = &pnl;
        auto& ev = view (pnl, new EnvView (proc, "amp_"));
        std::vector<Knob*> ks { &knob (pnl, "amp_a", "Attack"), &knob (pnl, "amp_d", "Decay"), &knob (pnl, "amp_s", "Sustain"),
                                &knob (pnl, "amp_r", "Release"), &knob (pnl, "amp_curve", "Curve"), &knob (pnl, "amp_level", "Level"),
                                &knob (pnl, "amp_pan", "Pan", true), &knob (pnl, "amp_vel", "Velocity") };
        auto* pp = &pnl; auto* evp = &ev;
        pnl.onLayout = [pp, evp, ks]
        {
            auto c = pp->content();
            evp->setBounds (c.removeFromLeft (128));
            c.removeFromLeft (6);
            const int w = c.getWidth() / (int) ks.size();
            for (auto* k : ks) k->setBounds (c.removeFromLeft (w));
        };
    }

    void buildVoice()
    {
        auto& pnl = panel (*this, "Voice");
        voice = &pnl;
        auto& mode = choice (pnl, "voice_mode", "Mode");
        auto& quality = choice (pnl, "quality", "Quality");
        quality.box.setTooltip ("High renders voices at 2x sample rate: cleaner drive, fold and sync with no aliasing. Uses about twice the CPU.");
        auto& poly = knob (pnl, "voice_poly", "Voices");
        auto& glide = knob (pnl, "voice_glide", "Glide");
        auto& bend = knob (pnl, "voice_pbrange", "Bend");
        auto& drift = knob (pnl, "voice_drift", "Drift");
        drift.setTooltip ("Analog drift: per-voice tuning and filter variation, slow wander. Makes stacks sound alive.");
        auto* pp = &pnl; auto* mp = &mode; auto* qp = &quality; auto* a = &poly; auto* b = &glide; auto* c2 = &bend; auto* d = &drift;
        pnl.onLayout = [pp, mp, qp, a, b, c2, d]
        {
            auto c = pp->content();
            auto left = c.removeFromLeft (92);
            mp->setBounds (left.removeFromTop (36)); left.removeFromTop (4);
            qp->setBounds (left.removeFromTop (36));
            c.removeFromLeft (4);
            layoutRow (c, { a, b, c2, d }, 0);
        };
    }

    Panel* oscPanel[kNumOsc] {};
    Panel* filPanel[kNumFilters] {};
    Panel *subNoise = nullptr, *routing = nullptr, *inserts = nullptr, *ampPanel = nullptr, *voice = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ================================================================== MOD page
class ModPage : public Page
{
public:
    ModPage (LumenSynthXLProcessor& p, Tickers& t) : Page (p, t)
    {
        for (int s = 0; s < kNumModSlots; ++s)
        {
            auto& b = add<juce::TextButton> (*this);
            b.setClickingTogglesState (false);
            b.onClick = [this, s] { select (s); };
            slotButtons.push_back (&b);
        }
        addAndMakeVisible (editorArea);
        addAndMakeVisible (routeArea);
        tickers.push_back ([this] { if (gForceTick || isShowing()) tick(); });
        select (0);
    }

    void select (int s)
    {
        slot = s;
        for (int i = 0; i < kNumModSlots; ++i) slotButtons[(size_t) i]->setToggleState (i == s, juce::dontSendNotification);
        rebuild();
    }
    int selected() const { return slot; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto list = r.removeFromLeft (170);
        for (auto* b : slotButtons) { b->setBounds (list.removeFromTop (54)); list.removeFromTop (5); }
        r.removeFromLeft (10);
        routeArea.setBounds (r.removeFromRight (300));
        r.removeFromRight (10);
        editorArea.setBounds (r);
        layoutEditor();
        layoutRoutes();
    }

private:
    struct Area : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f);
            g.setColour (col::border);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
            g.setFont (labelFont (10.5f));
            g.setColour (col::text);
            g.drawText (title.toUpperCase(), getLocalBounds().reduced (12, 0).removeFromTop (26), juce::Justification::centredLeft);
        }
        juce::String title;
    };

    void tick()
    {
        for (int s = 0; s < kNumModSlots; ++s)
        {
            const int type = (int) std::lround (proc.apvts.getRawParameterValue (modId (s, "type"))->load());
            const auto txt = "MOD " + juce::String (s + 1) + "   " + modTypeNames()[type].toUpperCase();
            if (slotButtons[(size_t) s]->getButtonText() != txt) slotButtons[(size_t) s]->setButtonText (txt);
        }
        const int type = (int) std::lround (proc.apvts.getRawParameterValue (modId (slot, "type"))->load());
        int routes = 0;
        for (int r = 0; r < kNumMatrix; ++r)
            if ((int) std::lround (proc.apvts.getRawParameterValue (mtxId (r, "src"))->load()) == SrcMod1 + slot
                && (int) std::lround (proc.apvts.getRawParameterValue (mtxId (r, "dst"))->load()) > 0)
                routes |= 1 << r;
        if (type != builtType || routes != builtRoutes) rebuild();
        for (auto& f : localTickers) f();
    }

    void rebuild()
    {
        localTickers.clear();
        editorItems.clear();
        routeItems.clear();
        const int s = slot;
        builtType = (int) std::lround (proc.apvts.getRawParameterValue (modId (s, "type"))->load());
        editorArea.title = "Mod " + juce::String (s + 1) + "  /  " + modTypeNames()[builtType];
        routeArea.title = "Routed to";

        auto addE = [this] (juce::Component* c) { editorItems.add (c); editorArea.addAndMakeVisible (c); return c; };
        typeBox = static_cast<Choice*> (addE (new Choice (proc.apvts, modId (s, "type"), "Type")));
        bigView = nullptr; knobs.clear(); toggles.clear(); boxes.clear();

        auto addKnob = [&] (const char* n, const juce::String& label, bool bip = false)
        {
            auto* k = static_cast<Knob*> (addE (new Knob (proc, modId (s, n), label, bip)));
            knobs.push_back (k);
            localTickers.push_back ([k] { k->tick(); });
        };
        auto addToggle = [&] (const char* n, const juce::String& label)
        {
            toggles.push_back (static_cast<Toggle*> (addE (new Toggle (proc.apvts, modId (s, n), label))));
        };
        auto addBox = [&] (const char* n, const juce::String& label)
        {
            boxes.push_back (static_cast<Choice*> (addE (new Choice (proc.apvts, modId (s, n), label))));
        };

        if (builtType == ModEnv)
        {
            auto* v = new EnvView (proc, modId (s, ""), s);
            bigView = addE (v); localTickers.push_back ([v] { v->tick(); });
            addKnob ("a", "Attack"); addKnob ("d", "Decay"); addKnob ("s", "Sustain"); addKnob ("r", "Release"); addKnob ("curve", "Curve");
        }
        else if (builtType == ModLfo)
        {
            auto* v = new LfoView (proc, s);
            bigView = addE (v); localTickers.push_back ([v] { v->tick(); });
            addBox ("shape", "Shape"); addBox ("div", "Sync Rate");
            addToggle ("sync", "Sync"); addToggle ("retrig", "Retrigger"); addToggle ("bipolar", "Bipolar");
            addKnob ("rate", "Rate"); addKnob ("phase", "Phase");
        }
        else if (builtType == ModPerformer)
        {
            auto* v = new PerformerView (proc, s);
            bigView = addE (v); localTickers.push_back ([v] { v->tick(); });
            addBox ("div", "Step Length");
            addToggle ("sync", "Sync"); addToggle ("retrig", "Retrigger"); addToggle ("bipolar", "Bipolar");
            addKnob ("len", "Steps"); addKnob ("rate", "Rate"); addKnob ("smooth", "Glide");
        }
        else
        {
            auto* v = new LfoView (proc, s);
            bigView = addE (v); localTickers.push_back ([v] { v->tick(); });
            addBox ("div", "Sync Rate");
            addToggle ("sync", "Sync"); addToggle ("retrig", "Per Voice"); addToggle ("bipolar", "Bipolar");
            addKnob ("rate", "Rate"); addKnob ("smooth", "Smooth");
        }

        // routes
        builtRoutes = 0;
        routes.clear();
        for (int r = 0; r < kNumMatrix; ++r)
        {
            const int src = (int) std::lround (proc.apvts.getRawParameterValue (mtxId (r, "src"))->load());
            const int dst = (int) std::lround (proc.apvts.getRawParameterValue (mtxId (r, "dst"))->load());
            if (src != SrcMod1 + s || dst <= 0) continue;
            builtRoutes |= 1 << r;
            auto* lbl = new juce::Label ({}, modDests()[(size_t) dst].name);
            lbl->setFont (font (12.0f));
            lbl->setColour (juce::Label::textColourId, col::text);
            auto* sl = new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
            sl->getProperties().set ("bipolar", true);
            sl->getProperties().set ("modColour", true);
            auto* rm = new juce::TextButton ("x");
            rm->setTooltip ("Remove this modulation");
            rm->onClick = [this, dst, src] { juce::MessageManager::callAsync ([this, dst, src] { proc.removeModulation (dst, src); }); };
            for (juce::Component* c : { (juce::Component*) lbl, (juce::Component*) sl, (juce::Component*) rm }) { routeItems.add (c); routeArea.addAndMakeVisible (c); }
            routes.push_back ({ lbl, sl, rm, std::make_unique<APVTS::SliderAttachment> (proc.apvts, mtxId (r, "amt"), *sl) });
        }
        if (routes.empty())
        {
            auto* lbl = new juce::Label ({}, "Nothing yet. Drag the MOD " + juce::String (s + 1) + " chip from the bottom strip onto any knob.");
            lbl->setFont (font (12.0f));
            lbl->setJustificationType (juce::Justification::topLeft);
            lbl->setColour (juce::Label::textColourId, col::textDim);
            routeItems.add (lbl); routeArea.addAndMakeVisible (lbl);
            hint = lbl;
        }
        else hint = nullptr;

        layoutEditor();
        layoutRoutes();
        repaint();
    }

    void layoutEditor()
    {
        if (typeBox == nullptr) return;
        auto c = editorArea.getLocalBounds().reduced (12).withTrimmedTop (22);
        auto top = c.removeFromTop (36);
        typeBox->setBounds (top.removeFromLeft (130)); top.removeFromLeft (10);
        for (auto* b : boxes) { b->setBounds (top.removeFromLeft (110)); top.removeFromLeft (10); }
        top.removeFromTop (13);
        for (auto* t : toggles) { t->setBounds (top.removeFromLeft (96)); }
        c.removeFromTop (10);
        auto knobRow = c.removeFromBottom (86);
        if (bigView != nullptr) bigView->setBounds (c);
        const int w = 78;
        for (auto* k : knobs) k->setBounds (knobRow.removeFromLeft (w));
    }

    void layoutRoutes()
    {
        auto c = routeArea.getLocalBounds().reduced (12).withTrimmedTop (24);
        if (hint != nullptr) { hint->setBounds (c.removeFromTop (60)); return; }
        for (auto& r : routes)
        {
            auto row = c.removeFromTop (44);
            r.label->setBounds (row.removeFromTop (18));
            r.remove->setBounds (row.removeFromRight (22).withSizeKeepingCentre (22, 20));
            r.slider->setBounds (row);
            c.removeFromTop (6);
        }
    }

    struct Route { juce::Label* label; juce::Slider* slider; juce::TextButton* remove; std::unique_ptr<APVTS::SliderAttachment> att; };

    int slot = 0, builtType = -1, builtRoutes = -1;
    std::vector<juce::TextButton*> slotButtons;
    Area editorArea, routeArea;
    juce::OwnedArray<juce::Component> editorItems, routeItems;
    std::vector<Route> routes;
    Choice* typeBox = nullptr;
    juce::Component* bigView = nullptr;
    juce::Label* hint = nullptr;
    std::vector<Knob*> knobs;
    std::vector<Toggle*> toggles;
    std::vector<Choice*> boxes;
    Tickers localTickers;
};

// ================================================================== FX page
class FxPage : public Page
{
public:
    FxPage (LumenSynthXLProcessor& p, Tickers& t) : Page (p, t)
    {
        make ("Hyper / Dimension", "fx_hyper_on", {}, {}, { { "fx_hyper_rate", "Rate" }, { "fx_hyper_detune", "Detune" }, { "fx_hyper_dim", "Dimension" }, { "fx_hyper_mix", "Mix" } });
        make ("Distortion", "fx_dist_on", { "fx_dist_type" }, {}, { { "fx_dist_drive", "Drive" }, { "fx_dist_tone", "Tone" }, { "fx_dist_mix", "Mix" } });
        make ("OTT Multiband", "fx_ott_on", {}, {}, { { "fx_ott_depth", "Depth" }, { "fx_ott_time", "Time" }, { "fx_ott_up", "Upward" },
                                                    { "fx_ott_down", "Downward" }, { "fx_ott_gain", "Output" } });
        make ("Chorus", "fx_chorus_on", {}, {}, { { "fx_chorus_rate", "Rate" }, { "fx_chorus_depth", "Depth" }, { "fx_chorus_mix", "Mix" } });
        make ("Phaser", "fx_phaser_on", {}, {}, { { "fx_phaser_rate", "Rate" }, { "fx_phaser_depth", "Depth" }, { "fx_phaser_fb", "Feedback" }, { "fx_phaser_mix", "Mix" } });
        make ("Delay", "fx_delay_on", { "fx_delay_div" }, { { "fx_delay_sync", "Sync" }, { "fx_delay_pp", "Ping-Pong" } },
              { { "fx_delay_time", "Time" }, { "fx_delay_fb", "Feedback" }, { "fx_delay_tone", "Tone" }, { "fx_delay_mix", "Mix" } });
        make ("Reverb", "fx_rev_on", {}, {}, { { "fx_rev_size", "Size" }, { "fx_rev_decay", "Decay" }, { "fx_rev_damp", "Damp" },
                                                { "fx_rev_pre", "Pre-Delay" }, { "fx_rev_width", "Width" }, { "fx_rev_mix", "Mix" } });
        make ("EQ", "fx_eq_on", {}, {}, { { "fx_eq_hp", "Low Cut" }, { "fx_eq_low", "Low" }, { "fx_eq_midf", "Mid Freq" },
                                          { "fx_eq_mid", "Mid" }, { "fx_eq_high", "High" } }, { "fx_eq_low", "fx_eq_mid", "fx_eq_high" });
        make ("Tape", "fx_tape_on", {}, {}, { { "fx_tape_wow", "Wow" }, { "fx_tape_flutter", "Flutter" }, { "fx_tape_sat", "Saturate" }, { "fx_tape_mix", "Mix" } });
        make ("Output", {}, {}, { { "fx_clip_on", "Soft Clip" } }, { { "fx_width", "Width" }, { "fx_bassmono", "Bass Mono" } });
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        const int h = (r.getHeight() - 10) / 2;
        const int w = (r.getWidth() - 40) / 5;
        for (int i = 0; i < (int) panels.size(); ++i)
            panels[(size_t) i]->setBounds (r.getX() + (i % 5) * (w + 10), r.getY() + (i / 5) * (h + 10), w, h);
    }

private:
    void make (const juce::String& title, const juce::String& power, std::vector<juce::String> boxIds,
               std::vector<std::pair<juce::String, juce::String>> toggleIds, std::vector<std::pair<juce::String, juce::String>> knobIds,
               std::vector<juce::String> bipolarIds = {})
    {
        auto& pnl = panel (*this, title, power);
        panels.push_back (&pnl);
        std::vector<juce::Component*> top;
        for (auto& b : boxIds) top.push_back (&choice (pnl, b));
        for (auto& t : toggleIds) top.push_back (&add<Toggle> (pnl, proc.apvts, t.first, t.second));
        std::vector<Knob*> ks;
        for (auto& k : knobIds)
        {
            const bool bip = std::find (bipolarIds.begin(), bipolarIds.end(), k.first) != bipolarIds.end();
            ks.push_back (&knob (pnl, k.first, k.second, bip));
            ks.back()->maxKnobSize = 64;
        }
        auto* pp = &pnl;
        pnl.onLayout = [pp, top, ks]
        {
            auto c = pp->content().reduced (2, 2);
            if (! top.empty())
            {
                auto t = c.removeFromTop (24);
                for (auto* comp : top)
                {
                    const bool isToggle = dynamic_cast<juce::ToggleButton*> (comp) != nullptr;
                    const int w = isToggle ? 96 : 104;
                    if (t.getWidth() < w) { c.removeFromTop (4); t = c.removeFromTop (24); }
                    comp->setBounds (t.removeFromLeft (w)); t.removeFromLeft (6);
                }
                c.removeFromTop (6);
            }
            const int n = (int) ks.size();
            const int perRow = n <= 3 ? n : (n == 4 ? 2 : 3);
            const int rows = (n + perRow - 1) / perRow;
            const int kh = juce::jmin (100, c.getHeight() / juce::jmax (1, rows));
            const int kw = c.getWidth() / juce::jmax (1, perRow);
            for (int i = 0; i < n; ++i)
            {
                const int rr = i / perRow, cc = i % perRow;
                const int inRow = juce::jmin (perRow, n - rr * perRow);
                const int x0 = c.getX() + (c.getWidth() - kw * inRow) / 2;
                ks[(size_t) i]->setBounds (x0 + cc * kw, c.getY() + rr * kh, kw, kh);
            }
        };
    }
    std::vector<Panel*> panels;
};

// ================================================================== MATRIX page
class MatrixPage : public Page
{
public:
    MatrixPage (LumenSynthXLProcessor& p, Tickers& t) : Page (p, t)
    {
        for (int r = 0; r < kNumMatrix; ++r)
        {
            Row row;
            row.num = &add<juce::Label> (*this, juce::String (), juce::String (r + 1));
            row.num->setFont (labelFont (11.0f));
            row.num->setColour (juce::Label::textColourId, col::textDim);
            row.num->setJustificationType (juce::Justification::centred);
            row.src = &choice (*this, mtxId (r, "src"));
            row.dst = &choice (*this, mtxId (r, "dst"));
            row.amt = &add<juce::Slider> (*this, juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            row.amt->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
            row.amt->getProperties().set ("bipolar", true);
            row.amt->getProperties().set ("modColour", true);
            row.att = std::make_unique<APVTS::SliderAttachment> (proc.apvts, mtxId (r, "amt"), *row.amt);
            row.clear = &add<juce::TextButton> (*this, "x");
            row.clear->setTooltip ("Clear this slot");
            row.clear->onClick = [this, r]
            {
                for (const char* n : { "src", "dst", "amt" })
                    if (auto* prm = proc.apvts.getParameter (mtxId (r, n)))
                    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); }
            };
            rows.push_back (std::move (row));
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (10, 8);
        g.setColour (col::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.setFont (labelFont (10.5f));
        g.setColour (col::text);
        g.drawText ("MODULATION MATRIX", r.reduced (12, 0).removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (font (11.5f));
        g.drawText ("Tip: drag a source chip onto any knob to fill the next free slot.", r.reduced (12, 0).removeFromTop (28),
                    juce::Justification::centredRight);
        const int half = (r.getWidth() - 24) / 2;
        for (int c = 0; c < 2; ++c)
        {
            auto h = juce::Rectangle<int> (r.getX() + 12 + c * (half + 12), r.getY() + 32, half, 16);
            g.setFont (labelFont (9.5f));
            g.drawText ("SOURCE", h.withX (h.getX() + 30).withWidth (140), juce::Justification::centredLeft);
            g.drawText ("DESTINATION", h.withX (h.getX() + 178).withWidth (180), juce::Justification::centredLeft);
            g.drawText ("AMOUNT", h.withX (h.getX() + 370).withWidth (100), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8).reduced (12, 0).withTrimmedTop (52);
        const int half = (r.getWidth() - 12) / 2;
        for (int i = 0; i < kNumMatrix; ++i)
        {
            const int c = i / 8, k = i % 8;
            auto row = juce::Rectangle<int> (r.getX() + c * (half + 12), r.getY() + k * 56, half, 30);
            auto& rw = rows[(size_t) i];
            rw.num->setBounds (row.removeFromLeft (26));
            row.removeFromLeft (4);
            rw.src->setBounds (row.removeFromLeft (140)); row.removeFromLeft (8);
            rw.dst->setBounds (row.removeFromLeft (184)); row.removeFromLeft (8);
            rw.clear->setBounds (row.removeFromRight (26).reduced (0, 3));
            row.removeFromRight (6);
            rw.amt->setBounds (row);
        }
    }

private:
    struct Row { juce::Label* num; Choice* src; Choice* dst; juce::Slider* amt; juce::TextButton* clear; std::unique_ptr<APVTS::SliderAttachment> att; };
    std::vector<Row> rows;
};

// ================================================================== Content (header + pages + strip)
class Content : public juce::Component
{
public:
    Content (LumenSynthXLProcessor& p) : proc (p), meter (p)
    {
        // pages
        synth = std::make_unique<SynthPage> (p, tickers);
        mod = std::make_unique<ModPage> (p, tickers);
        fx = std::make_unique<FxPage> (p, tickers);
        matrix = std::make_unique<MatrixPage> (p, tickers);
        for (auto* pg : { (juce::Component*) synth.get(), (juce::Component*) mod.get(), (juce::Component*) fx.get(), (juce::Component*) matrix.get() })
            addChildComponent (pg);

        browser = std::make_unique<PresetBrowser> (p);
        browser->onClose = [this] { toggleBrowser(); };
        addChildComponent (*browser);

        const char* tabNames[] = { "SYNTH", "MOD", "FX", "MATRIX" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = tabs.add (new juce::TextButton (tabNames[i]));
            b->onClick = [this, i] { showPage (i); };
            addAndMakeVisible (b);
        }

        // header: presets
        for (auto* b : { &prevBtn, &nextBtn, &presetBtn, &saveBtn, &mutateBtn, &undoBtn, &redoBtn }) addAndMakeVisible (b);
        prevBtn.setButtonText ("<"); nextBtn.setButtonText (">");
        prevBtn.onClick = [this] { proc.presets.step (-1); };
        nextBtn.onClick = [this] { proc.presets.step (1); };
        presetBtn.onClick = [this] { toggleBrowser(); };
        presetBtn.setTooltip ("Open the preset browser");
        saveBtn.setButtonText ("SAVE");
        saveBtn.onClick = [this] { savePreset(); };
        mutateBtn.setButtonText ("MUTATE");
        mutateBtn.setTooltip ("Creates a variation of the current sound. The slider sets how far it moves. Undo to go back.");
        mutateBtn.onClick = [this] { proc.mutate ((float) mutateAmt.getValue()); };
        mutateAmt.setSliderStyle (juce::Slider::LinearHorizontal);
        mutateAmt.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        mutateAmt.setRange (0.05, 1.0);
        mutateAmt.setValue (0.35);
        mutateAmt.getProperties().set ("modColour", true);
        mutateAmt.setTooltip ("Mutation strength");
        addAndMakeVisible (mutateAmt);
        undoBtn.setButtonText ("UNDO"); redoBtn.setButtonText ("REDO");
        undoBtn.onClick = [this] { proc.undoManager.undo(); };
        redoBtn.onClick = [this] { proc.undoManager.redo(); };

        masterKnob = std::make_unique<Knob> (p, "master_vol", "Volume");
        addAndMakeVisible (*masterKnob);
        addAndMakeVisible (meter);
        tickers.push_back ([this] { meter.tick(); masterKnob->tick(); });

        // macros
        macroPanel = std::make_unique<Panel> ("Macros");
        addAndMakeVisible (*macroPanel);
        for (int m = 0; m < kNumMacros; ++m)
        {
            auto* k = macroKnobs.add (new Knob (p, macroId (m), "Macro " + juce::String (m + 1)));
            macroPanel->addAndMakeVisible (k);
            auto* c = chips.add (new ModChip (p, SrcMacro1 + m, "M" + juce::String (m + 1)));
            macroPanel->addAndMakeVisible (c);
        }
        macroPanel->onLayout = [this]
        {
            auto c = macroPanel->content();
            const int w = c.getWidth() / kNumMacros;
            for (int m = 0; m < kNumMacros; ++m)
            {
                auto col = c.removeFromLeft (w);
                chips[m]->setBounds (col.removeFromBottom (20).reduced (8, 0));
                macroKnobs[m]->setBounds (col);
            }
        };

        // sources
        srcPanel = std::make_unique<Panel> ("Mod Sources  -  drag onto any knob");
        addAndMakeVisible (*srcPanel);
        for (int s = 0; s < kNumModSlots; ++s)
        {
            auto* c = chips.add (new ModChip (p, SrcMod1 + s, "MOD " + juce::String (s + 1)));
            srcPanel->addAndMakeVisible (c);
            slotChips.add (c);
            c->onClick = [this, s] { mod->select (s); showPage (1); };
        }
        const std::pair<int, const char*> others[] = { { SrcVelocity, "VELOCITY" }, { SrcKeyTrack, "KEY" }, { SrcAftertouch, "PRESSURE" },
                                                       { SrcModWheel, "MOD WHEEL" }, { SrcPitchBend, "BEND" }, { SrcVoiceRandom, "RANDOM" },
                                                       { SrcAmpEnv, "AMP ENV" } };
        for (auto& o : others)
        {
            auto* c = chips.add (new ModChip (p, o.first, o.second));
            srcPanel->addAndMakeVisible (c);
            otherChips.add (c);
        }
        srcPanel->onLayout = [this]
        {
            auto c = srcPanel->content();
            auto r1 = c.removeFromTop (c.getHeight() / 2).reduced (0, 3);
            auto r2 = c.reduced (0, 3);
            const int w1 = r1.getWidth() / kNumModSlots;
            for (auto* ch : slotChips) ch->setBounds (r1.removeFromLeft (w1).reduced (2, 0));
            const int w2 = r2.getWidth() / otherChips.size();
            for (auto* ch : otherChips) ch->setBounds (r2.removeFromLeft (w2).reduced (2, 0));
        };
        for (auto* ch : chips) { auto* cp = ch; tickers.push_back ([cp] { cp->tick(); }); }

        keyboard = std::make_unique<juce::MidiKeyboardComponent> (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
        keyboard->setAvailableRange (24, 108);
        keyboard->setLowestVisibleKey (24);
        keyboard->setScrollButtonsVisible (false);
        keyboard->setWantsKeyboardFocus (false);
        addAndMakeVisible (*keyboard);

        undoListener.proc = &proc;
        addMouseListener (&undoListener, true);
        showPage (0);
    }

    ~Content() override { removeMouseListener (&undoListener); }

    void toggleBrowser()
    {
        browser->setVisible (! browser->isVisible());
        presetBtn.setToggleState (browser->isVisible(), juce::dontSendNotification);
    }

    void showPage (int i)
    {
        if (browser != nullptr && browser->isVisible()) toggleBrowser();
        page = i;
        synth->setVisible (i == 0); mod->setVisible (i == 1); fx->setVisible (i == 2); matrix->setVisible (i == 3);
        for (int t = 0; t < tabs.size(); ++t) tabs[t]->setToggleState (t == i, juce::dontSendNotification);
    }

    void selectModSlot (int s) { mod->select (s); }

    void tick()
    {
        for (auto& f : tickers) f();
        const auto name = proc.presets.currentName();
        if (presetBtn.getButtonText() != name)
        {
            presetBtn.setButtonText (name);
            presetInfo = proc.presets.currentInfo();
            repaint (presetBtn.getBounds().translated (0, 20).expanded (40, 0));
        }
        for (int m = 0; m < kNumMacros; ++m) macroKnobs[m]->setLabel (proc.macroName (m));
        undoBtn.setEnabled (proc.undoManager.canUndo());
        redoBtn.setEnabled (proc.undoManager.canRedo());
        for (int s = 0; s < kNumModSlots; ++s)
        {
            const int type = (int) std::lround (proc.apvts.getRawParameterValue (modId (s, "type"))->load());
            static const char* shortT[] = { "ENV", "LFO", "PERF", "RND" };
            const juce::String txt = juce::String (shortT[juce::jlimit (0, 3, type)]) + " " + juce::String (s + 1);
            slotChips[s]->selected = page == 1 && mod->selected() == s;
            if (slotChips[s]->text != txt) { slotChips[s]->text = txt; slotChips[s]->repaint(); }
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        auto head = getLocalBounds().removeFromTop (56);
        g.setGradientFill (juce::ColourGradient (col::bg2, 0, 0, col::bg, 0, 56, false));
        g.fillRect (head);
        g.setColour (col::border);
        g.fillRect (0, 55, getWidth(), 1);

        // logo
        auto logo = head.reduced (16, 0).removeFromLeft (190);
        g.setColour (col::accent);
        g.fillEllipse ((float) logo.getX(), (float) logo.getCentreY() - 7.0f, 14.0f, 14.0f);
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillEllipse ((float) logo.getX() - 4.0f, (float) logo.getCentreY() - 11.0f, 22.0f, 22.0f);
        g.setColour (col::text);
        g.setFont (font (20.0f, true).withExtraKerningFactor (0.12f));
        g.drawText ("LUMEN", logo.withTrimmedLeft (24), juce::Justification::centredLeft);
        g.setColour (col::mod);
        g.setFont (font (11.0f, true).withExtraKerningFactor (0.2f));
        g.drawText ("SYNTH XL", logo.withTrimmedLeft (104).withTrimmedTop (4), juce::Justification::centredLeft);
        if (presetInfo.isNotEmpty())
        {
            g.setFont (font (9.5f));
            g.setColour (col::textDim);
            g.drawText (presetInfo.toUpperCase(), presetBtn.getBounds().withY (presetBtn.getBottom() + 1).withHeight (11), juce::Justification::centred);
        }

    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto head = r.removeFromTop (56).reduced (12, 12);
        head.removeFromLeft (200);
        for (auto* t : tabs) { t->setBounds (head.removeFromLeft (70)); head.removeFromLeft (4); }
        head.removeFromLeft (20);
        prevBtn.setBounds (head.removeFromLeft (26)); head.removeFromLeft (3);
        presetBtn.setBounds (head.removeFromLeft (220).withTrimmedBottom (9).translated (0, -3)); head.removeFromLeft (3);
        nextBtn.setBounds (head.removeFromLeft (26)); head.removeFromLeft (6);
        saveBtn.setBounds (head.removeFromLeft (50)); head.removeFromLeft (14);
        mutateBtn.setBounds (head.removeFromLeft (70)); head.removeFromLeft (4);
        mutateAmt.setBounds (head.removeFromLeft (56)); head.removeFromLeft (12);
        undoBtn.setBounds (head.removeFromLeft (48)); head.removeFromLeft (3);
        redoBtn.setBounds (head.removeFromLeft (48));
        auto hr = getLocalBounds().removeFromTop (56);
        meter.setBounds (hr.removeFromRight (24).reduced (5, 10));
        masterKnob->setBounds (hr.removeFromRight (56).reduced (0, 2));

        auto keys = r.removeFromBottom (80).reduced (12, 6);
        keyboard->setBounds (keys);
        keyboard->setKeyWidth ((float) keys.getWidth() / 50.0f);
        auto strip = r.removeFromBottom (112).reduced (10, 4);
        macroPanel->setBounds (strip.removeFromLeft (500));
        strip.removeFromLeft (10);
        srcPanel->setBounds (strip);

        for (auto* pg : { (juce::Component*) synth.get(), (juce::Component*) mod.get(), (juce::Component*) fx.get(), (juce::Component*) matrix.get() })
            pg->setBounds (r);
        browser->setBounds (r);
    }

private:
    struct UndoListener : juce::MouseListener
    {
        LumenSynthXLProcessor* proc = nullptr;
        void mouseDown (const juce::MouseEvent&) override { if (proc != nullptr) proc->undoManager.beginNewTransaction(); }
    } undoListener;

    void savePreset()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Save the current sound as a user preset.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", proc.presets.currentName(), "Name");
        auto cats = PresetManager::categories();
        w->addComboBox ("cat", cats, "Category");
        const auto curCat = proc.presets.entries()[juce::jmax (0, proc.presets.currentIndex())].category;
        if (auto* cb = w->getComboBoxComponent ("cat")) cb->setSelectedItemIndex (juce::jmax (0, cats.indexOf (curCat)));
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1)
            {
                const auto name = w->getTextEditorContents ("name");
                const auto cat = w->getComboBoxComponent ("cat")->getText();
                if (! proc.presets.saveUser (name, cat, proc.fullState()))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", "Could not write the preset file.");
            }
        }), true);
    }

    LumenSynthXLProcessor& proc;
    Tickers tickers;
    std::unique_ptr<SynthPage> synth;
    std::unique_ptr<ModPage> mod;
    std::unique_ptr<FxPage> fx;
    std::unique_ptr<MatrixPage> matrix;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::TextButton prevBtn, nextBtn, presetBtn, saveBtn, mutateBtn, undoBtn, redoBtn;
    juce::Slider mutateAmt;
    std::unique_ptr<Knob> masterKnob;
    Meter meter;
    std::unique_ptr<Panel> macroPanel, srcPanel;
    juce::OwnedArray<Knob> macroKnobs;
    juce::OwnedArray<ModChip> chips;
    juce::Array<ModChip*> slotChips, otherChips;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard;
    std::unique_ptr<PresetBrowser> browser;
    juce::String presetInfo;
    int page = 0, voicesShown = 0;
};
} // namespace lxl::ui

namespace lxl
{
PluginEditor::PluginEditor (LumenSynthXLProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<ui::Content> (p);
    content->setLookAndFeel (&lnf);
    content->setBounds (0, 0, kBaseW, kBaseH);
    addAndMakeVisible (*content);
    setResizable (true, true);
    setResizeLimits (kBaseW * 6 / 10, kBaseH * 6 / 10, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW, kBaseH);
    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    content->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void PluginEditor::paint (juce::Graphics& g) { g.fillAll (ui::col::bg); }

void PluginEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (s));
}

void PluginEditor::timerCallback() { content->tick(); }

void PluginEditor::showPageForTest (int page)
{
    ui::gForceTick = true;
    if (page >= 10) { content->selectModSlot (page - 10); page = 1; }
    content->showPage (page == 4 ? 0 : page);
    if (page == 4) content->toggleBrowser();
    content->tick();
}
} // namespace lxl
