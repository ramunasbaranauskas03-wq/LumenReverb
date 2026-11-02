#include "Widgets.h"
#include "../OscMath.h"
#include "../Filters.h"

namespace lxl::ui
{
static constexpr float kRotStart = juce::MathConstants<float>::pi * 1.25f;
static constexpr float kRotEnd   = juce::MathConstants<float>::pi * 2.75f;

void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps, int gap)
{
    const int n = (int) comps.size();
    if (n == 0) return;
    const int w = (area.getWidth() - gap * (n - 1)) / n;
    for (auto* c : comps)
    {
        if (c != nullptr) c->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (gap);
    }
}

// ================================================================== Panel
Panel::Panel (juce::String t, APVTS* state, juce::String powerParam) : title (std::move (t))
{
    if (state != nullptr && powerParam.isNotEmpty())
    {
        power = std::make_unique<juce::ToggleButton>();
        power->setTooltip ("Enable / bypass " + title);
        addAndMakeVisible (*power);
        powerAtt = std::make_unique<APVTS::ButtonAttachment> (*state, powerParam, *power);
        power->onStateChange = [this] { repaint(); };
        power->onClick = [this] { repaint(); };
    }
}

void Panel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    auto head = getLocalBounds().removeFromTop (24).reduced (10, 0);
    if (power != nullptr) head.removeFromLeft (18);
    g.setFont (labelFont (10.5f));
    g.setColour (isOn() ? col::text : col::textFaint);
    g.drawText (title.toUpperCase(), head, juce::Justification::centredLeft);
    g.setColour (col::border.withAlpha (0.6f));
    g.fillRect (8, 24, getWidth() - 16, 1);
}

void Panel::paintOverChildren (juce::Graphics& g)
{
    if (isOn()) return;
    g.setColour (col::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (25.0f), 6.0f);
}

void Panel::resized()
{
    if (power != nullptr) power->setBounds (8, 5, 16, 16);
    if (onLayout) onLayout();
}

// ================================================================== Knob
Knob::Knob (LumenSynthXLProcessor& p, const juce::String& id, const juce::String& lbl, bool bipolar)
    : proc (p), paramId (id), label (lbl)
{
    param = proc.apvts.getParameter (id);
    jassert (param != nullptr);
    destIndex = destIndexForParam (id);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (kRotStart, kRotEnd, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);
    att = std::make_unique<APVTS::SliderAttachment> (proc.apvts, id, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };

    slider.onDownHook = [this] (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu()) { showModMenu(); return true; }
        if (e.mods.isShiftDown() && destIndex > 0)
        {
            auto rows = proc.rowsForDest (destIndex);
            if (! rows.isEmpty())
            {
                modDragRow = rows.getLast();
                modDragStart = proc.apvts.getRawParameterValue (mtxId (modDragRow, "amt"))->load();
                if (auto* a = proc.apvts.getParameter (mtxId (modDragRow, "amt"))) a->beginChangeGesture();
                return true;
            }
        }
        modDragRow = -1;
        return false;
    };
    slider.onDragHook = [this] (const juce::MouseEvent& e)
    {
        if (modDragRow < 0) return;
        if (auto* a = proc.apvts.getParameter (mtxId (modDragRow, "amt")))
        {
            const float v = juce::jlimit (-1.0f, 1.0f, modDragStart - (float) e.getDistanceFromDragStartY() / 160.0f);
            a->setValueNotifyingHost (a->convertTo0to1 (v));
        }
        repaint();
    };

    juce::String tip = param->getName (64);
    if (destIndex > 0) tip << "\nDrag a mod source here to modulate. Shift+drag: mod depth. Right-click: modulation menu.";
    setTooltip (tip);
}

Knob::~Knob() = default;

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (14);
    const int s = juce::jmin (maxKnobSize, juce::jmin (r.getWidth(), r.getHeight()));
    slider.setBounds (r.withSizeKeepingCentre (s, s).withY (r.getBottom() - s));
}

