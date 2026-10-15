#include "PluginEditor.h"
#include "dsp/Wavetables.h"
#include <complex>

using namespace lumen::led;
using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
inline bool differs (float a, float b) { return std::abs (a - b) > 1.0e-7f; }
float pv (APVTS& s, const char* id) { return s.getRawParameterValue (id)->load(); }
float pv (APVTS& s, const juce::String& id) { return s.getRawParameterValue (id)->load(); }

//==============================================================================
// 3D wavetable: all 16 frames stacked in depth, the current (morphed + warped) frame lit up.
class WavetableView : public juce::Component
{
public:
    WavetableView (APVTS& s, juce::String prefix, juce::Colour c) : st (s), p (std::move (prefix)), col (c) {}
    bool changed()
    {
        const float sig = pv (st, p + "_table") * 1000.0f + pv (st, p + "_pos") * 100.0f + pv (st, p + "_warpm") * 7.0f + pv (st, p + "_warp") * 3.0f + pv (st, p + "_on");
        if (! differs (sig, last)) return false;
        last = sig; return true;
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = pv (st, p + "_on") > 0.5f;
        lcd (g, r, col);
        const auto& bank = lumen::WavetableBank::get();
        const int table = (int) pv (st, p + "_table");
        const float pos = pv (st, p + "_pos");
        const int wm = (int) pv (st, p + "_warpm");
        const float warp = pv (st, p + "_warp");
        const int F = lumen::WavetableBank::kFrames, P = 72;
        auto area = r.reduced (10.0f, 8.0f);
        const float depthX = area.getWidth() * 0.22f, depthY = area.getHeight() * 0.32f;
        const float w = area.getWidth() - depthX, h = area.getHeight() - depthY;
        auto framePath = [&] (int f, float morph, bool warped)
        {
            juce::Path path;
            const float dz = (float) f / (float) (F - 1);
            const float ox = area.getX() + depthX * (1.0f - dz), oy = area.getY() + depthY * dz;
            for (int i = 0; i <= P; ++i)
            {
                float ph = (float) i / (float) P;
                if (warped)
                {
                    switch (wm)
                    {
                        case 1: ph *= 1.0f + 7.0f * warp; ph -= std::floor (ph); break;
                        case 2: { const float a = 8.0f * warp; ph = ph * (1.0f + a) / (1.0f + a * ph); break; }
                        case 3: { const float sw = 0.5f - 0.48f * warp; ph = ph < sw ? 0.5f * ph / sw : 0.5f + 0.5f * (ph - sw) / (1.0f - sw); break; }
                        default: break;
                    }
                }
                const float v = morph >= 0.0f ? bank.read (table, morph, 3, ph) : bank.frame (table, f, 3)[(int) (ph * 2047.0f)];
                const float x = ox + w * (float) i / (float) P;
                const float y = oy + h * 0.5f - v * h * 0.46f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            return path;
        };
        for (int f = F - 1; f >= 0; --f)
        {
            const float dist = std::abs ((float) f / (F - 1) - pos);
            g.setColour (col.withAlpha ((on ? 0.17f : 0.07f) + (on ? 0.25f : 0.0f) * juce::jmax (0.0f, 1.0f - dist * 5.0f)));
            g.strokePath (framePath (f, -1.0f, false), juce::PathStrokeType (0.9f));
        }
        // current frame at its depth position
        const int fcur = juce::jlimit (0, F - 1, (int) std::round (pos * (F - 1)));
        auto cur = framePath (fcur, pos, true);
        if (on)
        {
            const float dz = (float) fcur / (float) (F - 1);
            const float base = area.getY() + depthY * dz + h * 0.5f;
            juce::Path fill (cur);
            fill.lineTo (area.getX() + depthX * (1.0f - dz) + w, base); fill.lineTo (area.getX() + depthX * (1.0f - dz), base); fill.closeSubPath();
            juce::ColourGradient gr (col.withAlpha (0.28f), 0, base - h * 0.5f, col.withAlpha (0.0f), 0, base + h * 0.5f, false);
            g.setGradientFill (gr); g.fillPath (fill);
            glowPath (g, cur, col, 1.6f);
        }
        else glowPath (g, cur, C::dim, 1.0f, 0.5f);
        g.setFont (font (9.5f)); g.setColour (col.withAlpha (on ? 0.7f : 0.3f));
        g.drawText ("FRAME " + juce::String (fcur + 1) + "/16", r.reduced (8.0f, 4.0f), juce::Justification::topRight);
    }
private:
    APVTS& st; juce::String p; juce::Colour col; float last = -1.0f;
};

//==============================================================================
class FilterView : public juce::Component
{
public:
    explicit FilterView (APVTS& s) : st (s) {}
    bool changed()
    {
        const float sig = pv (st, "flt_type") * 7.0f + pv (st, "flt_cut") + pv (st, "flt_res") * 13.0f + pv (st, "flt_on") * 3.0f + pv (st, "flt_drive") * 5.0f;
        if (! differs (sig, last)) return false;
        last = sig;
        return true;
    }
    static float magDb (int type, float f, float fc, float res)
    {
        using cd = std::complex<double>;
        const double w = f / fc;
        const cd s (0.0, w);
        const double r = juce::jlimit (0.0f, 1.0f, res);
        const double ks = 2.0 - 1.96 * std::pow (r, 0.7);
        const double k = 3.95 * r * (0.75 + 0.25 * r);
        cd H;
        const cd den = s * s + ks * s + 1.0;
        switch (type)
        {
            case 0:  H = (1.0 + 0.55 * k) / (std::pow (1.0 + s, 4.0) + k); break;
            case 1:  H = 1.0 / den; break;
            case 2:  H = 1.0 / (den * den); break;
            case 3:  H = s * s / den; break;
            case 4:  H = (s * s / den) * (s * s / den); break;
            case 5:  H = ks * s / den; break;
            default: H = (s * s + 1.0) / den; break;
        }
        return (float) (20.0 * std::log10 (std::abs (H) + 1e-9));
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto col = C::amber;
        lcd (g, r, col);
        auto a = r.reduced (8.0f, 8.0f);
        dotGrid (g, a, 6.0f, col, 0.08f);
        g.setColour (col.withAlpha (0.12f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = a.getX() + a.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        }
        const bool on = pv (st, "flt_on") > 0.5f;
        const int type = (int) pv (st, "flt_type");
        const float fc = pv (st, "flt_cut"), res = pv (st, "flt_res");
        auto yOf = [&] (float db) { return a.getY() + a.getHeight() * (1.0f - (juce::jlimit (-42.0f, 24.0f, db) + 42.0f) / 66.0f); };
        juce::Path p;
        for (int i = 0; i <= 160; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 160.0f);
            const float db = on ? magDb (type, f, fc, res) : 0.0f;
            const float x = a.getX() + a.getWidth() * (float) i / 160.0f;
            if (i == 0) p.startNewSubPath (x, yOf (db)); else p.lineTo (x, yOf (db));
        }
        juce::Path fill (p); fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
        juce::ColourGradient gr (col.withAlpha (on ? 0.3f : 0.08f), 0, a.getY(), col.withAlpha (0.0f), 0, a.getBottom(), false);
        g.setGradientFill (gr); g.fillPath (fill);
        glowPath (g, p, on ? col : C::dim, 1.6f, on ? 1.0f : 0.5f);
        if (on)
        {
            const float x = a.getX() + a.getWidth() * std::log (fc / 20.0f) / std::log (1000.0f);
            drawLed (g, { x, yOf (magDb (type, fc, fc, res)) }, 3.5f, juce::Colours::white.interpolatedWith (col, 0.4f), 1.0f);
        }
        g.setFont (font (9.5f)); g.setColour (col.withAlpha (0.7f));
        g.drawText (formatValue (*lumen::synth::findSpec ("flt_cut"), fc), r.reduced (8.0f, 4.0f), juce::Justification::topRight);
    }
private:
    APVTS& st; float last = -1.0f;
};

//==============================================================================
class EnvView : public juce::Component
{
public:
    EnvView (APVTS& s, int index, juce::Colour c) : st (s), e (index), col (c) {}
    bool changed()
    {
        const juce::String p = "e" + juce::String (e) + "_";
        const float sig = pv (st, p + "a") * 1.0f + pv (st, p + "d") * 3.1f + pv (st, p + "s") * 7.3f + pv (st, p + "r") * 11.7f;
        if (! differs (sig, last)) return false;
        last = sig;
        return true;
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        lcd (g, r, col);
        auto a = r.reduced (8.0f, 10.0f);
        dotGrid (g, a, 6.0f, col, 0.07f);
        const juce::String p = "e" + juce::String (e) + "_";
        const float A = pv (st, p + "a"), D = pv (st, p + "d"), S = pv (st, p + "s"), R = pv (st, p + "r");
        auto tw = [] (float t) { return std::sqrt (juce::jmax (0.0f, t)) + 0.04f; };
        const float total = tw (A) + tw (D) + 0.5f + tw (R);
        const float sx = a.getWidth() / total;
        float x = a.getX();
        juce::Path pth; pth.startNewSubPath (x, a.getBottom());
        // attack (convex), decay & release (exponential look)
        const int N = 16;
        for (int i = 1; i <= N; ++i) { const float t = (float) i / N; pth.lineTo (x + tw (A) * sx * t, a.getBottom() - a.getHeight() * (1.0f - std::pow (1.0f - t, 1.8f))); }
        x += tw (A) * sx;
        for (int i = 1; i <= N; ++i) { const float t = (float) i / N; const float v = S + (1.0f - S) * std::exp (-4.0f * t); pth.lineTo (x + tw (D) * sx * t, a.getBottom() - a.getHeight() * v); }
        x += tw (D) * sx;
        pth.lineTo (x + 0.5f * sx, a.getBottom() - a.getHeight() * S);
        x += 0.5f * sx;
        for (int i = 1; i <= N; ++i) { const float t = (float) i / N; const float v = S * std::exp (-5.0f * t) * (1.0f - t * 0.05f); pth.lineTo (x + tw (R) * sx * t, a.getBottom() - a.getHeight() * v); }
        juce::Path fill (pth); fill.lineTo (a.getRight(), a.getBottom()); fill.closeSubPath();
        juce::ColourGradient gr (col.withAlpha (0.28f), 0, a.getY(), col.withAlpha (0.0f), 0, a.getBottom(), false);
        g.setGradientFill (gr); g.fillPath (fill);
        glowPath (g, pth, col, 1.5f);
        // LED nodes at the stage corners
        float nx = a.getX() + tw (A) * sx;
        drawLed (g, { nx, a.getY() }, 2.6f, col, 1.0f);
        nx += tw (D) * sx; drawLed (g, { nx, a.getBottom() - a.getHeight() * S }, 2.6f, col, 1.0f);
        nx += 0.5f * sx;   drawLed (g, { nx, a.getBottom() - a.getHeight() * S }, 2.6f, col, 1.0f);
    }
private:
    APVTS& st; int e; juce::Colour col; float last = -1.0f;
};

//==============================================================================
class LfoView : public juce::Component
{
public:
    LfoView (APVTS& s, LumenSynthProcessor& pr, int index, juce::Colour c) : st (s), proc (pr), l (index), col (c) {}
    void advance (double dt)
    {
        const juce::String p = "l" + juce::String (l) + "_";
        const double hz = pv (st, p + "sync") > 0.5f ? proc.hostBpm.load() / 60.0 / lumen::synth::divBeats ((int) pv (st, p + "div")) : pv (st, p + "rate");
        phase += hz * dt; phase -= std::floor (phase);
        if (phase < lastPh) { shA = shB; shB = rnd.nextFloat() * 2.0f - 1.0f; }
        lastPh = phase;
    }
    float value() const { return lumen::synth::lfoShape ((int) pv (st, "l" + juce::String (l) + "_shape"), (float) phase, shA, shB); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        lcd (g, r, col);
        auto a = r.reduced (10.0f, 12.0f);
        dotGrid (g, a, 6.0f, col, 0.07f);
        g.setColour (col.withAlpha (0.15f)); g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
        const int shape = (int) pv (st, "l" + juce::String (l) + "_shape");
        juce::Path pth;
        juce::Random rr (77);
        float h0 = rr.nextFloat() * 2 - 1, h1 = rr.nextFloat() * 2 - 1;
        const int N = 120;
        for (int i = 0; i <= N; ++i)
        {
            const float ph = (float) i / N * 2.0f;          // two cycles
            const float fr = ph - std::floor (ph);
            if (i > 0 && fr < ((float) (i - 1) / N * 2.0f) - std::floor ((float) (i - 1) / N * 2.0f)) { h0 = h1; h1 = rr.nextFloat() * 2 - 1; }
            const float v = lumen::synth::lfoShape (shape, juce::jmin (fr, 0.9999f), h0, h1);
            const float x = a.getX() + a.getWidth() * (float) i / N, y = a.getCentreY() - v * a.getHeight() * 0.45f;
            if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        glowPath (g, pth, col, 1.5f);
        const float v = value();
        const float x = a.getX() + a.getWidth() * (float) phase * 0.5f, y = a.getCentreY() - v * a.getHeight() * 0.45f;
        drawLed (g, { x, y }, 3.6f, juce::Colours::white.interpolatedWith (col, 0.3f), 1.0f);
        // value bar on the right
        auto bar = r.removeFromRight (6.0f).reduced (1.0f, 10.0f);
        g.setColour (col.withAlpha (0.1f)); g.fillRect (bar);
        const float bh = bar.getHeight() * 0.5f * std::abs (v);
        g.setColour (col.withAlpha (0.8f));
        g.fillRect (v > 0 ? bar.withTop (bar.getCentreY() - bh).withBottom (bar.getCentreY()) : bar.withTop (bar.getCentreY()).withHeight (bh));
    }
private:
    APVTS& st; LumenSynthProcessor& proc; int l; juce::Colour col;
    double phase = 0, lastPh = 0; float shA = 0, shB = 0.5f; juce::Random rnd;
};

//==============================================================================
template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}

//==============================================================================
class OscPage : public Page
{
public:
    OscPage (APVTS& s) : st (s)
    {
        for (int o = 0; o < 2; ++o)
        {
            const juce::String p = o == 0 ? "a" : "b";
            const auto col = o == 0 ? C::cyan : C::magenta;
            const int x0 = o * 308;
            auto* pw = add<Toggle> (*this, owned, st, p + "_on", "", col, true); pw->setBounds (x0 + 6, 3, 20, 20);
            add<Combo> (*this, owned, st, p + "_table", col)->setBounds (x0 + 92, 4, 118, 20);
            add<Combo> (*this, owned, st, p + "_warpm", col)->setBounds (x0 + 214, 4, 80, 20);
            wt[o] = add<WavetableView> (*this, owned, st, p, col); wt[o]->setBounds (x0 + 8, 30, 284, 86);
            const char* ids[3][4] = { { "_pos", "_warp", "_level", "_pan" }, { "_uni", "_det", "_blend", "_width" }, { "_oct", "_semi", "_fine", "_rand" } };
            const char* lbl[3][4] = { { "POSITION", "WARP", "LEVEL", "PAN" }, { "UNISON", "DETUNE", "BLEND", "WIDTH" }, { "OCTAVE", "SEMI", "FINE", "PHASE" } };
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 4; ++c)
                {
                    const juce::String id = p + ids[row][c];
                    const bool bip = id.endsWith ("_pan") || id.endsWith ("_oct") || id.endsWith ("_semi") || id.endsWith ("_fine");
                    add<Knob> (*this, owned, st, id, lbl[row][c], col, bip)->setBounds (x0 + 8 + c * 71, 122 + row * 96, 71, 90);
                }
        }
        add<Toggle> (*this, owned, st, "sub_on", "", C::violet, true)->setBounds (622, 3, 20, 20);
        add<Combo> (*this, owned, st, "sub_shape", C::violet)->setBounds (624, 32, 116, 22);
        add<Combo> (*this, owned, st, "sub_oct", C::violet)->setBounds (624, 60, 116, 22);
        add<Knob> (*this, owned, st, "sub_level", "LEVEL", C::violet)->setBounds (638, 92, 88, 100);
        add<Toggle> (*this, owned, st, "noise_on", "", C::blue, true)->setBounds (622, 211, 20, 20);
        add<Knob> (*this, owned, st, "noise_level", "LEVEL", C::blue)->setBounds (620, 250, 62, 86);
        add<Knob> (*this, owned, st, "noise_color", "COLOR", C::blue, true)->setBounds (684, 250, 62, 86);
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 300, 412 }, "OSC A", C::cyan, pv (st, "a_on") > 0.5f);
        panel (g, { 308, 0, 300, 412 }, "OSC B", C::magenta, pv (st, "b_on") > 0.5f);
        panel (g, { 616, 0, 132, 200 }, "SUB", C::violet, pv (st, "sub_on") > 0.5f);
        panel (g, { 616, 208, 132, 204 }, "NOISE", C::blue, pv (st, "noise_on") > 0.5f);
        // noise LED spectrum strip (purely decorative of the colour setting)
        const float colr = pv (st, "noise_color");
        for (int i = 0; i < 14; ++i)
        {
            const float t = (float) i / 13.0f;
            const float lv = juce::jlimit (0.05f, 1.0f, 0.55f + (colr - 0.5f) * 1.6f * (t - 0.5f) * 2.0f);
            drawLed (g, { 628.0f + i * 8.0f, 352.0f }, 2.2f, C::blue, pv (st, "noise_on") > 0.5f ? lv : 0.0f);
        }
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText ("SPECTRUM", 620, 362, 124, 12, juce::Justification::centred);
    }
    void tick() override
    {
        for (auto* w : wt) if (w->changed()) w->repaint();
        const float sig = pv (st, "a_on") + 2 * pv (st, "b_on") + 4 * pv (st, "sub_on") + 8 * pv (st, "noise_on") + 16 * pv (st, "noise_color");
        if (differs (sig, lastSig)) { lastSig = sig; repaint(); }
    }
