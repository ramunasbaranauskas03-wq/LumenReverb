#include "PluginEditor.h"
#include "dsp/Wavetables.h"
#include <complex>

using namespace lumen::led;
using namespace lumen::synth;
using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
inline bool differs (float a, float b) { return std::abs (a - b) > 1.0e-7f; }
float pv (APVTS& s, const juce::String& id) { return s.getRawParameterValue (id)->load(); }
const char* oscPrefix (int o) { return o == 0 ? "a" : o == 1 ? "b" : "c"; }

template <typename T, typename... Args>
T* add (juce::Component& parent, std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
{
    auto c = std::make_unique<T> (std::forward<Args> (args)...);
    T* raw = c.get(); parent.addAndMakeVisible (*raw); owner.push_back (std::move (c));
    return raw;
}

//==============================================================================
// 3D wavetable display + LOAD button + WAV drag & drop
class WavetableView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    WavetableView (LumenSynthProcessor& p, int oscIndex, juce::Colour c) : proc (p), st (p.apvts), osc (oscIndex), pre (oscPrefix (oscIndex)), col (c)
    {
        load.setButtonText ("LOAD");
        load.setColour (juce::TextButton::buttonColourId, col);
        load.setTooltip ("Import a wavetable (Serum-style 2048-sample frames) or any audio file. You can also drag a WAV onto the display.");
        load.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load wavetable", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) importFile (fc.getResult()); });
        };
        addAndMakeVisible (load);
    }
    void resized() override { load.setBounds (6, 5, 42, 16); }
    bool changed()
    {
        const float sig = pv (st, pre + "_table") * 1000.0f + pv (st, pre + "_pos") * 100.0f + pv (st, pre + "_warpm") * 7.0f + pv (st, pre + "_warp") * 3.0f
                          + pv (st, pre + "_on") + (float) proc.tableVersion.load() * 13.0f;
        if (! differs (sig, last)) return false;
        last = sig;
        return true;
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override { dropHover = false; if (! files.isEmpty()) importFile (juce::File (files[0])); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = pv (st, pre + "_on") > 0.5f;
        lcd (g, r, col);
        const lumen::Table* tb = proc.tableFor (osc);
        const float pos = pv (st, pre + "_pos");
        const int wm = (int) pv (st, pre + "_warpm");
        const float warp = pv (st, pre + "_warp");
        if (tb == nullptr)
        {
            g.setColour (col.withAlpha (0.6f)); g.setFont (font (11.0f));
            g.drawText ("DROP A WAV HERE", r, juce::Justification::centred);
            return;
        }
        const int F = tb->frames, P = 64;
        auto area = r.reduced (8.0f, 8.0f).withTrimmedTop (8.0f);
        const float depthX = area.getWidth() * 0.22f, depthY = area.getHeight() * 0.32f;
        const float w = area.getWidth() - depthX, h = area.getHeight() - depthY;
        auto framePath = [&] (int f, float morph, bool warped)
        {
            juce::Path path;
            const float dz = F > 1 ? (float) f / (float) (F - 1) : 0.0f;
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
                        case 5: { const float m = ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph; ph += warp * (m * 0.999f - ph); break; }
                        case 6: { const float a = (2.0f * warp - 1.0f) * 8.0f;
                                  ph = a >= 0.0f ? ph * (1.0f + a) / (1.0f + a * ph) : 1.0f - (1.0f - ph) * (1.0f - a) / (1.0f - a * (1.0f - ph)); break; }
                        case 7: { const float q = std::exp2 (1.0f + (1.0f - warp) * 7.0f); ph = std::floor (ph * q) / q; break; }
                        case 8: ph -= warp * 0.15f * std::sin (6.2831853f * ph); ph -= std::floor (ph); break;
                        default: break;
                    }
                }
                const float v = morph >= 0.0f ? tb->read (morph, 3, juce::jmin (ph, 0.9999f)) : tb->frame (f, 3)[(int) (juce::jmin (ph, 0.9999f) * 2047.0f)];
                const float x = ox + w * (float) i / (float) P;
                const float y = oy + h * 0.5f - juce::jlimit (-1.2f, 1.2f, v) * h * 0.46f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            return path;
        };
        const int shown = juce::jmin (F, 16);
        for (int k = shown - 1; k >= 0; --k)
        {
            const int f = shown > 1 ? (int) std::round ((float) k * (F - 1) / (shown - 1)) : 0;
            const float dist = std::abs ((F > 1 ? (float) f / (F - 1) : 0.0f) - pos);
            g.setColour (col.withAlpha ((on ? 0.17f : 0.07f) + (on ? 0.25f : 0.0f) * juce::jmax (0.0f, 1.0f - dist * 5.0f)));
            g.strokePath (framePath (f, -1.0f, false), juce::PathStrokeType (0.9f));
        }
        const int fcur = juce::jlimit (0, F - 1, (int) std::round (pos * (F - 1)));
        auto cur = framePath (fcur, pos, true);
        if (on)
        {
            const float dz = F > 1 ? (float) fcur / (float) (F - 1) : 0.0f;
            const float base = area.getY() + depthY * dz + h * 0.5f;
            juce::Path fill (cur);
            fill.lineTo (area.getX() + depthX * (1.0f - dz) + w, base); fill.lineTo (area.getX() + depthX * (1.0f - dz), base); fill.closeSubPath();
            juce::ColourGradient gr (col.withAlpha (0.28f), 0, base - h * 0.5f, col.withAlpha (0.0f), 0, base + h * 0.5f, false);
            g.setGradientFill (gr); g.fillPath (fill);
            glowPath (g, cur, col, 1.6f);
        }
        else glowPath (g, cur, C::dim, 1.0f, 0.5f);
        g.setFont (font (9.0f)); g.setColour (col.withAlpha (on ? 0.75f : 0.3f));
        const bool user = (int) pv (st, pre + "_table") >= kNumFactoryTables;
        g.drawText ((user ? juce::String (tb->name).toUpperCase().substring (0, 14) + "  " : juce::String()) + juce::String (fcur + 1) + "/" + juce::String (F),
                    r.reduced (6.0f, 5.0f), juce::Justification::topRight);
        if (dropHover) { g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (r, 5.0f); g.setColour (col); g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f); }
    }
private:
    void importFile (const juce::File& f)
    {
        juce::String err;
        if (! proc.loadWavetableFile (osc, f, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Lumen Synth", err);
        last = -1.0f; repaint();
    }
    LumenSynthProcessor& proc; APVTS& st; int osc; juce::String pre; juce::Colour col;
    juce::TextButton load;
    std::unique_ptr<juce::FileChooser> chooser;
    float last = -1.0f; bool dropHover = false;
};

//==============================================================================
class FilterView : public juce::Component
{
public:
    FilterView (APVTS& s, juce::String prefix, juce::Colour c) : st (s), p (std::move (prefix)), col (c) {}
    bool changed()
    {
        const float sig = pv (st, p + "_type") * 7.0f + pv (st, p + "_cut") + pv (st, p + "_res") * 13.0f + pv (st, p + "_on") * 3.0f + pv (st, p + "_drive") * 5.0f;
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
            case fLadder:  H = (1.0 + 0.55 * k) / (std::pow (1.0 + s, 4.0) + k); break;
            case fLP12:    H = 1.0 / den; break;
            case fLP24:    H = 1.0 / (den * den); break;
            case fHP12:    H = s * s / den; break;
            case fHP24:    H = (s * s / den) * (s * s / den); break;
            case fBP:      H = ks * s / den; break;
            case fNotch:   H = (s * s + 1.0) / den; break;
            case fCombP: case fCombN:
            {
                const double g = 0.15 + 0.83 * std::sqrt (r);
                const cd e = std::exp (cd (0.0, -2.0 * 3.141592653589793 * (double) f / std::max (25.0, (double) fc)));
                H = (type == fCombP ? 1.0 / (1.0 - g * e) : 1.0 / (1.0 + g * e)) * std::sqrt (1.0 - g * g) * 1.4;
                break;
            }
            case fFormant:
            {
                static const double F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2100, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const double pos = juce::jlimit (0.0, 1.0, std::log2 (fc / 20.0) / std::log2 (1000.0)) * 4.0;
                const int a = std::min (3, (int) pos); const double fr = pos - a;
                const double q = 4.0 + 16.0 * r;
                H = 0.0;
                for (int i = 0; i < 3; ++i)
                {
                    const double fi = F[a][i] + (F[a + 1][i] - F[a][i]) * fr;
                    const cd si (0.0, f / fi);
                    H += (si / q) / (si * si + si / q + 1.0) * (i == 0 ? 1.0 : i == 1 ? 0.7 : 0.45) * 1.8;
                }
                break;
            }
            case fPhaser:
            {
                static const double spread[4] = { 0.55, 0.85, 1.3, 2.1 };
                cd ap = 1.0;
                for (double sp : spread) { const cd si (0.0, f / (fc * sp)); ap *= (1.0 - si) / (1.0 + si); }
                H = 0.6 * (1.0 + ap / (1.0 - 0.92 * r * ap));
                break;
            }
            case fMS20:
            {
                const double kk = 2.0 - 1.995 * std::pow (r, 0.5);
                H = (1.0 + 0.4 * (2.0 - kk)) / (s * s + kk * s + 1.0);
                break;
            }
            case fDiode:
            {
                const double kd = 7.4 * r * (0.7 + 0.3 * r) * 0.95;
                H = (1.0 + 0.3 * kd) / (std::pow (1.0 + s, 3.0) + kd);
                break;
            }
            case fJuno:
            {
                const double kj = 3.7 * r * 0.97;
                H = (1.0 + 0.9 * kj) / (std::pow (1.0 + s, 4.0) + kj);
                break;
            }
            default: H = 1.0; break;
        }
        return (float) (20.0 * std::log10 (std::abs (H) + 1e-9));
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        lcd (g, r, col);
        auto a = r.reduced (8.0f, 8.0f);
        dotGrid (g, a, 6.0f, col, 0.08f);
        g.setColour (col.withAlpha (0.12f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = a.getX() + a.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        }
        const bool on = pv (st, p + "_on") > 0.5f;
        const int type = (int) pv (st, p + "_type");
        const float fc = pv (st, p + "_cut"), res = pv (st, p + "_res");
        auto yOf = [&] (float db) { return a.getY() + a.getHeight() * (1.0f - (juce::jlimit (-42.0f, 24.0f, db) + 42.0f) / 66.0f); };
        juce::Path path;
        for (int i = 0; i <= 200; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
            const float db = on ? magDb (type, f, fc, res) : 0.0f;
            const float x = a.getX() + a.getWidth() * (float) i / 200.0f;
            if (i == 0) path.startNewSubPath (x, yOf (db)); else path.lineTo (x, yOf (db));
        }
        juce::Path fill (path); fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
        juce::ColourGradient gr (col.withAlpha (on ? 0.3f : 0.08f), 0, a.getY(), col.withAlpha (0.0f), 0, a.getBottom(), false);
        g.setGradientFill (gr); g.fillPath (fill);
        glowPath (g, path, on ? col : C::dim, 1.6f, on ? 1.0f : 0.5f);
        if (on)
        {
            const float x = a.getX() + a.getWidth() * std::log (fc / 20.0f) / std::log (1000.0f);
            drawLed (g, { x, yOf (magDb (type, fc, fc, res)) }, 3.5f, juce::Colours::white.interpolatedWith (col, 0.4f), 1.0f);
        }
        g.setFont (font (9.5f)); g.setColour (col.withAlpha (0.7f));
        g.drawText (formatValue (*findSpec (p + "_cut"), fc), r.reduced (8.0f, 4.0f), juce::Justification::topRight);
    }
private:
    APVTS& st; juce::String p; juce::Colour col; float last = -1.0f;
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
        float nx = a.getX() + tw (A) * sx;
        drawLed (g, { nx, a.getY() }, 2.6f, col, 1.0f);
        nx += tw (D) * sx; drawLed (g, { nx, a.getBottom() - a.getHeight() * S }, 2.6f, col, 1.0f);
        nx += 0.5f * sx;   drawLed (g, { nx, a.getBottom() - a.getHeight() * S }, 2.6f, col, 1.0f);
    }
private:
    APVTS& st; int e; juce::Colour col; float last = -1.0f;
};