void Knob::paint (juce::Graphics& g)
{
    auto txt = getLocalBounds().removeFromBottom (14);
    const bool showValue = slider.isMouseOverOrDragging() || isMouseOver (true);
    g.setFont (showValue ? font (10.5f, true) : juce::Font (juce::FontOptions (9.0f, juce::Font::bold)).withExtraKerningFactor (0.03f));
    g.setColour (showValue ? col::accent : (isEnabled() ? col::textDim : col::textFaint));
    g.drawFittedText (showValue ? param->getCurrentValueAsText() : label.toUpperCase(), txt, juce::Justification::centred, 1);
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    const auto b = slider.getBounds().toFloat();
    const auto c = b.getCentre();
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 4.0f;

    if (dragHover)
    {
        g.setColour (col::mod.withAlpha (0.25f));
        g.fillEllipse (c.x - r - 3, c.y - r - 3, (r + 3) * 2, (r + 3) * 2);
    }
    if (destIndex <= 0) return;

    const float base = param->getValue();
    auto rows = proc.rowsForDest (destIndex);
    float rr = r + 3.5f;
    for (auto row : rows)
    {
        const float amt = proc.apvts.getRawParameterValue (mtxId (row, "amt"))->load();
        const float to = clamp01 (base + amt);
        if (std::abs (to - base) < 0.002f) continue;
        juce::Path p;
        const float a0 = kRotStart + base * (kRotEnd - kRotStart), a1 = kRotStart + to * (kRotEnd - kRotStart);
        p.addCentredArc (c.x, c.y, rr, rr, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (col::mod.withAlpha (0.9f));
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        rr += 3.0f;
    }
    if (! rows.isEmpty() && (proc.engine.activeVoiceCount.load() > 0 || destIndex >= D::GlobalStart))
    {
        const float v = clamp01 (base + proc.engine.modDisplay[(size_t) destIndex].load());
        const float a = kRotStart + v * (kRotEnd - kRotStart);
        const float dr = r + 3.5f;
        g.setColour (col::mod);
        g.fillEllipse (c.x + std::sin (a) * dr - 3.0f, c.y - std::cos (a) * dr - 3.0f, 6.0f, 6.0f);
    }
}

void Knob::tick()
{
    if (destIndex <= 0) return;
    const int n = proc.rowsForDest (destIndex).size();
    const float v = proc.engine.modDisplay[(size_t) destIndex].load();
    if (n != numRows || (n > 0 && std::abs (v - lastModDisplay) > 0.001f))
    {
        numRows = n;
        lastModDisplay = v;
        repaint();
    }
}

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    if (destIndex <= 0 || ! d.description.toString().startsWith ("src:")) return false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("src:", false, false).getIntValue();
    // FX and envelope-time targets are global: only macros, wheel, bend and pressure can move them
    return destIndex < D::GlobalStart || isGlobalSource (src) || src == SrcAftertouch;
}

void Knob::itemDropped (const SourceDetails& d)
{
    dragHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("src:", false, false).getIntValue();
    const bool bipolarDest = param->getNormalisableRange().start < 0.0f;
    if (proc.assignModulation (src, destIndex, bipolarDest ? 0.25f : 0.35f) < 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Mod matrix full",
                                                "All 16 matrix slots are in use. Remove a modulation first.");
    repaint();
}

void Knob::showModMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (param->getName (64));
    m.addItem (1, "Reset to default");
    auto rows = proc.rowsForDest (destIndex);
    const auto& srcNames = sourceNames();
    if (! rows.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("Modulation");
        for (int i = 0; i < rows.size(); ++i)
        {
            const int row = rows[i];
            const int src = (int) std::lround (proc.apvts.getRawParameterValue (mtxId (row, "src"))->load());
            const float amt = proc.apvts.getRawParameterValue (mtxId (row, "amt"))->load();
            juce::PopupMenu sub;
            sub.addItem (100 + i * 10 + 0, "Invert");
            sub.addItem (100 + i * 10 + 1, "Depth 25%");
            sub.addItem (100 + i * 10 + 2, "Depth 50%");
            sub.addItem (100 + i * 10 + 3, "Depth 100%");
            sub.addSeparator();
            sub.addItem (100 + i * 10 + 4, "Remove");
            m.addSubMenu (srcNames[src] + "  (" + juce::String (juce::roundToInt (amt * 100.0f)) + "%)", sub);
        }
        m.addItem (2, "Remove all modulation");
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, rows] (int r)
    {
        if (r == 1) { param->beginChangeGesture(); param->setValueNotifyingHost (param->getDefaultValue()); param->endChangeGesture(); }
        else if (r == 2) proc.removeModulation (destIndex);
        else if (r >= 100)
        {
            const int i = (r - 100) / 10, op = (r - 100) % 10;
            if (i >= rows.size()) return;
            const int row = rows[i];
            auto* a = proc.apvts.getParameter (mtxId (row, "amt"));
            const float cur = proc.apvts.getRawParameterValue (mtxId (row, "amt"))->load();
            const float sign = cur < 0.0f ? -1.0f : 1.0f;
            float v = cur;
            if (op == 0) v = -cur; else if (op == 1) v = 0.25f * sign; else if (op == 2) v = 0.5f * sign; else if (op == 3) v = sign;
            if (op == 4)
            {
                const int src = (int) std::lround (proc.apvts.getRawParameterValue (mtxId (row, "src"))->load());
                proc.removeModulation (destIndex, src);
            }
            else if (a != nullptr) { a->beginChangeGesture(); a->setValueNotifyingHost (a->convertTo0to1 (v)); a->endChangeGesture(); }
        }
        repaint();
    });
}