private:
    APVTS& st;
    std::vector<std::unique_ptr<juce::Component>> owned;
    WavetableView* wt[2] {};
    float lastSig = -1;
};

//==============================================================================
class FilterPage : public Page
{
public:
    FilterPage (APVTS& s) : st (s)
    {
        add<Toggle> (*this, owned, st, "flt_on", "", C::amber, true)->setBounds (6, 3, 20, 20);
        add<Combo> (*this, owned, st, "flt_type", C::amber)->setBounds (226, 4, 126, 20);
        fv = add<FilterView> (*this, owned, st); fv->setBounds (8, 30, 344, 112);
        const char* ids[] = { "flt_cut", "flt_res", "flt_drive", "flt_env", "flt_key" };
        const char* lbl[] = { "CUTOFF", "RESO", "DRIVE", "ENV AMT", "KEYTRK" };
        for (int i = 0; i < 5; ++i) add<Knob> (*this, owned, st, ids[i], lbl[i], C::amber, i == 3)->setBounds (8 + i * 69, 148, 69, 96);

        const char* vids[] = { "voices", "glide", "bend", "velsens" };
        const char* vl[] = { "VOICES", "GLIDE", "BEND", "VELOCITY" };
        for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, vids[i], vl[i], C::blue)->setBounds (8 + i * 69, 288, 69, 96);
        add<Toggle> (*this, owned, st, "legato", "LEGATO", C::blue)->setBounds (284, 320, 68, 24);