//==============================================================================
// LFO display + curve editor (Custom shape is drawable: double-click adds a point, drag moves, right-click deletes)
class LfoEditor : public juce::Component
{
public:
    LfoEditor (LumenSynthProcessor& p, int index, juce::Colour c) : proc (p), st (p.apvts), l (index), col (c)
    {
        draw.setButtonText ("DRAW");
        draw.setColour (juce::TextButton::buttonColourId, col);
        draw.setTooltip ("Convert the current shape into editable points");
        draw.onClick = [this] { proc.setLfoPointsFromShape (l, shape()); repaint(); };
        addAndMakeVisible (draw);
    }
    void resized() override { draw.setBounds (getWidth() - 50, 5, 44, 16); }
    int shape() const { return (int) pv (st, "l" + juce::String (l + 1) + "_shape"); }
    float value() const
    {
        const float ph = proc.lfoPhase[l].load();
        if (shape() == lCustom) { const auto cp = proc.getLfoPoints (l); return 2.0f * curveAt (cp, ph) - 1.0f; }
        return lfoShape (shape(), ph, 0.0f, 0.5f);
    }
    static float curveAt (const std::vector<lumen::CurvePoint>& cp, float x)
    {
        for (size_t i = 0; i + 1 < cp.size(); ++i)
            if (x <= cp[i + 1].x)
            {
                const float w = cp[i + 1].x - cp[i].x;
                const float t = w > 1e-6f ? (x - cp[i].x) / w : 1.0f;
                return cp[i].y + (cp[i + 1].y - cp[i].y) * t;
            }
        return cp.empty() ? 0.5f : cp.back().y;
    }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 12.0f).withTrimmedTop (8.0f); }
    juce::Point<float> toScreen (lumen::CurvePoint c) const { auto a = plot(); return { a.getX() + c.x * a.getWidth(), a.getBottom() - c.y * a.getHeight() }; }
    lumen::CurvePoint fromScreen (juce::Point<float> s) const
    {
        auto a = plot();
        return { juce::jlimit (0.0f, 1.0f, (s.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - s.y) / a.getHeight()) };
    }
    int findPoint (juce::Point<float> s) const
    {
        for (int i = 0; i < (int) pts.size(); ++i) if (toScreen (pts[(size_t) i]).getDistanceFrom (s) < 7.0f) return i;
        return -1;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (shape() != lCustom) return;
        pts = proc.getLfoPoints (l);
        drag = findPoint (e.position);
        if (e.mods.isPopupMenu() && drag > 0 && drag < (int) pts.size() - 1) { pts.erase (pts.begin() + drag); drag = -1; proc.setLfoPoints (l, pts); repaint(); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (shape() != lCustom || drag < 0) return;
        auto c = fromScreen (e.position);
        const int last = (int) pts.size() - 1;
        if (drag == 0) c.x = 0.0f; else if (drag == last) c.x = 1.0f;
        else c.x = juce::jlimit (pts[(size_t) drag - 1].x + 0.004f, pts[(size_t) drag + 1].x - 0.004f, c.x);
        if (e.mods.isShiftDown()) c.y = std::round (c.y * 8.0f) / 8.0f;
        pts[(size_t) drag] = c;
        proc.setLfoPoints (l, pts);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag = -1; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (shape() != lCustom) { proc.setLfoPointsFromShape (l, shape()); repaint(); return; }
        pts = proc.getLfoPoints (l);
        const int h = findPoint (e.position);
        if (h > 0 && h < (int) pts.size() - 1) pts.erase (pts.begin() + h);
        else if (h < 0 && pts.size() < 48) pts.push_back (fromScreen (e.position));
        proc.setLfoPoints (l, pts);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        lcd (g, r, col);
        auto a = plot();
        dotGrid (g, a, 6.0f, col, 0.08f);
        g.setColour (col.withAlpha (0.15f)); g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
        for (int k = 1; k < 4; ++k) g.drawVerticalLine ((int) (a.getX() + a.getWidth() * k / 4.0f), a.getY(), a.getBottom());
        const int sh = shape();
        const bool custom = sh == lCustom;
        if (custom) pts = drag >= 0 ? pts : proc.getLfoPoints (l);
        juce::Path pth;
        juce::Random rr (77 + l);
        float rv[6]; for (auto& x : rv) x = rr.nextFloat() * 2.0f - 1.0f;
        const int N = 160;
        for (int i = 0; i <= N; ++i)
        {
            const float ph = (float) i / N;
            float v;
            if (custom) v = 2.0f * curveAt (pts, ph) - 1.0f;
            else if (sh == lSH || sh == lSmooth)
            {
                const int seg = juce::jmin (3, (int) (ph * 4.0f));
                v = lfoShape (sh, juce::jmin (ph * 4.0f - (float) seg, 0.9999f), rv[seg], rv[seg + 1]);
            }
            else v = lfoShape (sh, juce::jmin (ph, 0.9999f), 0.0f, 0.5f);
            const float x = a.getX() + a.getWidth() * ph, y = a.getCentreY() - v * a.getHeight() * 0.5f;
            if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        juce::Path fill (pth); fill.lineTo (a.getRight(), a.getCentreY()); fill.lineTo (a.getX(), a.getCentreY()); fill.closeSubPath();
        g.setColour (col.withAlpha (0.12f)); g.fillPath (fill);
        glowPath (g, pth, col, 1.5f);
        if (custom)
            for (int i = 0; i < (int) pts.size(); ++i)
                drawLed (g, toScreen (pts[(size_t) i]), i == drag ? 4.2f : 3.2f, juce::Colours::white.interpolatedWith (col, 0.5f), 1.0f);
        const float ph = proc.lfoPhase[l].load();
        const float v = value();
        drawLed (g, { a.getX() + a.getWidth() * ph, a.getCentreY() - v * a.getHeight() * 0.5f }, 3.8f, juce::Colours::white.interpolatedWith (col, 0.3f), 1.0f);
        g.setFont (font (9.0f)); g.setColour (col.withAlpha (0.6f));
        g.drawText (custom ? "DBL-CLICK ADD  -  RIGHT-CLICK DEL  -  SHIFT SNAP" : "DBL-CLICK TO DRAW", r.reduced (8.0f, 4.0f), juce::Justification::bottomLeft);
    }
private:
    LumenSynthProcessor& proc; APVTS& st; int l; juce::Colour col;
    juce::TextButton draw;
    std::vector<lumen::CurvePoint> pts;
    int drag = -1;
};

//==============================================================================
//  PAGES
//==============================================================================
class OscPage : public Page
{
public:
    OscPage (LumenSynthProcessor& p) : st (p.apvts)
    {
        const juce::Colour cols[3] = { C::cyan, C::magenta, C::amber };
        for (int o = 0; o < 3; ++o)
        {
            const juce::String pre = oscPrefix (o);
            const auto col = cols[o];
            const int x0 = o * 252;
            add<Toggle> (*this, owned, st, pre + "_on", "", col, true)->setBounds (x0 + 6, 3, 20, 20);
            add<Combo> (*this, owned, st, pre + "_table", col)->setBounds (x0 + 92, 4, 144, 20);
            auto* wm = add<Combo> (*this, owned, st, pre + "_warpm", col); wm->setBounds (x0 + 8, 28, 112, 20);
            wm->setTooltip ("Warp mode");
            auto* um = add<Combo> (*this, owned, st, pre + "_umode", col); um->setBounds (x0 + 124, 28, 112, 20);
            um->setTooltip ("Unison mode: Classic / Super (JP-8000 supersaw) / stacks: unison voices spread over octaves, fifths or a major / minor chord");
            wt[o] = add<WavetableView> (*this, owned, p, o, col); wt[o]->setBounds (x0 + 8, 54, 228, 112);
            const char* ids[3][4] = { { "_pos", "_warp", "_level", "_pan" }, { "_uni", "_det", "_blend", "_width" }, { "_oct", "_semi", "_fine", "_rand" } };
            const char* lbl[3][4] = { { "POSITION", "WARP", "LEVEL", "PAN" }, { "UNISON", "DETUNE", "BLEND", "WIDTH" }, { "OCTAVE", "SEMI", "FINE", "PHASE" } };
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 4; ++c)
                {
                    const juce::String id = pre + ids[row][c];
                    const bool bip = id.endsWith ("_pan") || id.endsWith ("_oct") || id.endsWith ("_semi") || id.endsWith ("_fine");
                    add<Knob> (*this, owned, st, id, lbl[row][c], col, bip)->setBounds (x0 + 8 + c * 57, 172 + row * 80, 57, 76);
                }
        }
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 244, 412 }, "OSC A", C::cyan, pv (st, "a_on") > 0.5f);
        panel (g, { 252, 0, 244, 412 }, "OSC B", C::magenta, pv (st, "b_on") > 0.5f);
        panel (g, { 504, 0, 244, 412 }, "OSC C", C::amber, pv (st, "c_on") > 0.5f);
    }
    void tick() override
    {
        for (auto* w : wt) if (w->changed()) w->repaint();
        const float sig = pv (st, "a_on") + 2 * pv (st, "b_on") + 4 * pv (st, "c_on");
        if (differs (sig, lastSig)) { lastSig = sig; repaint(); }
    }