// ================================================================== Choice / Toggle
Choice::Choice (APVTS& s, const juce::String& id, const juce::String& lbl) : label (lbl)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (c->choices, 1);
    box.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (box);
    att = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    if (label.isNotEmpty()) r.removeFromTop (13);
    box.setBounds (r);
}

void Choice::paint (juce::Graphics& g)
{
    if (label.isEmpty()) return;
    g.setFont (labelFont (9.5f));
    g.setColour (col::textDim);
    g.drawText (label.toUpperCase(), getLocalBounds().removeFromTop (12), juce::Justification::centredLeft);
}

Toggle::Toggle (APVTS& s, const juce::String& id, const juce::String& text)
{
    setButtonText (text);
    att = std::make_unique<APVTS::ButtonAttachment> (s, id, *this);
}

// ================================================================== ModChip
ModChip::ModChip (LumenSynthXLProcessor& p, int s, juce::String t) : text (std::move (t)), proc (p), src (s)
{
    setTooltip ("Drag onto any knob to modulate it");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ModChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (selected ? col::modDim.withAlpha (0.55f) : col::well);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (selected ? col::mod : col::border);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    // live value bar
    auto bar = r.reduced (4.0f).removeFromBottom (3.0f);
    g.setColour (col::bg2);
    g.fillRoundedRectangle (bar, 1.5f);
    g.setColour (col::mod);
    const float v = juce::jlimit (-1.0f, 1.0f, value);
    if (v >= 0.0f) g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v), 1.5f);
    else g.fillRoundedRectangle (bar.withX (bar.getCentreX() + bar.getWidth() * 0.5f * v).withWidth (-bar.getWidth() * 0.5f * v), 1.5f);
    g.setColour (col::text);
    g.setFont (labelFont (9.5f));
    g.drawFittedText (text, getLocalBounds().reduced (3).withTrimmedBottom (5), juce::Justification::centred, 1);
}

void ModChip::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 4)
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            auto img = createComponentSnapshot (getLocalBounds()).convertedToFormat (juce::Image::ARGB);
            img.multiplyAllAlphas (0.85f);
            c->startDragging ("src:" + juce::String (src), this, juce::ScaledImage (img));
        }
}

void ModChip::mouseUp (const juce::MouseEvent&)
{
    if (! dragging && onClick) onClick();
    dragging = false;
}

void ModChip::tick()
{
    float v = 0.0f;
    if (src >= SrcMod1 && src < SrcMod1 + kNumModSlots) v = proc.engine.slotDisplay[(size_t) (src - SrcMod1)].load();
    else if (src >= SrcMacro1 && src < SrcMacro1 + kNumMacros) v = proc.apvts.getRawParameterValue (macroId (src - SrcMacro1))->load();
    else if (src == SrcModWheel) v = proc.engine.modWheel;
    else if (src == SrcPitchBend) v = proc.engine.pitchBend;
    else if (src == SrcAftertouch) v = proc.engine.chanPressure;
    if (std::abs (v - value) > 0.003f) { value = v; repaint(); }
}

// ================================================================== WavetableView
WavetableView::WavetableView (LumenSynthXLProcessor& p, int o) : proc (p), osc (o) {}