        const juce::Colour ec[3] = { C::green, C::amber, C::magenta };
        for (int e = 0; e < 3; ++e)
        {
            const int y0 = e * 139;
            ev[e] = add<EnvView> (*this, owned, st, e + 1, ec[e]); ev[e]->setBounds (376, y0 + 28, 148, 98);
            const juce::String p = "e" + juce::String (e + 1) + "_";
            const char* k[] = { "a", "d", "s", "r" };
            const char* kl[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
            for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, p + k[i], kl[i], ec[e])->setBounds (530 + i * 54, y0 + 32, 54, 90);
        }
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 360, 252 }, "FILTER", C::amber, pv (st, "flt_on") > 0.5f);
        panel (g, { 0, 260, 360, 152 }, "VOICE", C::blue);
        panel (g, { 368, 0, 380, 134 }, "ENV 1  -  AMP", C::green);
        panel (g, { 368, 139, 380, 134 }, "ENV 2  -  FILTER", C::amber);
        panel (g, { 368, 278, 380, 134 }, "ENV 3  -  MOD", C::magenta);
    }
    void tick() override
    {
        if (fv->changed()) fv->repaint();
        for (auto* e : ev) if (e->changed()) e->repaint();
        const float sig = pv (st, "flt_on");
        if (differs (sig, lastSig)) { lastSig = sig; repaint(); }
    }