private:
    APVTS& st;
    std::vector<std::unique_ptr<juce::Component>> owned;
    WavetableView* wt[3] {};
    float lastSig = -1;
};

//==============================================================================
class FilterPage : public Page
{
public:
    FilterPage (APVTS& s) : st (s)
    {
        for (int f = 0; f < 2; ++f)
        {
            const int x0 = f * 378;
            const juce::String pre = f == 0 ? "flt" : "f2";
            const auto col = f == 0 ? C::amber : C::orange;
            add<Toggle> (*this, owned, st, pre + "_on", "", col, true)->setBounds (x0 + 6, 3, 20, 20);
            add<Combo> (*this, owned, st, pre + "_type", col)->setBounds (x0 + 236, 4, 126, 20);
            fv[f] = add<FilterView> (*this, owned, st, pre, col); fv[f]->setBounds (x0 + 8, 28, 354, 112);
            const char* ids[] = { "_cut", "_res", "_drive", "_env", "_key" };
            const char* lbl[] = { "CUTOFF", "RESO", "DRIVE", "ENV AMT", "KEYTRK" };
            for (int i = 0; i < 5; ++i) add<Knob> (*this, owned, st, pre + ids[i], lbl[i], col, i == 3)->setBounds (x0 + 8 + i * 71, 146, 71, 98);
        }
        add<Toggle> (*this, owned, st, "sub_on", "", C::violet, true)->setBounds (6, 261, 20, 20);
        add<Combo> (*this, owned, st, "sub_shape", C::violet)->setBounds (10, 292, 112, 22);
        add<Combo> (*this, owned, st, "sub_oct", C::violet)->setBounds (10, 322, 112, 22);
        add<Knob> (*this, owned, st, "sub_level", "LEVEL", C::violet)->setBounds (128, 284, 108, 120);
        add<Toggle> (*this, owned, st, "noise_on", "", C::blue, true)->setBounds (258, 261, 20, 20);
        add<Knob> (*this, owned, st, "noise_level", "LEVEL", C::blue)->setBounds (262, 286, 112, 100);
        add<Knob> (*this, owned, st, "noise_color", "COLOR", C::blue, true)->setBounds (378, 286, 112, 100);
        add<Combo> (*this, owned, st, "flt_route", C::amber)->setBounds (512, 290, 228, 24);
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 370, 250 }, "FILTER 1", C::amber, pv (st, "flt_on") > 0.5f);
        panel (g, { 378, 0, 370, 250 }, "FILTER 2", C::orange, pv (st, "f2_on") > 0.5f);
        panel (g, { 0, 258, 244, 154 }, "SUB", C::violet, pv (st, "sub_on") > 0.5f);
        panel (g, { 252, 258, 244, 154 }, "NOISE", C::blue, pv (st, "noise_on") > 0.5f);
        panel (g, { 504, 258, 244, 154 }, "ROUTING", C::amber);
        // routing diagram with LED blocks
        const bool par = (int) pv (st, "flt_route") == 1;
        const bool f1 = pv (st, "flt_on") > 0.5f, f2 = pv (st, "f2_on") > 0.5f;
        auto block = [&] (float x, float y, const char* t, bool on, juce::Colour c)
        {
            juce::Rectangle<float> b (x, y, 46, 20);
            g.setColour (c.withAlpha (on ? 0.18f : 0.05f)); g.fillRoundedRectangle (b, 4);
            g.setColour (c.withAlpha (on ? 0.9f : 0.3f)); g.drawRoundedRectangle (b, 4, 1);
            g.setFont (font (9.5f)); g.drawText (t, b, juce::Justification::centred);
        };
        g.setColour (C::dim);
        if (! par)
        {
            block (514, 340, "OSC", true, C::cyan); block (578, 340, "F1", f1, C::amber); block (642, 340, "F2", f2, C::orange); block (700, 340, "OUT", true, C::green);
            for (float x : { 560.0f, 624.0f, 688.0f }) { g.setColour (C::dim); g.drawArrow ({ x, 350, x + 16, 350 }, 1.2f, 6, 6); }
        }
        else
        {
            block (514, 340, "OSC", true, C::cyan); block (604, 322, "F1", f1, C::amber); block (604, 360, "F2", f2, C::orange); block (694, 340, "OUT", true, C::green);
            g.setColour (C::dim);
            g.drawArrow ({ 560, 346, 602, 332 }, 1.2f, 6, 6); g.drawArrow ({ 560, 354, 602, 370 }, 1.2f, 6, 6);
            g.drawArrow ({ 650, 332, 692, 346 }, 1.2f, 6, 6); g.drawArrow ({ 650, 370, 692, 354 }, 1.2f, 6, 6);
        }
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawFittedText ("Serial: F1 into F2.  Parallel: both filters side by side, then mixed.", 512, 384, 228, 24, juce::Justification::centredLeft, 2);
    }
    void tick() override
    {
        for (auto* f : fv) if (f->changed()) f->repaint();
        const float sig = pv (st, "flt_on") + 2 * pv (st, "f2_on") + 4 * pv (st, "sub_on") + 8 * pv (st, "noise_on") + 16 * pv (st, "flt_route");
        if (differs (sig, lastSig)) { lastSig = sig; repaint(); }
    }
private:
    APVTS& st;
    std::vector<std::unique_ptr<juce::Component>> owned;
    FilterView* fv[2] {};
    float lastSig = -1;
};

//==============================================================================
class EnvPage : public Page
{
public:
    EnvPage (LumenSynthProcessor& p, std::vector<ModBadge*>& badges) : st (p.apvts), proc (p)
    {
        const juce::Colour ec[3] = { C::green, C::amber, C::magenta };
        for (int e = 0; e < 3; ++e)
        {
            const int y0 = e * 139;
            ev[e] = add<EnvView> (*this, owned, st, e + 1, ec[e]); ev[e]->setBounds (8, y0 + 28, 222, 98);
            const juce::String pre = "e" + juce::String (e + 1) + "_";
            const char* k[] = { "a", "d", "s", "r" };
            const char* kl[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
            for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, pre + k[i], kl[i], ec[e])->setBounds (236 + i * 64, y0 + 30, 64, 96);
            auto* b = add<ModBadge> (*this, owned, sE1 + e, "ENV " + juce::String (e + 1), ec[e]); b->setBounds (420, y0 + 4, 74, 18); badges.push_back (b);
        }
        const char* vids[] = { "voices", "glide", "bend", "velsens" };
        const char* vl[] = { "VOICES", "GLIDE", "BEND", "VELOCITY" };
        for (int i = 0; i < 4; ++i) add<Knob> (*this, owned, st, vids[i], vl[i], C::blue)->setBounds (516 + (i % 2) * 112, 30 + (i / 2) * 110, 108, 104);
        add<Toggle> (*this, owned, st, "legato", "LEGATO", C::blue)->setBounds (520, 252, 104, 24);
        const int srcs[] = { sVel, sMW, sAT, sKey, sRnd, sBend };
        const char* names[] = { "VELOCITY", "MOD WHEEL", "AFTERTOUCH", "KEY TRACK", "NOTE RND", "PITCH BEND" };
        for (int i = 0; i < 6; ++i)
        {
            auto* b = add<ModBadge> (*this, owned, srcs[i], names[i], C::text); b->setBounds (516 + (i % 2) * 112, 300 + (i / 2) * 24, 106, 18); badges.push_back (b);
        }
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 500, 134 }, "ENV 1  -  AMP", C::green);
        panel (g, { 0, 139, 500, 134 }, "ENV 2  -  FILTER", C::amber);
        panel (g, { 0, 278, 500, 134 }, "ENV 3  -  MOD", C::magenta);
        panel (g, { 508, 0, 240, 412 }, "VOICE / PERFORMANCE", C::blue);
        g.setFont (font (9.0f)); g.setColour (C::dim);
        g.drawText ("DRAG A SOURCE ONTO ANY KNOB", 516, 280, 224, 14, juce::Justification::centredLeft);
        // live wheel LEDs
        const float mw = proc.modSourceValue (sMW), pb = proc.modSourceValue (sBend);
        for (int k = 0; k < 12; ++k)
        {
            drawLed (g, { 524.0f + k * 9.0f, 386.0f }, 2.4f, C::cyan, mw * 12.0f > (float) k ? 1.0f : 0.0f);
            const float c = (float) k - 5.5f;
            drawLed (g, { 636.0f + k * 9.0f, 386.0f }, 2.4f, C::magenta, (pb > 0 ? c > 0 && c < pb * 6.0f + 0.5f : c < 0 && c > pb * 6.0f - 0.5f) ? 1.0f : 0.0f);
        }
        g.drawText ("MOD", 520, 394, 100, 12, juce::Justification::centredLeft);
        g.drawText ("BEND", 632, 394, 100, 12, juce::Justification::centredLeft);
    }
    void tick() override
    {
        for (auto* e : ev) if (e->changed()) e->repaint();
        repaint (516, 378, 230, 30);
    }