void WavetableView::tick()
{
    const int d = D::osc (osc, D::Pos);
    const float pos = clamp01 (proc.apvts.getParameter (oscId (osc, "pos"))->getValue() + proc.engine.modDisplay[(size_t) d].load());
    const float warp = clamp01 (proc.apvts.getParameter (oscId (osc, "warp"))->getValue() + proc.engine.modDisplay[(size_t) D::osc (osc, D::Warp)].load());
    const int mode = (int) std::lround (proc.apvts.getRawParameterValue (oscId (osc, "mode"))->load());
    const auto* t = proc.currentTable (osc);
    if (std::abs (pos - lastPos) > 0.002f || t != lastTable || mode != lastMode || std::abs (warp - lastWarp) > 0.002f)
    {
        lastPos = pos; lastTable = t; lastMode = mode; lastWarp = warp;
        repaint();
    }
}

void WavetableView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 5.0f);
    const auto* t = proc.currentTable (osc);
    const bool on = proc.apvts.getRawParameterValue (oscId (osc, "on"))->load() > 0.5f;
    if (t == nullptr) return;

    auto area = r.reduced (10.0f, 14.0f);
    const int stackN = juce::jmin (t->numFrames, 24);
    const float dx = area.getWidth() * 0.18f, dy = area.getHeight() * 0.32f;
    const float w = area.getWidth() - dx, h = area.getHeight() - dy;
    const int pts = 96;

    auto framePath = [&] (int f, float ox, float oy, float hh, bool warpIt)
    {
        juce::Path p;
        const float* data = t->frame (f, 0);
        for (int i = 0; i <= pts; ++i)
        {
            float ph = (float) i / (float) pts;
            float gain = 1.0f;
            if (warpIt) ph = warpPhase (lastMode, juce::jmin (ph, 0.9999f), lastWarp, gain);
            float v = data[(int) (ph * (float) (Wavetable::kSize - 1))] * gain;
            if (warpIt && lastMode == ModeFold) v = foldSample (v * (1.0f + lastWarp * 5.0f));
            const float x = ox + (float) i / (float) pts * w;
            const float y = oy - v * hh * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    // 3D stack, back to front
    for (int k = stackN - 1; k >= 0; --k)
    {
        const float t01 = stackN > 1 ? (float) k / (float) (stackN - 1) : 0.0f;
        const int f = juce::roundToInt (t01 * (float) (t->numFrames - 1));
        const float ox = area.getX() + dx * t01, oy = area.getY() + h * 0.5f + dy * (1.0f - t01);
        g.setColour (col::accent.withAlpha (on ? 0.08f + 0.06f * (1.0f - t01) : 0.04f));
        g.strokePath (framePath (f, ox, oy, h * 0.8f, false), juce::PathStrokeType (1.0f));
    }

    // current frame (warped), positioned in the stack
    const float pos = juce::jmax (0.0f, lastPos);
    const int f = juce::roundToInt (pos * (float) (t->numFrames - 1));
    const float ox = area.getX() + dx * pos, oy = area.getY() + h * 0.5f + dy * (1.0f - pos);
    auto cur = framePath (f, ox, oy, h * 0.8f, true);
    g.setColour ((on ? col::accent : col::textFaint).withAlpha (0.25f));
    g.strokePath (cur, juce::PathStrokeType (4.0f));
    g.setColour (on ? col::accent : col::textFaint);
    g.strokePath (cur, juce::PathStrokeType (1.6f));

    g.setFont (font (11.0f, true));
    g.setColour (col::textDim);
    g.drawText (t->name, getLocalBounds().reduced (8, 4).removeFromTop (14), juce::Justification::centredLeft);
    g.drawText ("Frame " + juce::String (f + 1) + "/" + juce::String (t->numFrames), getLocalBounds().reduced (8, 4).removeFromBottom (14),
                juce::Justification::centredRight);
}

// ================================================================== FilterView
FilterView::FilterView (LumenSynthXLProcessor& p, int f) : proc (p), filter (f) {}

void FilterView::tick()
{
    const float c = clamp01 (proc.apvts.getParameter (filId (filter, "cutoff"))->getValue() + proc.engine.modDisplay[(size_t) D::fil (filter, D::Cutoff)].load());
    const float r = clamp01 (proc.apvts.getParameter (filId (filter, "reso"))->getValue() + proc.engine.modDisplay[(size_t) D::fil (filter, D::Reso)].load());
    const int t = (int) std::lround (proc.apvts.getRawParameterValue (filId (filter, "type"))->load());
    const bool on = proc.apvts.getRawParameterValue (filId (filter, "on"))->load() > 0.5f;
    if (std::abs (c - lastC) > 0.001f || std::abs (r - lastR) > 0.001f || t != lastT || on != lastOn)
    {
        lastC = c; lastR = r; lastT = t; lastOn = on;
        repaint();
    }
}

void FilterView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 5.0f);
    auto a = r.reduced (4.0f);
    g.setColour (col::border.withAlpha (0.5f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = a.getX() + std::log10 (f / 20.0f) / 3.0f * a.getWidth();
        g.fillRect (x, a.getY(), 1.0f, a.getHeight());
    }
    const float zeroY = a.getY() + a.getHeight() * (24.0f / 60.0f);
    g.fillRect (a.getX(), zeroY, a.getWidth(), 1.0f);

    const auto range = proc.apvts.getParameter (filId (filter, "cutoff"))->getNormalisableRange();
    const float cutoff = range.convertFrom0to1 (juce::jmax (0.0f, lastC));
    juce::Path p, fill;
    const int N = 80;
    for (int i = 0; i <= N; ++i)
    {
        const float x01 = (float) i / (float) N;
        const float f = 20.0f * std::pow (1000.0f, x01);
        const float db = juce::jlimit (-36.0f, 24.0f, filterMagnitudeDb (lastT, cutoff, juce::jmax (0.0f, lastR), f));
        const float x = a.getX() + x01 * a.getWidth();
        const float y = a.getY() + (24.0f - db) / 60.0f * a.getHeight();
        if (i == 0) { p.startNewSubPath (x, y); fill.startNewSubPath (x, a.getBottom()); }
        p.lineTo (x, y); fill.lineTo (x, y);
    }
    fill.lineTo (a.getRight(), a.getBottom());
    fill.closeSubPath();
    const auto c = lastOn ? col::accent : col::textFaint;
    g.setColour (c.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.6f));
}