private:
    APVTS& st;
    std::vector<std::unique_ptr<juce::Component>> owned;
    FilterView* fv = nullptr; EnvView* ev[3] {};
    float lastSig = -1;
};

//==============================================================================
class ModPage : public Page
{
public:
    ModPage (APVTS& s, LumenSynthProcessor& pr) : st (s)
    {
        for (int l = 0; l < 2; ++l)
        {
            const int x0 = l * 378;
            const juce::String p = "l" + juce::String (l + 1) + "_";
            const auto col = l == 0 ? C::blue : C::violet;
            add<Combo> (*this, owned, st, p + "shape", col)->setBounds (x0 + 80, 4, 124, 20);
            lv[l] = add<LfoView> (*this, owned, st, pr, l + 1, col); lv[l]->setBounds (x0 + 8, 30, 206, 138);
            rate[l] = add<Knob> (*this, owned, st, p + "rate", "RATE", col); rate[l]->setBounds (x0 + 220, 30, 70, 96);
            div[l] = add<Combo> (*this, owned, st, p + "div", col); div[l]->setBounds (x0 + 220, 140, 142, 22);
            add<Toggle> (*this, owned, st, p + "sync", "SYNC", col)->setBounds (x0 + 294, 42, 68, 22);
            add<Toggle> (*this, owned, st, p + "retrig", "RETRIG", col)->setBounds (x0 + 294, 72, 68, 22);
        }
        for (int m = 0; m < 8; ++m)
        {
            const juce::String p = "m" + juce::String (m + 1) + "_";
            const int y = 214 + m * 24;
            add<Combo> (*this, owned, st, p + "src", C::cyan)->setBounds (34, y, 176, 20);
            add<Combo> (*this, owned, st, p + "dst", C::magenta)->setBounds (240, y, 176, 20);
            auto* sl = add<juce::Slider> (*this, owned);
            sl->setSliderStyle (juce::Slider::LinearHorizontal); sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            sl->setColour (juce::Slider::trackColourId, C::green);
            sl->setDoubleClickReturnValue (true, 0.0);
            sl->setBounds (426, y, 254, 20);
            amtAtt[m] = std::make_unique<APVTS::SliderAttachment> (st, p + "amt", *sl);
            auto* lb = add<juce::Label> (*this, owned);
            lb->setFont (mono (11.0f)); lb->setColour (juce::Label::textColourId, C::green); lb->setJustificationType (juce::Justification::centredRight);
            lb->setBounds (682, y, 58, 20);
            sl->onValueChange = [sl, lb] { const double v = sl->getValue(); lb->setText ((v > 0 ? "+" : "") + juce::String (v * 100.0, 0) + "%", juce::dontSendNotification); };
            sl->onValueChange();
        }
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 370, 176 }, "LFO 1", C::blue);
        panel (g, { 378, 0, 370, 176 }, "LFO 2", C::violet);
        drawLed (g, { 16.0f, 13.0f }, 3.4f, C::blue, 0.5f + 0.5f * lv[0]->value());
        drawLed (g, { 394.0f, 13.0f }, 3.4f, C::violet, 0.5f + 0.5f * lv[1]->value());
        panel (g, { 0, 184, 748, 228 }, "MOD MATRIX", C::green);
        g.setFont (font (10.0f)); g.setColour (C::dim);
        g.drawText ("SOURCE", 34, 190, 176, 16, juce::Justification::centredRight);
        g.drawText ("DESTINATION", 240, 190, 176, 16, juce::Justification::centredLeft);
        g.drawText ("AMOUNT", 426, 190, 254, 16, juce::Justification::centred);
        for (int m = 0; m < 8; ++m)
        {
            const int y = 214 + m * 24;
            const bool active = pv (st, "m" + juce::String (m + 1) + "_src") > 0.5f && pv (st, "m" + juce::String (m + 1) + "_dst") > 0.5f
                                && std::abs (pv (st, "m" + juce::String (m + 1) + "_amt")) > 1.0e-4f;
            drawLed (g, { 16.0f, y + 10.0f }, 3.4f, C::green, active ? 1.0f : 0.0f);
            g.setColour (C::dim); g.setFont (font (12.0f));
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"), 212, y, 26, 20, juce::Justification::centred);
        }
    }
    void tick() override
    {
        const double dt = 1.0 / 30.0;
        for (int l = 0; l < 2; ++l)
        {
            lv[l]->advance (dt); lv[l]->repaint(); repaint (l * 378 + 4, 2, 26, 24);
            const bool sync = pv (st, "l" + juce::String (l + 1) + "_sync") > 0.5f;
            rate[l]->setAlpha (sync ? 0.35f : 1.0f); div[l]->setAlpha (sync ? 1.0f : 0.35f);
        }
        float sig = 0; for (int m = 1; m <= 8; ++m) sig += (pv (st, "m" + juce::String (m) + "_src") * 3.0f + pv (st, "m" + juce::String (m) + "_dst")) * (float) m + (std::abs (pv (st, "m" + juce::String (m) + "_amt")) > 1e-4f ? 0.37f * m : 0.0f);
        if (differs (sig, lastSig)) { lastSig = sig; repaint (0, 184, 40, 228); }
    }