private:
    APVTS& st; LumenSynthProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> owned;
    EnvView* ev[3] {};
};

//==============================================================================
class LfoPage : public Page
{
public:
    LfoPage (LumenSynthProcessor& p, std::vector<ModBadge*>& badges) : st (p.apvts)
    {
        const juce::Colour cols[4] = { C::blue, C::violet, C::cyan, C::magenta };
        for (int l = 0; l < 4; ++l)
        {
            const int x0 = (l % 2) * 378, y0 = (l / 2) * 210;
            const juce::String pre = "l" + juce::String (l + 1) + "_";
            add<Combo> (*this, owned, st, pre + "shape", cols[l])->setBounds (x0 + 70, y0 + 4, 104, 20);
            add<Combo> (*this, owned, st, pre + "mode", cols[l])->setBounds (x0 + 178, y0 + 4, 88, 20);
            auto* b = add<ModBadge> (*this, owned, sL1 + l, "LFO " + juce::String (l + 1), cols[l]); b->setBounds (x0 + 272, y0 + 5, 90, 18); badges.push_back (b);
            ed[l] = add<LfoEditor> (*this, owned, p, l, cols[l]); ed[l]->setBounds (x0 + 8, y0 + 28, 244, 166);
            rate[l] = add<Knob> (*this, owned, st, pre + "rate", "RATE", cols[l]); rate[l]->setBounds (x0 + 258, y0 + 28, 104, 98);
            div[l] = add<Combo> (*this, owned, st, pre + "div", cols[l]); div[l]->setBounds (x0 + 258, y0 + 134, 104, 22);
            add<Toggle> (*this, owned, st, pre + "sync", "SYNC", cols[l])->setBounds (x0 + 258, y0 + 164, 104, 24);
        }
    }
    void paint (juce::Graphics& g) override
    {
        const juce::Colour cols[4] = { C::blue, C::violet, C::cyan, C::magenta };
        for (int l = 0; l < 4; ++l)
        {
            const float x0 = (float) ((l % 2) * 378), y0 = (float) ((l / 2) * 210);
            panel (g, { x0, y0, 370, 202 }, "LFO " + juce::String (l + 1), cols[l]);
            drawLed (g, { x0 + 16.0f, y0 + 13.0f }, 3.4f, cols[l], 0.5f + 0.5f * ed[l]->value());
        }
    }
    void tick() override
    {
        for (int l = 0; l < 4; ++l)
        {
            ed[l]->repaint();
            repaint ((l % 2) * 378 + 4, (l / 2) * 210 + 2, 26, 24);
            const bool sync = pv (st, "l" + juce::String (l + 1) + "_sync") > 0.5f;
            rate[l]->setAlpha (sync ? 0.35f : 1.0f); div[l]->setAlpha (sync ? 1.0f : 0.35f);
        }
    }
private:
    APVTS& st;
    std::vector<std::unique_ptr<juce::Component>> owned;
    LfoEditor* ed[4] {}; Knob* rate[4] {}; Combo* div[4] {};
};

//==============================================================================
class MatrixPage : public Page
{
public:
    MatrixPage (LumenSynthProcessor& p) : st (p.apvts), proc (p)
    {
        for (int m = 0; m < kSlots; ++m)
        {
            const juce::String pre = "m" + juce::String (m + 1) + "_";
            const int y = 40 + m * 23;
            add<Combo> (*this, owned, st, pre + "src", C::cyan)->setBounds (30, y, 150, 19);
            add<Combo> (*this, owned, st, pre + "dst", C::magenta)->setBounds (250, y, 186, 19);
            auto* sl = add<juce::Slider> (*this, owned);
            sl->setSliderStyle (juce::Slider::LinearHorizontal); sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            sl->setColour (juce::Slider::trackColourId, C::green);
            sl->setDoubleClickReturnValue (true, 0.0);
            sl->setBounds (444, y, 224, 19);
            att[m] = std::make_unique<APVTS::SliderAttachment> (st, pre + "amt", *sl);
            auto* lb = add<juce::Label> (*this, owned);
            lb->setFont (mono (11.0f)); lb->setColour (juce::Label::textColourId, C::green); lb->setJustificationType (juce::Justification::centredRight);
            lb->setBounds (668, y, 50, 19);
            sl->onValueChange = [sl, lb] { const double v = sl->getValue(); lb->setText ((v > 0 ? "+" : "") + juce::String (v * 100.0, 0) + "%", juce::dontSendNotification); };
            sl->onValueChange();
            auto* x = add<juce::TextButton> (*this, owned, "x");
            x->setColour (juce::TextButton::buttonColourId, C::red);
            x->setBounds (722, y, 20, 19);
            x->onClick = [this, m] { proc.clearModSlot (m); };
        }
    }
    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 748, 412 }, "MOD MATRIX  -  16 SLOTS", C::green);
        g.setFont (font (10.0f)); g.setColour (C::dim);
        g.drawText ("SOURCE", 30, 22, 150, 14, juce::Justification::centredLeft);
        g.drawText ("LIVE", 184, 22, 60, 14, juce::Justification::centred);
        g.drawText ("DESTINATION", 250, 22, 186, 14, juce::Justification::centredLeft);
        g.drawText ("AMOUNT", 444, 22, 224, 14, juce::Justification::centred);
        for (int m = 0; m < kSlots; ++m)
        {
            const juce::String pre = "m" + juce::String (m + 1) + "_";
            const int y = 40 + m * 23;
            const int src = (int) pv (st, pre + "src");
            const bool active = src > 0 && pv (st, pre + "dst") > 0.5f && std::abs (pv (st, pre + "amt")) > 1.0e-4f;
            drawLed (g, { 15.0f, y + 9.5f }, 3.2f, C::green, active ? 1.0f : 0.0f);
            // live source meter
            const float v = src > 0 ? proc.modSourceValue (src) : 0.0f;
            const bool bip = isBipolarSrc (src);
            for (int k = 0; k < 8; ++k)
            {
                const float t = (k + 0.5f) / 8.0f;
                const float lit = bip ? ((v >= 0 && t >= 0.5f && t - 0.5f <= v * 0.5f) || (v < 0 && t < 0.5f && 0.5f - t <= -v * 0.5f) ? 1.0f : 0.0f)
                                      : (t <= v ? 1.0f : 0.0f);
                drawLed (g, { 188.0f + k * 7.0f, y + 9.5f }, 2.0f, sourceColour (src), src > 0 ? lit : 0.0f);
            }
        }
    }
    void tick() override { repaint (6, 40, 240, kSlots * 23); }
private:
    APVTS& st; LumenSynthProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<APVTS::SliderAttachment> att[kSlots];
};

//==============================================================================
// FX rack: reorderable list on the left, editor for the selected effect on the right
class FxPage : public Page
{
public:
    FxPage (LumenSynthProcessor& p) : st (p.apvts), proc (p)
    {
        static const char* onIds[] = { "hyp_on", "dist_on", "fla_on", "pha_on", "cho_on", "dly_on", "cmp_on", "rev_on", "eq_on", "ott_on", "pump_on" };
        for (int i = 0; i < kNumFx; ++i)
        {
            rows[i].power = add<Toggle> (*this, owned, st, onIds[i], "", fxCol (i), true);
            rows[i].sel = add<juce::TextButton> (*this, owned, fxNames()[i]);
            rows[i].sel->setColour (juce::TextButton::buttonColourId, fxCol (i));
            rows[i].sel->onClick = [this, i] { select (i); };
            rows[i].up = add<juce::TextButton> (*this, owned, juce::CharPointer_UTF8 ("\xe2\x96\xb2"));
            rows[i].dn = add<juce::TextButton> (*this, owned, juce::CharPointer_UTF8 ("\xe2\x96\xbc"));
            for (auto* b : { rows[i].up, rows[i].dn }) b->setColour (juce::TextButton::buttonColourId, C::dim);
            rows[i].up->onClick = [this, i] { move (i, -1); };
            rows[i].dn->onClick = [this, i] { move (i, 1); };
            editors[i] = add<juce::Component> (*this, owned);
            editors[i]->setBounds (208, 0, 540, 262);
            editors[i]->setInterceptsMouseClicks (false, true);
            buildEditor (i);
        }
        add<Knob> (*this, owned, st, "vol", "VOLUME", C::green)->setBounds (226, 292, 100, 112);
        add<Toggle> (*this, owned, st, "clip", "SOFT CLIP", C::green)->setBounds (346, 330, 110, 24);
        layoutRack();
        select (0);
    }
    static juce::Colour fxCol (int i)
    {
        const juce::Colour c[] = { C::cyan, C::orange, C::blue, C::violet, C::cyan, C::blue, C::green, C::violet, C::amber, C::amber, C::magenta };
        return c[i];
    }
    void layoutRack()
    {
        const auto order = proc.getFxOrder();
        for (int slot = 0; slot < kNumFx; ++slot)
        {
            const int id = order[(size_t) slot];
            const int y = 28 + slot * 34;
            rows[id].power->setBounds (8, y + 5, 20, 20);
            rows[id].sel->setBounds (32, y + 3, 118, 26);
            rows[id].up->setBounds (154, y + 3, 20, 12);
            rows[id].dn->setBounds (154, y + 17, 20, 12);
            rows[id].slot = slot;
        }
        lastOrder = proc.orderVersion.load();
        repaint();
    }
    void select (int i)
    {
        selected = i;
        for (int k = 0; k < kNumFx; ++k) editors[k]->setVisible (k == i);
        repaint();
    }
    void move (int id, int delta) { proc.moveFx (rows[id].slot, delta); layoutRack(); }