// ================================================================== EnvView
EnvView::EnvView (LumenSynthXLProcessor& p, juce::String pre, int s) : proc (p), prefix (std::move (pre)), slot (s) {}

void EnvView::tick()
{
    const char* names[] = { "a", "d", "s", "r", "curve" };
    bool changed = false;
    for (int i = 0; i < 5; ++i)
    {
        const float v = proc.apvts.getRawParameterValue (prefix + names[i])->load();
        if (v != last[i]) { last[i] = v; changed = true; }
    }
    const float lv = slot >= 0 ? proc.engine.slotDisplay[(size_t) slot].load() : 0.0f;
    if (std::abs (lv - last[5]) > 0.002f) { last[5] = lv; changed = true; }
    if (changed) repaint();
}

void EnvView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 5.0f);
    auto a = r.reduced (6.0f, 8.0f);
    const float A = last[0], Dd = last[1], S = last[2], R = last[3], curve = last[4];
    auto tw = [] (float t) { return std::sqrt (juce::jmax (0.0f, t)) + 0.04f; };
    const float wa = tw (A), wd = tw (Dd), ws = 0.35f, wr = tw (R);
    const float total = wa + wd + ws + wr;
    const float sx = a.getWidth() / total;
    const float k = (curve - 0.5f) * 10.0f;
    auto shape = [k] (float t) { return std::abs (k) < 0.01f ? t : (1.0f - std::exp (-k * t)) / (1.0f - std::exp (-k)); };
    juce::Path p;
    float x = a.getX();
    const float y0 = a.getBottom(), yTop = a.getY();
    auto Y = [&] (float lvl) { return y0 - lvl * (y0 - yTop); };
    p.startNewSubPath (x, y0);
    const int N = 24;
    for (int i = 1; i <= N; ++i) { const float t = (float) i / N; p.lineTo (x + t * wa * sx, Y (1.0f - std::pow (1.0f - t, 2.0f))); }
    x += wa * sx;
    for (int i = 1; i <= N; ++i) { const float t = (float) i / N; p.lineTo (x + t * wd * sx, Y (1.0f - (1.0f - S) * shape (t))); }
    x += wd * sx;
    p.lineTo (x + ws * sx, Y (S));
    x += ws * sx;
    for (int i = 1; i <= N; ++i) { const float t = (float) i / N; p.lineTo (x + t * wr * sx, Y (S * (1.0f - shape (t)))); }
    juce::Path fill (p);
    fill.lineTo (a.getRight(), y0);
    fill.closeSubPath();
    const auto c = slot >= 0 ? col::mod : col::accent;
    g.setColour (c.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.6f));
    if (slot >= 0 && last[5] > 0.001f)
    {
        g.setColour (c.withAlpha (0.7f));
        g.fillRect (a.getX(), Y (last[5]) - 0.5f, a.getWidth(), 1.0f);
    }
}