private:
    APVTS& st;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<APVTS::SliderAttachment> amtAtt[8];
    LfoView* lv[2] {}; Knob* rate[2] {}; Combo* div[2] {};
    float lastSig = -1;
};

//==============================================================================
class FxPage : public Page
{
public:
    FxPage (APVTS& s, LumenSynthProcessor& pr) : st (s), proc (pr)
    {
        auto cell = [] (int i) { return juce::Point<int> ((i % 4) * 189, (i / 4) * 210); };
        struct M { const char* on; juce::Colour col; };
        const M mods[] = { { "dist_on", C::orange }, { "ott_on", C::amber }, { "cho_on", C::cyan }, { "dly_on", C::blue },
                           { "rev_on", C::violet }, { "pump_on", C::magenta } };
        for (int i = 0; i < 6; ++i) { auto p = cell (i); add<Toggle> (*this, owned, st, mods[i].on, "", mods[i].col, true)->setBounds (p.x + 6, p.y + 3, 20, 20); }
        auto knob = [&] (int c, const char* id, const char* l, juce::Colour col, int x, int y, int w = 58, int h = 88)
        { auto p = cell (c); add<Knob> (*this, owned, st, id, l, col)->setBounds (p.x + x, p.y + y, w, h); };
        auto combo = [&] (int c, const char* id, juce::Colour col, int x, int y, int w)
        { auto p = cell (c); add<Combo> (*this, owned, st, id, col)->setBounds (p.x + x, p.y + y, w, 22); };

        combo (0, "dist_mode", C::orange, 8, 32, 165);
        knob (0, "dist_drive", "DRIVE", C::orange, 14, 70, 76, 104); knob (0, "dist_mix", "MIX", C::orange, 92, 70, 76, 104);
        knob (1, "ott_depth", "DEPTH", C::amber, 8, 34, 84, 110); knob (1, "ott_time", "TIME", C::amber, 92, 34, 84, 110);
        knob (2, "cho_rate", "RATE", C::cyan, 4, 40); knob (2, "cho_depth", "DEPTH", C::cyan, 62, 40); knob (2, "cho_mix", "MIX", C::cyan, 120, 40);
        combo (3, "dly_div", C::blue, 8, 32, 104);
        { auto p = cell (3); add<Toggle> (*this, owned, st, "dly_ping", "PING", C::blue)->setBounds (p.x + 116, p.y + 32, 58, 22); }
        knob (3, "dly_fb", "FEEDBACK", C::blue, 4, 66); knob (3, "dly_tone", "TONE", C::blue, 62, 66); knob (3, "dly_mix", "MIX", C::blue, 120, 66);
        knob (4, "rev_size", "SIZE", C::violet, 14, 28, 76, 84); knob (4, "rev_decay", "DECAY", C::violet, 92, 28, 76, 84);
        knob (4, "rev_damp", "DAMP", C::violet, 14, 112, 76, 84); knob (4, "rev_mix", "MIX", C::violet, 92, 112, 76, 84);
        combo (5, "pump_div", C::magenta, 8, 32, 140);
        knob (5, "pump_depth", "DEPTH", C::magenta, 4, 66, 70, 100); knob (5, "pump_curve", "RELEASE", C::magenta, 74, 66, 70, 100);
        knob (6, "vol", "VOLUME", C::green, 34, 30, 112, 128);
        { auto p = cell (6); add<Toggle> (*this, owned, st, "clip", "SOFT CLIP", C::green)->setBounds (p.x + 40, p.y + 164, 100, 24); }
    }
    void paint (juce::Graphics& g) override
    {
        const char* names[] = { "DISTORTION", "OTT", "CHORUS", "DELAY", "REVERB", "PUMP", "MASTER", "OUTPUT" };
        const char* ons[] = { "dist_on", "ott_on", "cho_on", "dly_on", "rev_on", "pump_on", nullptr, nullptr };
        const juce::Colour cols[] = { C::orange, C::amber, C::cyan, C::blue, C::violet, C::magenta, C::green, C::green };
        for (int i = 0; i < 8; ++i)
            panel (g, juce::Rectangle<float> ((float) ((i % 4) * 189), (float) ((i / 4) * 210), 181.0f, 202.0f), names[i], cols[i],
                   ons[i] == nullptr || pv (st, ons[i]) > 0.5f);
        // OTT band LEDs (decorative depth indicator)
        const float depth = pv (st, "ott_on") > 0.5f ? pv (st, "ott_depth") : 0.0f;
        const char* bands[] = { "LOW", "MID", "HIGH" };
        for (int b = 0; b < 3; ++b)
        {
            for (int k = 0; k < 8; ++k)
                drawLed (g, { 200.0f + 40.0f + b * 50.0f + (k % 2) * 8.0f, 160.0f + (float) (k / 2) * 8.0f }, 2.0f, C::amber, depth * 8.0f > (float) k ? 0.9f : 0.0f);
            g.setFont (font (8.5f)); g.setColour (C::dim);
            g.drawText (bands[b], 228 + b * 50, 192, 40, 10, juce::Justification::centred);
        }
        // pump gain LEDs
        {
            const float pg = proc.pumpGain.load();
            const bool on = pv (st, "pump_on") > 0.5f;
            for (int k = 0; k < 12; ++k)
                drawLed (g, { 189.0f + 162.0f, 210.0f + 180.0f - k * 10.0f }, 2.6f, C::magenta, on && pg * 12.0f > (float) k ? 1.0f : 0.0f);
        }
        // output meters
        auto o = juce::Rectangle<float> (567.0f + 189.0f - 189.0f + 0.0f, 210.0f, 181.0f, 202.0f);
        o = juce::Rectangle<float> (3 * 189.0f, 210.0f, 181.0f, 202.0f);
        ledMeter (g, { o.getX() + 46.0f, o.getY() + 34.0f, 30.0f, 128.0f }, mL, hL, true, 24);
        ledMeter (g, { o.getX() + 104.0f, o.getY() + 34.0f, 30.0f, 128.0f }, mR, hR, true, 24);
        g.setFont (font (10.0f)); g.setColour (C::dim);
        g.drawText ("L", (int) o.getX() + 46, (int) o.getY() + 164, 30, 12, juce::Justification::centred);
        g.drawText ("R", (int) o.getX() + 104, (int) o.getY() + 164, 30, 12, juce::Justification::centred);
        const float pk = juce::Decibels::gainToDecibels (juce::jmax (hL, hR), -60.0f);
        peakMatrix.draw (g, { o.getX() + 8.0f, o.getY() + 176.0f, 165.0f, 22.0f }, pk <= -59.0f ? juce::String ("-INF DB") : juce::String (pk, 1) + " DB",
                         pk > -0.5f ? C::red : C::green, 2.2f, juce::Justification::centred, false, 10.0f);
    }
    void tick() override
    {
        const float l = proc.peakL.load(), r = proc.peakR.load();
        mL = juce::jmax (l, mL * 0.82f); mR = juce::jmax (r, mR * 0.82f);
        if (l > hL || r > hR || ++holdT > 45) { hL = juce::jmax (l, holdT > 45 ? 0.0f : hL); hR = juce::jmax (r, holdT > 45 ? 0.0f : hR); if (holdT > 45) { holdT = 0; hL = l; hR = r; } }
        repaint (189, 210, 181, 202);
        repaint (3 * 189, 210, 181, 202);
        const float sig = pv (st, "dist_on") + 2 * pv (st, "ott_on") + 4 * pv (st, "cho_on") + 8 * pv (st, "dly_on") + 16 * pv (st, "rev_on") + 32 * pv (st, "pump_on") + 64 * pv (st, "ott_depth");
        if (differs (sig, lastSig)) { lastSig = sig; repaint(); }
    }
private:
    APVTS& st; LumenSynthProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> owned;
    float mL = 0, mR = 0, hL = 0, hR = 0, lastSig = -1; int holdT = 0;
    DotMatrix peakMatrix;
};