    void buildEditor (int i)
    {
        auto& e = *editors[i];
        const auto col = fxCol (i);
        auto knob = [&] (const char* id, const char* l, int k, int row = 0, bool bip = false)
        { add<Knob> (e, owned, st, id, l, col, bip)->setBounds (16 + k * 104, 40 + row * 110, 100, 108); };
        switch (i)
        {
            case fxHyper:   knob ("hyp_detune", "DETUNE", 0); knob ("hyp_dim", "DIMENSION", 1); knob ("hyp_mix", "MIX", 2); break;
            case fxDist:    add<Combo> (e, owned, st, "dist_mode", col)->setBounds (16, 40, 150, 24); knob ("dist_drive", "DRIVE", 2); knob ("dist_mix", "MIX", 3); break;
            case fxFlanger: knob ("fla_rate", "RATE", 0); knob ("fla_depth", "DEPTH", 1); knob ("fla_fb", "FEEDBACK", 2, 0, true); knob ("fla_mix", "MIX", 3); break;
            case fxPhaser:  knob ("pha_rate", "RATE", 0); knob ("pha_depth", "DEPTH", 1); knob ("pha_fb", "FEEDBACK", 2); knob ("pha_mix", "MIX", 3); break;
            case fxChorus:  knob ("cho_rate", "RATE", 0); knob ("cho_depth", "DEPTH", 1); knob ("cho_mix", "MIX", 2); break;
            case fxDelay:   add<Combo> (e, owned, st, "dly_div", col)->setBounds (16, 40, 120, 24);
                            add<Toggle> (e, owned, st, "dly_ping", "PING PONG", col)->setBounds (16, 72, 120, 24);
                            knob ("dly_fb", "FEEDBACK", 2); knob ("dly_tone", "TONE", 3); knob ("dly_mix", "MIX", 4); break;
            case fxComp:    knob ("cmp_thresh", "THRESHOLD", 0); knob ("cmp_ratio", "RATIO", 1); knob ("cmp_attack", "ATTACK", 2); knob ("cmp_release", "RELEASE", 3); knob ("cmp_gain", "GAIN", 4); break;
            case fxReverb:  knob ("rev_size", "SIZE", 0); knob ("rev_decay", "DECAY", 1); knob ("rev_damp", "DAMP", 2); knob ("rev_mix", "MIX", 3); break;
            case fxEq:      knob ("eq_low", "LOW", 0, 0, true); knob ("eq_mid", "MID", 1, 0, true); knob ("eq_mfreq", "MID FREQ", 2); knob ("eq_high", "HIGH", 3, 0, true); break;
            case fxOtt:     knob ("ott_depth", "DEPTH", 0); knob ("ott_time", "TIME", 1); break;
            case fxPump:    add<Combo> (e, owned, st, "pump_div", col)->setBounds (16, 40, 120, 24); knob ("pump_depth", "DEPTH", 2); knob ("pump_curve", "RELEASE", 3); break;
            default: break;
        }
    }

    void paint (juce::Graphics& g) override
    {
        panel (g, { 0, 0, 200, 412 }, "FX RACK", C::magenta);
        // highlight the selected row
        const int y = 28 + rows[selected].slot * 34;
        g.setColour (fxCol (selected).withAlpha (0.12f)); g.fillRoundedRectangle (4.0f, (float) y, 192.0f, 32.0f, 5.0f);
        g.setColour (fxCol (selected).withAlpha (0.6f)); g.drawRoundedRectangle (4.0f, (float) y, 192.0f, 32.0f, 5.0f, 1.0f);
        // signal-flow LEDs between rows
        for (int s = 0; s < kNumFx - 1; ++s) drawLed (g, { 184.0f, 28.0f + s * 34 + 33.0f }, 1.6f, C::green, 0.5f);

        const auto col = fxCol (selected);
        panel (g, { 208, 0, 540, 262 }, fxNames()[selected], col);
        panel (g, { 208, 270, 266, 142 }, "MASTER", C::green);
        panel (g, { 482, 270, 266, 142 }, "OUTPUT", C::green);

        // per-effect visualisers
        auto vis = juce::Rectangle<float> (224.0f, 162.0f, 508.0f, 88.0f);
        switch (selected)
        {
            case fxComp:
            {
                lcd (g, vis, col);
                const float gr = proc.compGr.load();
                g.setFont (font (9.5f)); g.setColour (col.withAlpha (0.7f));
                g.drawText ("GAIN REDUCTION", vis.reduced (8, 6), juce::Justification::topLeft);
                ledMeterReduction (g, vis.reduced (12.0f, 30.0f), -gr, 24.0f, col);
                break;
            }
            case fxOtt:
            {
                lcd (g, vis, col);
                const char* bn[] = { "LOW", "MID", "HIGH" };
                for (int b = 0; b < 3; ++b)
                {
                    const float db = juce::Decibels::gainToDecibels (proc.ottBand[b].load());
                    auto rr = juce::Rectangle<float> (vis.getX() + 60.0f + b * 150.0f, vis.getY() + 14.0f, 110.0f, 56.0f);
                    for (int k = 0; k < 12; ++k)
                    {
                        const float t = (float) k - 5.5f;
                        const bool lit = db >= 0 ? (t > 0 && t < db / 2.0f + 0.5f) : (t < 0 && t > db / 2.0f - 0.5f);
                        drawLed (g, { rr.getX() + k * 9.0f, rr.getCentreY() }, 2.6f, db >= 0 ? C::green : C::amber, lit ? 1.0f : 0.0f);
                    }
                    g.setFont (font (9.0f)); g.setColour (C::dim);
                    g.drawText (bn[b], rr.withY (rr.getCentreY() + 8.0f).withHeight (14.0f), juce::Justification::centred);
                }
                break;
            }
            case fxPump:
            {
                lcd (g, vis, col);
                const float pg = proc.pumpGain.load();
                for (int k = 0; k < 40; ++k) drawLed (g, { vis.getX() + 16.0f + k * 12.0f, vis.getCentreY() }, 3.0f, col, pv (st, "pump_on") > 0.5f && pg * 40.0f > (float) k ? 1.0f : 0.0f);
                break;
            }
            case fxEq:
            {
                lcd (g, vis, col);
                juce::Path pth;
                lumen::synth::Biquad b[3];
                b[0].set (0, 110.0, pv (st, "eq_low"), 0.7, 48000.0); b[1].set (1, pv (st, "eq_mfreq"), pv (st, "eq_mid"), 0.8, 48000.0); b[2].set (2, 7500.0, pv (st, "eq_high"), 0.7, 48000.0);
                for (int i = 0; i <= 120; ++i)
                {
                    const double f = 20.0 * std::pow (1000.0, i / 120.0), w = 2.0 * 3.141592653589793 * f / 48000.0;
                    double db = 0;
                    for (auto& q : b)
                    {
                        const std::complex<double> z = std::exp (std::complex<double> (0, -w));
                        const auto num = q.b0 + q.b1 * z + q.b2 * z * z, den = 1.0 + q.a1 * z + q.a2 * z * z;
                        db += 20.0 * std::log10 (std::abs (num / den));
                    }
                    const float px = vis.getX() + 8 + (vis.getWidth() - 16) * i / 120.0f, py = vis.getCentreY() - (float) db * 2.4f;
                    if (i == 0) pth.startNewSubPath (px, py); else pth.lineTo (px, py);
                }
                glowPath (g, pth, col, 1.5f);
                break;
            }
            default:
            {
                lcd (g, vis, col);
                matrix.draw (g, vis.reduced (12.0f, 20.0f), fxNames()[selected] + (pv (st, rowsOnId (selected)) > 0.5f ? "  ON" : "  OFF"), col, 3.4f, juce::Justification::centred, true, 10.6f);
                break;
            }
        }
        // output meters
        auto o = juce::Rectangle<float> (494.0f, 302.0f, 242.0f, 100.0f);
        ledMeter (g, { o.getX(), o.getY() + 6.0f, o.getWidth(), 12.0f }, mL, hL, false, 30);
        ledMeter (g, { o.getX(), o.getY() + 24.0f, o.getWidth(), 12.0f }, mR, hR, false, 30);
        const float pk = juce::Decibels::gainToDecibels (juce::jmax (hL, hR), -60.0f);
        peakMatrix.draw (g, { o.getX(), o.getY() + 50.0f, o.getWidth(), 34.0f }, pk <= -59.0f ? juce::String ("-INF DB") : juce::String (pk, 1) + " DB",
                         pk > -0.5f ? C::red : C::green, 3.0f, juce::Justification::centred, false, 10.6f);
    }
    static const char* rowsOnId (int i)
    {
        static const char* onIds[] = { "hyp_on", "dist_on", "fla_on", "pha_on", "cho_on", "dly_on", "cmp_on", "rev_on", "eq_on", "ott_on", "pump_on" };
        return onIds[i];
    }
    static void ledMeterReduction (juce::Graphics& g, juce::Rectangle<float> r, float grDb, float range, juce::Colour col)
    {
        const int segs = 40;
        for (int k = 0; k < segs; ++k)
        {
            const float t = (k + 0.5f) / segs;
            drawLed (g, { r.getX() + r.getWidth() * t, r.getCentreY() }, 3.0f, col, t * range <= grDb ? 1.0f : 0.0f);
        }
    }
    void tick() override
    {
        const float l = proc.peakL.load(), r = proc.peakR.load();
        mL = juce::jmax (l, mL * 0.82f); mR = juce::jmax (r, mR * 0.82f);
        if (++holdT > 45) { holdT = 0; hL = l; hR = r; }
        hL = juce::jmax (hL, l); hR = juce::jmax (hR, r);
        if (proc.orderVersion.load() != lastOrder) layoutRack();
        repaint (482, 270, 266, 142);
        repaint (208, 150, 540, 112);
    }
private:
    struct Row { Toggle* power = nullptr; juce::TextButton* sel = nullptr; juce::TextButton* up = nullptr; juce::TextButton* dn = nullptr; int slot = 0; };
    APVTS& st; LumenSynthProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> owned;
    Row rows[kNumFx];
    juce::Component* editors[kNumFx] {};
    int selected = 0, lastOrder = -1;
    float mL = 0, mR = 0, hL = 0, hR = 0; int holdT = 0;
    DotMatrix peakMatrix, matrix;
};