// ================================================================== LfoView
LfoView::LfoView (LumenSynthXLProcessor& p, int s) : proc (p), slot (s) {}

void LfoView::tick()
{
    const float v = proc.engine.slotDisplay[(size_t) slot].load();
    if (std::abs (v - lastVal) > 0.003f) { lastVal = v; repaint(); }
}

void LfoView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 5.0f);
    auto a = r.reduced (8.0f, 10.0f);
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (modId (slot, "type"))->load());
    const int shape = (int) std::lround (proc.apvts.getRawParameterValue (modId (slot, "shape"))->load());
    const bool bip = proc.apvts.getRawParameterValue (modId (slot, "bipolar"))->load() > 0.5f;
    const float smooth = proc.apvts.getRawParameterValue (modId (slot, "smooth"))->load();
    g.setColour (col::border.withAlpha (0.5f));
    g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 1.0f);
    juce::Path p;
    const int N = 200;
    for (int i = 0; i <= N; ++i)
    {
        const float t = (float) i / N * 2.0f; // two cycles
        float v;
        if (type == ModRandom)
        {
            const int idx = (int) std::floor (t * 4.0f);
            const float ft = t * 4.0f - (float) idx;
            auto h = [] (int k) { return frac (std::sin ((float) k * 12.9898f) * 43758.5453f); };
            float tt = smooth < 0.001f ? 1.0f : juce::jmin (1.0f, ft / smooth);
            tt = tt * tt * (3.0f - 2.0f * tt);
            v = (h (idx - 1) + (h (idx) - h (idx - 1)) * tt) * 2.0f - 1.0f;
        }
        else
        {
            const float ph = frac (t);
            switch (shape)
            {
                case LfoSine: v = std::sin (kTwoPi * ph); break;
                case LfoTri: v = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
                case LfoSawUp: v = 2.0f * ph - 1.0f; break;
                case LfoSawDown: v = 1.0f - 2.0f * ph; break;
                case LfoSquare: v = ph < 0.5f ? 1.0f : -1.0f; break;
                case LfoPump: v = 2.0f * (1.0f - std::exp (-7.0f * ph)) / (1.0f - std::exp (-7.0f)) - 1.0f; break;
                case LfoStairs: v = std::floor (ph * 4.0f) / 3.0f * 2.0f - 1.0f; break;
                default: v = std::sin (kTwoPi * ph * 1.0f) * 0.6f + std::sin (kTwoPi * ph * 3.0f + 1.0f) * 0.4f; break;
            }
        }
        const float x = a.getX() + (float) i / N * a.getWidth();
        const float y = a.getCentreY() - v * a.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col::mod);
    g.strokePath (p, juce::PathStrokeType (1.6f));
    // live value marker
    const float lv = bip ? lastVal : lastVal * 2.0f - 1.0f;
    g.setColour (col::mod.withAlpha (0.8f));
    const float y = a.getCentreY() - juce::jlimit (-1.0f, 1.0f, lv) * a.getHeight() * 0.5f;
    g.fillEllipse (a.getRight() - 4.0f, y - 4.0f, 8.0f, 8.0f);
    g.setFont (labelFont (9.0f));
    g.setColour (col::textFaint);
    g.drawText (bip ? "BIPOLAR" : "UNIPOLAR", getLocalBounds().reduced (8, 4).removeFromTop (12), juce::Justification::centredRight);
}

// ================================================================== PerformerView
PerformerView::PerformerView (LumenSynthXLProcessor& p, int s) : proc (p), slot (s)
{
    setRepaintsOnMouseActivity (true);
}

void PerformerView::tick()
{
    const int s = proc.engine.stepDisplay[(size_t) slot].load();
    if (s != lastStep) { lastStep = s; repaint(); }
}