//==============================================================================
class SmartPanel : public Page
{
public:
    SmartPanel (LumenSynthProcessor& p) : proc (p)
    {
        category.addItemList (lumen::synth::categories(), 1);
        category.setSelectedId (1, juce::dontSendNotification);
        category.setColour (juce::ComboBox::textColourId, C::cyan);
        addAndMakeVisible (category);
        for (auto* b : { &gen, &mut, &und }) addAndMakeVisible (*b);
        gen.setColour (juce::TextButton::buttonColourId, C::cyan);
        mut.setColour (juce::TextButton::buttonColourId, C::magenta);
        und.setColour (juce::TextButton::buttonColourId, C::dim.brighter (0.5f));
        amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        amount.setRange (0.0, 1.0); amount.setValue (0.35);
        amount.setColour (juce::Slider::rotarySliderFillColourId, C::magenta);
        addAndMakeVisible (amount);
        gen.onClick = [this] { proc.generate (category.getSelectedId() - 1, rnd); status = "NEW " + category.getText() + " PATCH"; flash = 1.0f; };
        mut.onClick = [this] { proc.mutate ((float) amount.getValue(), rnd); status = "MUTATED " + juce::String ((int) std::round (amount.getValue() * 100)) + "%"; flash = 1.0f; };
        und.onClick = [this] { status = proc.undo() ? "UNDO" : "NOTHING TO UNDO"; flash = 1.0f; };
        gen.setTooltip ("Builds a complete new patch for the chosen style from sound-design rules");
        mut.setTooltip ("Creates a variation of the current sound (amount = how far)");
    }
    void resized() override
    {
        category.setBounds (10, 32, 118, 26);
        gen.setBounds (134, 30, 124, 30);
        mut.setBounds (10, 70, 118, 30);
        amount.setBounds (138, 62, 48, 48);
        und.setBounds (196, 70, 62, 30);
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, getLocalBounds().toFloat(), "SMART DESIGN", C::cyan);
        drawLed (g, { 16.0f, 13.0f }, 3.0f, C::cyan, 0.6f + 0.4f * breathe);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText ("AMOUNT", 134, 108, 56, 10, juce::Justification::centred);
        auto st = juce::Rectangle<float> (10.0f, 126.0f, 248.0f, 30.0f);
        lcd (g, st, C::cyan);
        matrix.draw (g, st.reduced (6.0f, 2.0f), status, C::cyan.interpolatedWith (juce::Colours::white, 0.3f * flash), 2.2f, juce::Justification::centred, true, 10.0f);
        g.setFont (font (9.5f, false)); g.setColour (C::dim.brighter (0.2f));
        g.drawFittedText ("GENERATE designs a full patch for the style.  MUTATE makes a variation.  UNDO steps back.",
                          juce::Rectangle<int> (10, 160, 248, 30), juce::Justification::centredLeft, 2);
    }
    void tick() override
    {
        t += 1.0 / 30.0; breathe = 0.5f + 0.5f * (float) std::sin (t * 2.4);
        flash *= 0.9f;
        und.setEnabled (proc.canUndo());
        repaint (0, 0, 30, 26); repaint (10, 126, 248, 30);
    }
private:
    LumenSynthProcessor& proc;
    juce::ComboBox category;
    juce::TextButton gen { "GENERATE" }, mut { "MUTATE" }, und { "UNDO" };
    juce::Slider amount;
    DotMatrix matrix;
    juce::String status { "READY" };
    juce::Random rnd;
    double t = 0; float breathe = 0, flash = 0;
};

class MacroPanel : public Page
{
public:
    MacroPanel (APVTS& s)
    {
        const juce::Colour cols[] = { C::cyan, C::magenta, C::amber, C::green };
        for (int m = 0; m < 4; ++m)
            add<Knob> (*this, owned, s, "macro" + juce::String (m + 1), "MACRO " + juce::String (m + 1), cols[m])->setBounds (6 + m * 64, 26, 64, 82);
    }
    void paint (juce::Graphics& g) override { panel (g, getLocalBounds().toFloat(), "MACROS", C::cyan); }
private:
    std::vector<std::unique_ptr<juce::Component>> owned;
};