//==============================================================================
class ArpPage : public Page
{
public:
    ArpPage (LumenSynthProcessor& p) : st (p.apvts), proc (p)
    {
        add<Toggle> (*this, owned, st, "arp_on", "", C::cyan, true)->setBounds (6, 3, 20, 20);
        add<Combo> (*this, owned, st, "arp_mode", C::cyan)->setBounds (16, 36, 160, 24);
        add<Combo> (*this, owned, st, "arp_rate", C::cyan)->setBounds (16, 70, 160, 24);
        add<Knob> (*this, owned, st, "arp_oct", "OCTAVES", C::cyan)->setBounds (196, 30, 104, 104);
        add<Knob> (*this, owned, st, "arp_gate", "GATE", C::cyan)->setBounds (304, 30, 104, 104);
        add<Knob> (*this, owned, st, "arp_swing", "SWING", C::cyan)->setBounds (412, 30, 104, 104);
        add<Combo> (*this, owned, st, "chord", C::magenta)->setBounds (16, 292, 160, 24);
    }
    void paint (juce::Graphics& g) override
    {
        const bool on = pv (st, "arp_on") > 0.5f;
        panel (g, { 0, 0, 748, 250 }, "ARPEGGIATOR", C::cyan, on);
        panel (g, { 0, 258, 370, 154 }, "CHORD MEMORY", C::magenta, (int) pv (st, "chord") > 0);
        panel (g, { 378, 258, 370, 154 }, "HOW TO", C::green);
        // 16 step LEDs
        auto steps = juce::Rectangle<float> (16.0f, 150.0f, 716.0f, 80.0f);
        lcd (g, steps, C::cyan);
        for (int k = 0; k < 16; ++k)
        {
            const float x = steps.getX() + 30.0f + k * (steps.getWidth() - 60.0f) / 15.0f;
            drawLed (g, { x, steps.getCentreY() - 8.0f }, 6.0f, k % 4 == 0 ? C::magenta : C::cyan, on && k == step ? 1.0f : 0.0f);
            g.setFont (font (9.0f)); g.setColour (C::dim);
            g.drawText (juce::String (k + 1), (int) x - 10, (int) steps.getCentreY() + 8, 20, 12, juce::Justification::centred);
        }
        // chord display: mini piano (C-1 octave .. +2 octaves), lit keys glow
        const auto& iv = Arp::chordIntervals ((int) pv (st, "chord"));
        auto kb = juce::Rectangle<float> (190.0f, 290.0f, 168.0f, 96.0f);
        lcd (g, kb, C::magenta);
        auto keys = kb.reduced (8.0f, 10.0f).withTrimmedBottom (14.0f);
        const int whites = 22;                       // 3 octaves + C
        const float ww = keys.getWidth() / whites;
        auto isLit = [&] (int s) { return std::find (iv.begin(), iv.end(), s) != iv.end(); };
        static const int whitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int w = 0; w < whites; ++w)
        {
            const int semi = (w / 7) * 12 + whitePc[w % 7] - 12;
            auto r = juce::Rectangle<float> (keys.getX() + w * ww + 0.5f, keys.getY(), ww - 1.0f, keys.getHeight());
            const bool lit = isLit (semi);
            g.setColour (lit ? (semi == 0 ? C::cyan : C::magenta) : juce::Colour (0xff2a3340)); g.fillRoundedRectangle (r, 1.5f);
            if (lit) { g.setColour ((semi == 0 ? C::cyan : C::magenta).withAlpha (0.3f)); g.fillRoundedRectangle (r.expanded (1.5f), 2.0f); }
        }
        for (int w = 0; w < whites - 1; ++w)
        {
            const int pc = whitePc[w % 7];
            if (pc == 4 || pc == 11) continue;
            const int semi = (w / 7) * 12 + pc + 1 - 12;
            auto r = juce::Rectangle<float> (keys.getX() + (w + 1) * ww - ww * 0.32f, keys.getY(), ww * 0.64f, keys.getHeight() * 0.6f);
            const bool lit = isLit (semi);
            g.setColour (lit ? C::magenta : juce::Colour (0xff05070a)); g.fillRoundedRectangle (r, 1.0f);
        }
        g.setFont (font (8.5f)); g.setColour (C::dim);
        g.drawText ("ROOT", (int) (keys.getX() + 7 * ww - 6), (int) keys.getBottom() + 1, 40, 12, juce::Justification::centredLeft);
        g.setFont (font (10.5f, false)); g.setColour (C::text.withAlpha (0.8f));
        g.drawFittedText ("CHORD: one key plays a full chord.\nARP: hold keys, the arp plays them in time with your DAW (sync to tempo).\n"
                          "Both work together - pick a chord, turn the arp on and hold one key.",
                          juce::Rectangle<int> (392, 288, 344, 110), juce::Justification::topLeft, 6);
    }
    void tick() override
    {
        const int f = proc.arpStepFlash.load();
        if (f != lastFlash) { lastFlash = f; step = (step + 1) % 16; repaint (16, 150, 716, 80); }
    }
private:
    APVTS& st; LumenSynthProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> owned;
    int lastFlash = 0, step = -1;
};

//==============================================================================
// Right column
class SmartPanel : public Page
{
public:
    SmartPanel (LumenSynthProcessor& p) : proc (p)
    {
        category.addItemList (categories(), 1);
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
        auto stR = juce::Rectangle<float> (10.0f, 126.0f, 248.0f, 30.0f);
        lcd (g, stR, C::cyan);
        matrix.draw (g, stR.reduced (6.0f, 2.0f), status, C::cyan.interpolatedWith (juce::Colours::white, 0.3f * flash), 2.2f, juce::Justification::centred, true, 10.0f);
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
    MacroPanel (LumenSynthProcessor& p, std::vector<ModBadge*>& badges) : proc (p)
    {
        auto& s = p.apvts;
        const juce::Colour cols[] = { C::cyan, C::magenta, C::amber, C::green };
        for (int m = 0; m < 4; ++m)
        {
            knobs[m] = add<Knob> (*this, owned, s, "macro" + juce::String (m + 1), "MACRO " + juce::String (m + 1), cols[m]);
            knobs[m]->setBounds (6 + m * 64, 22, 64, 78);
            auto* b = add<ModBadge> (*this, owned, sM1 + m, "M" + juce::String (m + 1), cols[m]); b->setBounds (14 + m * 64, 100, 48, 14); badges.push_back (b);
        }
    }
    void paint (juce::Graphics& g) override { panel (g, getLocalBounds().toFloat(), "MACROS", C::cyan); }
    void tick() override { for (int m = 0; m < 4; ++m) knobs[m]->setLabel (proc.macroNames[m].toUpperCase().substring (0, 9)); }
private:
    LumenSynthProcessor& proc;
    Knob* knobs[4] {};
    std::vector<std::unique_ptr<juce::Component>> owned;
};

class ScopePanel : public Page
{
public:
    ScopePanel (LumenSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        panel (g, getLocalBounds().toFloat(), "SCOPE", C::cyan);
        auto a = getLocalBounds().toFloat().reduced (10.0f, 6.0f).withTrimmedTop (20.0f);
        lcd (g, a, C::green);
        a = a.reduced (4.0f, 3.0f);
        const int N = LumenSynthProcessor::kScope;
        const int wp = proc.scopePos.load (std::memory_order_acquire);
        float buf[LumenSynthProcessor::kScope];
        for (int i = 0; i < N; ++i) buf[i] = proc.scope[(wp + i) & (N - 1)];
        int start = N - 700;
        for (int i = N - 700; i > 300; --i) if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) { start = i; break; }
        float pk = 0.05f; for (int i = start; i < start + 600 && i < N; ++i) pk = juce::jmax (pk, std::abs (buf[i]));
        gainSm += (1.0f / pk - gainSm) * 0.2f;
        g.setColour (C::border); g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
        juce::Path wave;
        for (int k = 0; k <= 200; ++k)
        {
            const int idx = juce::jmin (N - 1, start + k * 3);
            const float v = juce::jlimit (-1.0f, 1.0f, buf[idx] * gainSm * 0.9f);
            const float x = a.getX() + a.getWidth() * (float) k / 200.0f, y = a.getCentreY() - v * a.getHeight() * 0.48f;
            if (k == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }
        glowPath (g, wave, C::cyan, 1.4f);
    }
    void tick() override { repaint(); }
private:
    LumenSynthProcessor& proc;
    float gainSm = 1.0f;
};

//==============================================================================
// Preset browser overlay: search, categories, favourites, user presets
class Browser : public Page, private juce::ListBoxModel
{
public:
    Browser (LumenSynthProcessor& p) : proc (p)
    {
        search.setTextToShowWhenEmpty ("SEARCH PRESETS...", C::dim);
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff030508));
        search.setColour (juce::TextEditor::outlineColourId, C::border);
        search.setColour (juce::TextEditor::focusedOutlineColourId, C::cyan);
        search.setColour (juce::TextEditor::textColourId, C::cyan);
        search.setFont (font (14.0f));
        search.onTextChange = [this] { refilter(); };
        addAndMakeVisible (search);
        juce::StringArray cats { "ALL", "FAVOURITES", "USER", "BASS", "LEAD", "PLUCK", "PAD", "KEYS", "FX" };
        cats.set (1, juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) + " FAVOURITES");
        for (int i = 0; i < cats.size(); ++i)
        {
            auto* b = catBtns.add (new juce::TextButton (cats[i]));
            b->setColour (juce::TextButton::buttonColourId, i == 1 ? C::amber : C::cyan);
            b->onClick = [this, i] { cat = i; refilter(); };
            addAndMakeVisible (b);
        }
        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff030508));
        list.setColour (juce::ListBox::outlineColourId, C::border);
        list.setOutlineThickness (1);
        addAndMakeVisible (list);
        for (auto* b : { &loadBtn, &delBtn, &closeBtn }) addAndMakeVisible (*b);
        loadBtn.setColour (juce::TextButton::buttonColourId, C::green);
        delBtn.setColour (juce::TextButton::buttonColourId, C::red);
        closeBtn.setColour (juce::TextButton::buttonColourId, C::dim.brighter (0.5f));
        loadBtn.onClick = [this] { loadSelected(); };
        delBtn.onClick = [this]
        {
            const int r = list.getSelectedRow();
            if (! juce::isPositiveAndBelow (r, (int) shown.size()) || ! shown[(size_t) r].user) return;
            const auto e = shown[(size_t) r];
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Delete preset")
                                              .withMessage ("Delete \"" + e.name + "\"?").withButton ("Delete").withButton ("Cancel").withAssociatedComponent (this),
                                          [this, e] (int res) { if (res == 1) { proc.deleteUserPreset (e); refresh(); } });
        };
        closeBtn.onClick = [this] { setVisible (false); };
    }
    void refresh() { all = proc.listPresets(); refilter(); }
    void resized() override
    {
        search.setBounds (180, 10, 600, 30);
        for (int i = 0; i < catBtns.size(); ++i) catBtns[i]->setBounds (10, 50 + i * 36, 160, 30);
        list.setBounds (180, 50, 838, 310);
        loadBtn.setBounds (180, 370, 140, 32);
        delBtn.setBounds (330, 370, 110, 32);
        closeBtn.setBounds (908, 370, 110, 32);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff06080c)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        dotGrid (g, getLocalBounds().toFloat().reduced (6.0f), 9.0f, juce::Colours::white, 0.03f);
        g.setColour (C::cyan.withAlpha (0.5f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.2f);
        g.setFont (font (11.5f)); g.setColour (C::cyan);
        g.drawText ("PRESET BROWSER", 10, 14, 160, 20, juce::Justification::centredLeft);
        g.setFont (font (10.0f)); g.setColour (C::dim);
        g.drawText (juce::String ((int) shown.size()) + " PRESETS  -  DOUBLE-CLICK TO LOAD  -  CLICK THE STAR FOR FAVOURITES", 460, 376, 440, 20, juce::Justification::centredLeft);
        g.setFont (font (9.0f));
        g.drawFittedText ("YOUR PRESETS ARE SAVED IN\nDocuments / Lumen Audio / Lumen Synth / Presets", juce::Rectangle<int> (792, 8, 226, 34), juce::Justification::centredRight, 2);
    }
    // ListBoxModel
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& e = shown[(size_t) row];
        if (sel) { g.setColour (C::cyan.withAlpha (0.15f)); g.fillRect (0, 0, w, h); }
        const bool fav = proc.isFavourite (e);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.setColour (fav ? C::amber : C::dim.withAlpha (0.6f));
        g.drawText (juce::CharPointer_UTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86"), 6, 0, 22, h, juce::Justification::centred);
        g.setFont (font (13.0f)); g.setColour (sel ? juce::Colours::white : C::text);
        g.drawText (e.name, 36, 0, w - 260, h, juce::Justification::centredLeft);
        g.setFont (font (10.0f)); g.setColour (C::magenta.withAlpha (0.85f));
        g.drawText (e.category.toUpperCase(), w - 220, 0, 110, h, juce::Justification::centredLeft);
        g.setColour (e.user ? C::green : C::dim);
        g.drawText (e.user ? "USER" : "FACTORY", w - 100, 0, 90, h, juce::Justification::centredRight);
        g.setColour (C::border); g.drawHorizontalLine (h - 1, 0.0f, (float) w);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.x < 30 && juce::isPositiveAndBelow (row, (int) shown.size())) { proc.toggleFavourite (shown[(size_t) row]); list.repaintRow (row); if (cat == 1) refilter(); }
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }
    void returnKeyPressed (int) override { loadSelected(); }