void PerformerView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 5.0f);
    auto a = r.reduced (8.0f, 10.0f);
    const int len = (int) std::lround (proc.apvts.getRawParameterValue (modId (slot, "len"))->load());
    const bool bip = proc.apvts.getRawParameterValue (modId (slot, "bipolar"))->load() > 0.5f;
    const float bw = a.getWidth() / (float) kNumSteps;
    if (bip)
    {
        g.setColour (col::border.withAlpha (0.6f));
        g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 1.0f);
    }
    for (int k = 0; k < kNumSteps; ++k)
    {
        const float v = proc.apvts.getRawParameterValue (stepId (slot, k))->load();
        auto cell = juce::Rectangle<float> (a.getX() + bw * (float) k, a.getY(), bw, a.getHeight()).reduced (2.0f, 0.0f);
        const bool active = k < len;
        g.setColour (col::bg2);
        g.fillRoundedRectangle (cell, 2.0f);
        juce::Rectangle<float> bar;
        if (bip)
        {
            const float y = a.getCentreY() - (v * 2.0f - 1.0f) * a.getHeight() * 0.5f;
            bar = juce::Rectangle<float> (cell.getX(), juce::jmin (y, a.getCentreY()), cell.getWidth(), std::abs (y - a.getCentreY()));
        }
        else bar = cell.withTop (cell.getBottom() - v * cell.getHeight());
        const bool cur = k == lastStep && proc.engine.activeVoiceCount.load() > 0;
        g.setColour (! active ? col::textFaint.withAlpha (0.4f) : (cur ? col::mod.brighter (0.4f) : col::mod.withAlpha (0.8f)));
        g.fillRoundedRectangle (bar, 2.0f);
        if (k == lastStep && active)
        {
            g.setColour (col::mod.withAlpha (0.35f));
            g.drawRoundedRectangle (cell, 2.0f, 1.0f);
        }
    }
}

void PerformerView::setFromMouse (juce::Point<float> pt)
{
    auto a = getLocalBounds().toFloat().reduced (8.0f, 10.0f);
    const int k = juce::jlimit (0, kNumSteps - 1, (int) ((pt.x - a.getX()) / (a.getWidth() / (float) kNumSteps)));
    const float v = clamp01 ((a.getBottom() - pt.y) / a.getHeight());
    if (auto* p = proc.apvts.getParameter (stepId (slot, k)))
    {
        if (k != lastEdited)
        {
            if (lastEdited >= 0) if (auto* q = proc.apvts.getParameter (stepId (slot, lastEdited))) q->endChangeGesture();
            p->beginChangeGesture();
            lastEdited = k;
        }
        p->setValueNotifyingHost (v);
    }
    repaint();
}

void PerformerView::mouseDown (const juce::MouseEvent& e) { lastEdited = -1; setFromMouse (e.position); }
void PerformerView::mouseDrag (const juce::MouseEvent& e) { setFromMouse (e.position); }
void PerformerView::mouseUp (const juce::MouseEvent&)
{
    if (lastEdited >= 0) if (auto* q = proc.apvts.getParameter (stepId (slot, lastEdited))) q->endChangeGesture();
    lastEdited = -1;
}

// ================================================================== Meter
void Meter::tick()
{
    const float nl = proc.engine.peakL.load(), nr = proc.engine.peakR.load();
    const float dl = juce::jmax (nl, l * 0.86f), dr = juce::jmax (nr, r * 0.86f);
    if (std::abs (dl - l) > 0.001f || std::abs (dr - r) > 0.001f) { l = dl; r = dr; repaint(); }
}

void Meter::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    auto toY = [&] (float v)
    {
        const float db = juce::jmax (-48.0f, juce::Decibels::gainToDecibels (v, -100.0f));
        return (db + 48.0f) / 48.0f;
    };
    const float w = (a.getWidth() - 2.0f) * 0.5f;
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (a.getX() + (float) c * (w + 2.0f), a.getY(), w, a.getHeight());
        g.setColour (col::well);
        g.fillRoundedRectangle (bar, 2.0f);
        const float v = toY (c == 0 ? l : r);
        auto lit = bar.withTop (bar.getBottom() - v * bar.getHeight());
        g.setColour ((c == 0 ? l : r) > 0.89f ? col::warn : col::good.withAlpha (0.85f));
        g.fillRoundedRectangle (lit, 2.0f);
    }
}
} // namespace lxl::ui