// LED-matrix oscilloscope
class ScopePanel : public Page
{
public:
    ScopePanel (LumenSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        panel (g, getLocalBounds().toFloat(), "SCOPE", C::green);
        auto a = getLocalBounds().toFloat().reduced (10.0f, 6.0f).withTrimmedTop (20.0f);
        lcd (g, a, C::green);
        a = a.reduced (4.0f, 3.0f);
        const int cols = 62, rows = 13;
        const float px = a.getWidth() / cols, py = a.getHeight() / rows;
        // trigger: find a rising zero crossing in the recent history
        const int N = LumenSynthProcessor::kScope;
        const int wp = proc.scopePos.load (std::memory_order_acquire);
        float buf[LumenSynthProcessor::kScope];
        for (int i = 0; i < N; ++i) buf[i] = proc.scope[(wp + i) & (N - 1)];
        int start = N - 700;
        for (int i = N - 700; i > 300; --i) if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) { start = i; break; }
        float pk = 0.05f; for (int i = start; i < start + 600 && i < N; ++i) pk = juce::jmax (pk, std::abs (buf[i]));
        gainSm += (1.0f / pk - gainSm) * 0.2f;
        int prevRow = -1;
        for (int c = 0; c < cols; ++c)
        {
            const int idx = juce::jmin (N - 1, start + c * 600 / cols);
            const float v = juce::jlimit (-1.0f, 1.0f, buf[idx] * gainSm * 0.92f);
            const int row = juce::jlimit (0, rows - 1, (int) std::round ((1.0f - (v + 1.0f) * 0.5f) * (rows - 1)));
            for (int r = 0; r < rows; ++r)
            {
                const bool between = prevRow >= 0 && ((r > juce::jmin (row, prevRow) && r < juce::jmax (row, prevRow)));
                const float lv = r == row ? 1.0f : between ? 0.55f : 0.0f;
                drawLed (g, { a.getX() + (c + 0.5f) * px, a.getY() + (r + 0.5f) * py }, juce::jmin (px, py) * 0.32f, C::green, lv);
            }
            prevRow = row;
        }
    }
    void tick() override { repaint(); }
private:
    LumenSynthProcessor& proc;
    float gainSm = 1.0f;
};
} // namespace

//==============================================================================
void LedKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour)
{
    auto r = area.reduced (0.6f, 0.0f);
    juce::ColourGradient gr (juce::Colour (0xff2a3340), r.getX(), r.getY(), juce::Colour (0xff3e4a5b), r.getX(), r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (r.withTrimmedTop (-4.0f), 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRect (r.withHeight (2.0f).withY (r.getBottom() - 4.0f));
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRect (r); }
    if (down)
    {
        juce::ColourGradient lit (C::cyan.withAlpha (0.75f), r.getX(), r.getY(), C::cyan.withAlpha (0.15f), r.getX(), r.getBottom(), false);
        g.setGradientFill (lit); g.fillRoundedRectangle (r.withTrimmedTop (-4.0f), 3.0f);
    }
    // LED at the bottom of each key
    drawLed (g, { r.getCentreX(), r.getBottom() - 9.0f }, 2.3f, C::cyan, down ? 1.0f : 0.0f);
    g.setColour (juce::Colours::black); g.drawVerticalLine ((int) area.getRight(), area.getY(), area.getBottom());
    if (note % 12 == 0)
    {
        g.setColour (down ? juce::Colours::white : juce::Colour (0xff0d1117));
        g.setFont (font (8.5f));
        g.drawText ("C" + juce::String (note / 12 - 2), r.withTrimmedBottom (14.0f).removeFromBottom (12.0f), juce::Justification::centred);
    }
}

void LedKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour)
{
    auto r = area.reduced (0.5f, 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (r.translated (1.5f, 2.0f).withTrimmedTop (-4.0f), 2.5f);
    juce::ColourGradient gr (juce::Colour (0xff030405), r.getX(), r.getY(), juce::Colour (0xff10151c), r.getX(), r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (r.withTrimmedTop (-4.0f), 2.5f);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.16f : 0.09f)); g.drawRoundedRectangle (r.reduced (1.0f).withTrimmedTop (-4.0f), 2.0f, 1.0f);
    if (down)
    {
        juce::ColourGradient lit (C::magenta.withAlpha (0.85f), r.getX(), r.getY(), C::magenta.withAlpha (0.2f), r.getX(), r.getBottom(), false);
        g.setGradientFill (lit); g.fillRoundedRectangle (r.withTrimmedTop (-4.0f), 2.5f);
    }
    drawLed (g, { r.getCentreX(), r.getBottom() - 7.0f }, 1.8f, C::magenta, down ? 1.0f : 0.0f);
}

//==============================================================================
LumenSynthEditor::LumenSynthEditor (LumenSynthProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p.keyboardState)
{
    setLookAndFeel (&lnf);
    pages[0] = std::make_unique<OscPage> (proc.apvts);
    pages[1] = std::make_unique<FilterPage> (proc.apvts);
    pages[2] = std::make_unique<ModPage> (proc.apvts, proc);
    pages[3] = std::make_unique<FxPage> (proc.apvts, proc);
    for (auto& pg : pages) addChildComponent (*pg);
    smart  = std::make_unique<SmartPanel> (proc);
    macros = std::make_unique<MacroPanel> (proc.apvts);
    scope  = std::make_unique<ScopePanel> (proc);
    addAndMakeVisible (*smart); addAndMakeVisible (*macros); addAndMakeVisible (*scope);

    const char* names[] = { "OSC", "FILTER / ENV", "MOD", "FX" };
    const juce::Colour cols[] = { C::cyan, C::amber, C::green, C::magenta };
    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setButtonText (names[i]);
        tabs[i].setColour (juce::TextButton::buttonColourId, cols[i]);
        tabs[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs[i]);
    }
    for (auto* b : { &prevBtn, &nextBtn }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); addAndMakeVisible (*b); }
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    presetArea.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    presetArea.setAlpha (0.0f);
    presetArea.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetArea);

    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (24);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setKeyPressBaseOctave (5);
    addAndMakeVisible (keyboard);

    showPage (0);
    setSize (1060, 720);
    startTimerHz (30);
}

LumenSynthEditor::~LumenSynthEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LumenSynthEditor::showPage (int idx)
{
    currentPage = idx;
    for (int i = 0; i < 4; ++i) pages[i]->setVisible (i == idx);
    repaint (0, 64, getWidth(), 40);
}

void LumenSynthEditor::stepPreset (int delta)
{
    const int n = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + n) % n);
}

void LumenSynthEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& pr = lumen::synth::factoryPresets();
    for (auto& cat : lumen::synth::categories())
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) pr.size(); ++i)
            if (pr[(size_t) i].category == cat) sub.addItem (i + 1, pr[(size_t) i].name, true, i == proc.getCurrentProgram());
        menu.addSubMenu (cat, sub);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetArea),
                        [this] (int r) { if (r > 0) proc.setCurrentProgram (r - 1); });
}