private:
    void loadSelected()
    {
        const int r = list.getSelectedRow();
        if (juce::isPositiveAndBelow (r, (int) shown.size())) proc.loadPresetEntry (shown[(size_t) r]);
    }
    void refilter()
    {
        shown.clear();
        const auto q = search.getText().trim().toLowerCase();
        static const char* catNames[] = { "", "", "", "Bass", "Lead", "Pluck", "Pad", "Keys", "FX" };
        for (auto& e : all)
        {
            if (q.isNotEmpty() && ! e.name.toLowerCase().contains (q) && ! e.category.toLowerCase().contains (q)) continue;
            if (cat == 1 && ! proc.isFavourite (e)) continue;
            if (cat == 2 && ! e.user) continue;
            if (cat >= 3 && ! e.category.equalsIgnoreCase (catNames[cat])) continue;
            shown.push_back (e);
        }
        list.updateContent(); list.repaint();
        for (int i = 0; i < catBtns.size(); ++i) catBtns[i]->setAlpha (i == cat ? 1.0f : 0.55f);
        repaint();
    }
    LumenSynthProcessor& proc;
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> catBtns;
    juce::ListBox list;
    juce::TextButton loadBtn { "LOAD" }, delBtn { "DELETE" }, closeBtn { "CLOSE" };
    std::vector<LumenSynthProcessor::PresetEntry> all, shown;
    int cat = 0;
};
} // namespace

//==============================================================================
void LedKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour)
{
    auto r = area.reduced (0.5f, 0.0f);
    g.setColour (down ? C::cyan.interpolatedWith (juce::Colours::white, 0.2f) : over ? juce::Colour (0xffb4bac3) : juce::Colour (0xffa3a9b2));
    g.fillRoundedRectangle (r.withTrimmedTop (-3.0f), 2.5f);
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.drawVerticalLine ((int) area.getRight(), area.getY(), area.getBottom());
    if (note % 12 == 0)
    {
        g.setColour (juce::Colour (0xff3a3f47));
        g.setFont (font (8.5f, false));
        g.drawText ("C" + juce::String (note / 12 - 2), r.removeFromBottom (14.0f), juce::Justification::centred);
    }
}

void LedKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour)
{
    auto r = area.reduced (0.5f, 0.0f);
    g.setColour (down ? C::cyan : over ? juce::Colour (0xff3a4048) : juce::Colour (0xff22262c));
    g.fillRoundedRectangle (r.withTrimmedTop (-3.0f), 2.0f);
}

//==============================================================================
LumenSynthEditor::LumenSynthEditor (LumenSynthProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p.keyboardState)
{
    setLookAndFeel (&lnf);
    ModHost::current() = this;
    addAndMakeVisible (root);
    root.setBounds (0, 0, kW, kH);

    pages[0] = std::make_unique<OscPage> (proc);
    pages[1] = std::make_unique<FilterPage> (proc.apvts);
    pages[2] = std::make_unique<EnvPage> (proc, badges);
    pages[3] = std::make_unique<LfoPage> (proc, badges);
    pages[4] = std::make_unique<MatrixPage> (proc);
    pages[5] = std::make_unique<FxPage> (proc);
    pages[6] = std::make_unique<ArpPage> (proc);
    for (auto& pg : pages) root.addChildComponent (*pg);
    smart  = std::make_unique<SmartPanel> (proc);
    macros = std::make_unique<MacroPanel> (proc, badges);
    scope  = std::make_unique<ScopePanel> (proc);
    browser = std::make_unique<Browser> (proc);
    root.addAndMakeVisible (*smart); root.addAndMakeVisible (*macros); root.addAndMakeVisible (*scope);
    root.addChildComponent (*browser);

    const char* names[] = { "OSC", "FILTER", "ENV", "LFO", "MATRIX", "FX", "ARP" };
    const juce::Colour cols[] = { C::cyan, C::amber, C::green, C::blue, C::green, C::magenta, C::cyan };
    for (int i = 0; i < kPages; ++i)
    {
        tabs[i].setButtonText (names[i]);
        tabs[i].setColour (juce::TextButton::buttonColourId, C::cyan);
        tabs[i].getProperties().set ("tab", true);
        tabs[i].onClick = [this, i] { showPage (i); };
        (void) cols;
        root.addAndMakeVisible (tabs[i]);
    }
    for (auto* b : { &prevBtn, &nextBtn }) { b->setColour (juce::TextButton::buttonColourId, C::cyan); root.addAndMakeVisible (*b); }
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    presetArea.setAlpha (0.0f);
    presetArea.onClick = [this] { static_cast<Browser*> (browser.get())->refresh(); browser->setVisible (! browser->isVisible()); browser->toFront (true); };
    presetArea.setTooltip ("Open the preset browser");
    root.addAndMakeVisible (presetArea);
    browseBtn.setColour (juce::TextButton::buttonColourId, C::cyan);
    saveBtn.setColour (juce::TextButton::buttonColourId, C::green);
    browseBtn.onClick = presetArea.onClick;
    saveBtn.onClick = [this] { openSaveDialog(); };
    root.addAndMakeVisible (browseBtn); root.addAndMakeVisible (saveBtn);

    hqToggle = std::make_unique<Toggle> (proc.apvts, "hq", "HQ", C::amber);
    hqToggle->setTooltip ("HQ (on by default): voices run at 2x with oscillators band-limited for the output rate, so filter drive and resonance stay clean; distortion runs at 8x. Turn off only to save CPU");
    root.addAndMakeVisible (*hqToggle);
    driftSlider.setSliderStyle (juce::Slider::LinearBar);
    driftSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    driftSlider.setColour (juce::Slider::trackColourId, C::amber.withAlpha (0.55f));
    driftSlider.setTooltip ("ANALOG DRIFT: every unison voice wanders slightly in pitch and filter like a real analog synth - kills the static digital sound");
    driftAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "drift", driftSlider);
    root.addAndMakeVisible (driftSlider);
    warmSlider.label = "WARMTH"; warmSlider.col = C::orange;
    warmSlider.setSliderStyle (juce::Slider::LinearBar);
    warmSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    warmSlider.setTooltip ("ANALOG WARMTH: every voice goes through a soft analog-style saturation stage with small part tolerances - thicker, rounder, less 'digital'");
    warmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "warmth", warmSlider);
    root.addAndMakeVisible (warmSlider);
    zoom.addItemList ({ "75", "100", "125", "150" }, 1);
    zoom.setTooltip ("Window size (%)");
    zoom.setColour (juce::ComboBox::textColourId, C::cyan);
    zoom.setSelectedId (2, juce::dontSendNotification);
    zoom.onChange = [this] { const float s[] = { 0.75f, 1.0f, 1.25f, 1.5f }; const float k = s[juce::jlimit (0, 3, zoom.getSelectedId() - 1)]; setSize ((int) (kW * k), (int) (kH * k)); };
    root.addAndMakeVisible (zoom);

    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (24);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setKeyPressBaseOctave (5);
    root.addAndMakeVisible (keyboard);

    collectKnobs (root);
    showPage (0);
    setResizable (true, true);
    setResizeLimits (kW * 3 / 4, kH * 3 / 4, kW * 8 / 5, kH * 8 / 5);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / kH);
    setSize (kW, kH);
    startTimerHz (30);
}