void LumenSynthEditor::resized()
{
    for (auto& pg : pages) pg->setBounds (16, 104, 748, 412);
    smart->setBounds (776, 104, 268, 196);
    macros->setBounds (776, 308, 268, 112);
    scope->setBounds (776, 428, 268, 88);
    for (int i = 0; i < 4; ++i) tabs[i].setBounds (16 + i * 124, 70, 118, 28);
    prevBtn.setBounds (306, 18, 30, 30);
    nextBtn.setBounds (704, 18, 30, 30);
    presetArea.setBounds (340, 10, 360, 46);
    keyboard.setBounds (16, 528, 1028, 180);
    keyboard.setKeyWidth (1028.0f / 50.0f);
    keyboard.setBlackNoteLengthProportion (0.6f);
}

void LumenSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    dotGrid (g, getLocalBounds().toFloat(), 9.0f, juce::Colours::white, 0.035f);
    // vignette
    juce::ColourGradient vig (juce::Colours::transparentBlack, getWidth() * 0.5f, getHeight() * 0.4f, juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig); g.fillRect (getLocalBounds());

    // ---- header ----
    juce::ColourGradient hg (juce::Colour (0xff0c1119), 0, 0, juce::Colour (0xff06080c), 0, 64, false);
    g.setGradientFill (hg); g.fillRect (0, 0, getWidth(), 64);
    juce::ColourGradient line (C::cyan.withAlpha (0.0f), 0, 64, C::cyan.withAlpha (0.0f), (float) getWidth(), 64, false);
    line.addColour (0.3, C::cyan.withAlpha (0.7f)); line.addColour (0.7, C::magenta.withAlpha (0.7f));
    g.setGradientFill (line); g.fillRect (0, 63, getWidth(), 2);
    g.setGradientFill (line); g.setOpacity (0.25f); g.fillRect (0, 60, getWidth(), 8); g.setOpacity (1.0f);

    // logo with glow
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (32.0f), "LUMEN", 20.0f, 40.0f);
        juce::Path lp; ga.createPath (lp);
        g.setColour (C::cyan.withAlpha (0.12f)); g.strokePath (lp, juce::PathStrokeType (6.0f));
        g.setColour (C::cyan.withAlpha (0.25f)); g.strokePath (lp, juce::PathStrokeType (2.5f));
        juce::ColourGradient lg (C::cyan, 20, 0, C::magenta, 140, 0, false);
        g.setGradientFill (lg); g.fillPath (lp);
        logoMatrix.draw (g, { 150.0f, 18.0f, 120.0f, 28.0f }, "SYNTH", C::magenta, 2.6f, juce::Justification::centredLeft, false, 10.5f);
    }

    // preset display
    {
        auto d = juce::Rectangle<float> (300.0f, 8.0f, 440.0f, 50.0f);
        lcd (g, d, C::cyan);
        dotGrid (g, d.reduced (4.0f), 3.0f, C::cyan, 0.05f);
        const auto& pr = lumen::synth::factoryPresets();
        nameMatrix.draw (g, { 342.0f, 22.0f, 356.0f, 30.0f }, proc.getPresetName(), C::cyan, 2.45f, juce::Justification::centred, true, 10.6f);
        g.setFont (font (9.5f)); g.setColour (C::magenta.withAlpha (0.9f));
        g.drawText (proc.getPresetCategory().toUpperCase(), 344, 10, 150, 12, juce::Justification::centredLeft);
        g.setColour (C::dim);
        g.drawText (juce::String (proc.getCurrentProgram() + 1) + " / " + juce::String ((int) pr.size()), 546, 10, 150, 12, juce::Justification::centredRight);
    }

    // voice LEDs
    g.setFont (font (9.5f)); g.setColour (C::text.withAlpha (0.6f));
    g.drawText ("VOICES  " + juce::String (proc.activeVoices.load()), 760, 10, 140, 12, juce::Justification::centredLeft);
    for (int i = 0; i < 16; ++i)
        drawLed (g, { 766.0f + (i % 8) * 16.0f, 32.0f + (i / 8) * 14.0f }, 3.6f, i < 8 ? C::cyan : C::magenta, voiceLv[i]);
    // output meter
    g.setColour (C::text.withAlpha (0.6f));
    g.drawText ("OUTPUT", 912, 10, 132, 12, juce::Justification::centredLeft);
    ledMeter (g, { 912.0f, 28.0f, 132.0f, 9.0f }, meterL, holdL, false, 22);
    ledMeter (g, { 912.0f, 42.0f, 132.0f, 9.0f }, meterR, holdR, false, 22);

    // tab LEDs
    const juce::Colour cols[] = { C::cyan, C::amber, C::green, C::magenta };
    for (int i = 0; i < 4; ++i)
    {
        drawLed (g, { 16.0f + i * 124.0f + 12.0f, 84.0f }, 3.0f, cols[i], i == currentPage ? 1.0f : 0.0f);
        if (i == currentPage)
        {
            g.setColour (cols[i].withAlpha (0.8f)); g.fillRect (16.0f + i * 124.0f + 10.0f, 99.0f, 98.0f, 2.0f);
            g.setColour (cols[i].withAlpha (0.2f)); g.fillRect (16.0f + i * 124.0f + 6.0f, 97.0f, 106.0f, 6.0f);
        }
    }
    g.setFont (font (9.5f)); g.setColour (C::dim);
    g.drawText ("LUMEN AUDIO  -  WAVETABLE SYNTHESIZER", 520, 76, 244, 14, juce::Justification::centredRight);
}

void LumenSynthEditor::timerCallback()
{
    const float l = proc.peakL.load(), r = proc.peakR.load();
    meterL = juce::jmax (l, meterL * 0.8f); meterR = juce::jmax (r, meterR * 0.8f);
    if (++holdTimer > 40) { holdTimer = 0; holdL = holdR = 0.0f; }
    holdL = juce::jmax (holdL, l); holdR = juce::jmax (holdR, r);
    for (int i = 0; i < 16; ++i) voiceLv[i] = juce::jmax (proc.voiceLevel[i].load(), voiceLv[i] * 0.7f);
    repaint (756, 6, 292, 54);
    if (proc.presetVersion.load() != lastPresetVersion) { lastPresetVersion = proc.presetVersion.load(); repaint (300, 8, 440, 50); }
    pages[currentPage]->tick();
    smart->tick(); scope->tick();
}