LumenSynthEditor::~LumenSynthEditor()
{
    stopTimer();
    if (ModHost::current() == this) ModHost::current() = nullptr;
    setLookAndFeel (nullptr);
}

void LumenSynthEditor::collectKnobs (juce::Component& c)
{
    for (auto* ch : c.getChildren())
    {
        if (auto* k = dynamic_cast<Knob*> (ch)) { if (k->modDest() > 0) knobs.push_back (k); }
        else collectKnobs (*ch);
    }
}

void LumenSynthEditor::showPage (int idx)
{
    currentPage = idx;
    for (int i = 0; i < kPages; ++i) { pages[i]->setVisible (i == idx); tabs[i].setToggleState (i == idx, juce::dontSendNotification); }
    browser->setVisible (false);
    root.repaint (0, 64, kW, 40);
}

void LumenSynthEditor::stepPreset (int delta)
{
    const int n = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + n) % n);
}

void LumenSynthEditor::openSaveDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name and category for your preset:", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->addTextEditor ("name", proc.getPresetName().upToFirstOccurrenceOf (" *", false, false), "Name:");
    saveWindow->addComboBox ("cat", categories(), "Category:");
    if (auto* cb = saveWindow->getComboBoxComponent ("cat"))
    {
        const int idx = categories().indexOf (proc.getPresetCategory());
        cb->setSelectedItemIndex (juce::jmax (0, idx));
    }
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && saveWindow != nullptr)
        {
            const auto name = saveWindow->getTextEditorContents ("name");
            const auto cat = saveWindow->getComboBoxComponent ("cat")->getText();
            if (! proc.saveUserPreset (name, cat))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Lumen Synth", "Could not save the preset.");
        }
        saveWindow.reset();
    }), false);
}

void LumenSynthEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    root.setTransform (juce::AffineTransform::scale (s));
    for (auto& pg : pages) pg->setBounds (16, 104, 748, 412);
    smart->setBounds (776, 104, 268, 196);
    macros->setBounds (776, 308, 268, 118);
    scope->setBounds (776, 434, 268, 82);
    browser->setBounds (16, 104, 1028, 412);
    for (int i = 0; i < kPages; ++i) tabs[i].setBounds (16 + i * 106, 70, 100, 28);
    hqToggle->setBounds (776, 72, 56, 24);
    driftSlider.setBounds (838, 74, 72, 20);
    warmSlider.setBounds (914, 74, 82, 20);
    zoom.setBounds (1000, 72, 44, 24);
    prevBtn.setBounds (296, 18, 30, 30);
    nextBtn.setBounds (654, 18, 30, 30);
    presetArea.setBounds (330, 10, 320, 46);
    browseBtn.setBounds (692, 9, 62, 22);
    saveBtn.setBounds (692, 35, 62, 22);
    keyboard.setBounds (16, 526, 1028, 96);
    keyboard.setKeyWidth (1028.0f / 50.0f);
    keyboard.setBlackNoteLengthProportion (0.6f);
}

void LumenSynthEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (C::bg);
    // header bar
    g.setColour (juce::Colour (0xff111316)); g.fillRect (0, 0, kW, 64);
    g.setColour (C::border); g.fillRect (0, 64, kW, 1);
    {
        g.setColour (C::text); g.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
        g.drawText ("LUMEN", 22, 16, 120, 32, juce::Justification::centredLeft);
        g.setColour (C::cyan); g.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::plain)).withExtraKerningFactor (0.12f));
        g.drawText ("SYNTH", 132, 16, 120, 32, juce::Justification::centredLeft);
    }
    {
        auto d = juce::Rectangle<float> (290.0f, 10.0f, 400.0f, 46.0f);
        lcd (g, d, C::cyan);
        g.setColour (C::text); g.setFont (font (15.0f));
        g.drawFittedText (proc.getPresetName(), juce::Rectangle<int> (334, 24, 312, 26), juce::Justification::centred, 1);
        g.setFont (font (9.0f)); g.setColour (C::cyan);
        g.drawText (proc.getPresetCategory().toUpperCase(), 334, 12, 150, 12, juce::Justification::centredLeft);
        g.setColour (proc.isUserPreset() ? C::green : C::dim);
        g.drawText (proc.isUserPreset() ? juce::String ("USER") : juce::String (proc.getCurrentProgram() + 1) + " / " + juce::String (proc.getNumPrograms()),
                    496, 12, 150, 12, juce::Justification::centredRight);
    }
    g.setFont (font (9.0f)); g.setColour (C::dim);
    g.drawText ("VOICES  " + juce::String (proc.activeVoices.load()), 770, 10, 130, 12, juce::Justification::centredLeft);
    for (int i = 0; i < 16; ++i)
    {
        const float lv = voiceLv[i];
        auto cell = juce::Rectangle<float> (772.0f + (i % 8) * 15.0f, 28.0f + (i / 8) * 13.0f, 11.0f, 8.0f);
        g.setColour (lv > 0.02f ? C::cyan.withAlpha (0.35f + 0.65f * lv) : C::track); g.fillRoundedRectangle (cell, 1.5f);
    }
    g.setColour (C::dim);
    g.drawText ("OUTPUT", 912, 10, 132, 12, juce::Justification::centredLeft);
    ledMeter (g, { 912.0f, 28.0f, 132.0f, 7.0f }, meterL, holdL, false, 22);
    ledMeter (g, { 912.0f, 40.0f, 132.0f, 7.0f }, meterR, holdR, false, 22);

    // page tabs: one accent, underline on the active page
    for (int i = 0; i < kPages; ++i)
        if (i == currentPage) { g.setColour (C::cyan); g.fillRect (16.0f + i * 106.0f + 10.0f, 98.0f, 80.0f, 2.0f); }
}

//==============================================================================
// ModHost
std::vector<ModRing> LumenSynthEditor::ringsFor (int dst)
{
    std::vector<ModRing> r;
    for (auto& m : proc.modsForDest (dst))
        if (std::abs (m.amt) > 1.0e-4f)
            r.push_back ({ m.slot, m.amt, proc.modSourceValue (m.src), sourceColour (m.src) });
    return r;
}

void LumenSynthEditor::dropMod (int src, int dst)
{
    const int slot = proc.assignMod (src, dst, isBipolarSrc (src) ? 0.3f : 0.5f);
    if (slot < 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Lumen Synth", "All 16 modulation slots are in use. Clear one in the MATRIX page.");
    ringCounter = 0;
}

void LumenSynthEditor::knobMenu (int dst, juce::Component* target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("MODULATION  -  " + modDests()[dst].toUpperCase());
    const auto mods = proc.modsForDest (dst);
    for (auto& m : mods)
    {
        juce::PopupMenu sub;
        const float amts[] = { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f };
        for (int i = 0; i < 8; ++i) sub.addItem (1000 + m.slot * 20 + i, (amts[i] > 0 ? "+" : "") + juce::String ((int) (amts[i] * 100)) + "%", true, std::abs (m.amt - amts[i]) < 0.005f);
        sub.addSeparator();
        sub.addItem (1000 + m.slot * 20 + 19, "Remove");
        menu.addSubMenu (modSources()[m.src] + "  (" + juce::String ((int) std::round (m.amt * 100)) + "%)", sub);
    }
    if (! mods.empty()) menu.addSeparator();
    juce::PopupMenu addMenu;
    for (int s = 1; s < kNumSrc; ++s) addMenu.addItem (100 + s, modSources()[s]);
    menu.addSubMenu ("Add modulation", addMenu);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, dst] (int r)
    {
        if (r >= 1000)
        {
            const int slot = (r - 1000) / 20, k = (r - 1000) % 20;
            const float amts[] = { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f };
            if (k == 19) proc.clearModSlot (slot); else if (k < 8) proc.setModAmount (slot, amts[k]);
        }
        else if (r > 100) dropMod (r - 100, dst);
        ringCounter = 0;
    });
}

//==============================================================================
void LumenSynthEditor::timerCallback()
{
    const float l = proc.peakL.load(), r = proc.peakR.load();
    meterL = juce::jmax (l, meterL * 0.8f); meterR = juce::jmax (r, meterR * 0.8f);
    if (++holdTimer > 40) { holdTimer = 0; holdL = holdR = 0.0f; }
    holdL = juce::jmax (holdL, l); holdR = juce::jmax (holdR, r);
    for (int i = 0; i < 16; ++i) voiceLv[i] = juce::jmax (proc.voiceLevel[i].load(), voiceLv[i] * 0.7f);
    root.repaint (766, 6, 282, 54);
    if (proc.presetVersion.load() != lastPresetVersion) { lastPresetVersion = proc.presetVersion.load(); root.repaint (290, 8, 400, 50); ringCounter = 0; }
    pages[currentPage]->tick();
    smart->tick(); scope->tick(); macros->tick();
    for (auto* b : badges) b->setLevel (isBipolarSrc (b->source()) ? 0.5f + 0.5f * proc.modSourceValue (b->source()) : proc.modSourceValue (b->source()));

    // mod rings: refresh the assignment list a few times per second, animate the live dots every frame
    const bool refresh = (ringCounter++ % 6) == 0;
    for (auto* k : knobs)
    {
        if (! k->isShowing()) continue;
        const bool had = k->hasRings();
        if (refresh) k->refreshRings();
        if (had || k->hasRings()) { if (! refresh) k->refreshRings(); k->repaint(); }
    }
}
