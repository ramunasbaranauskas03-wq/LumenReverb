#include "PluginEditor.h"

using namespace lumen::ui;

static juce::Font uiFont (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

// ============================================================================
//  Look and feel
// ============================================================================

LnF::LnF()
{
    setColour (juce::ComboBox::backgroundColourId, col::panelHi);
    setColour (juce::ComboBox::outlineColourId, col::stroke);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::PopupMenu::backgroundColourId, col::panelHi);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::amber.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::amber);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::panelHi);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::amber.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId, col::amber);
    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::stroke);
}

void LnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float ang = start + pos * (end - start);
    const bool enabled = s.isEnabled();
    const bool hover = s.isMouseOverOrDragging();

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, start, end, true);
    g.setColour (col::stroke);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (bipolar params start from centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (end - start) : start;
    juce::Path val;
    val.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, juce::jmin (from, ang), juce::jmax (from, ang), true);
    juce::ColourGradient grad (col::amber, c.x - r, c.y + r, col::coral, c.x + r, c.y - r, false);
    g.setGradientFill (grad);
    if (! enabled) g.setColour (col::dim.withAlpha (0.4f));
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body
    const float br = r - 8.0f;
    juce::ColourGradient body (juce::Colour (0xff2a2e37), c.x, c.y - br, juce::Colour (0xff15171c), c.x, c.y + br, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (hover ? col::amber.withAlpha (0.35f) : juce::Colour (0xff353a45));
    g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.0f);

    // pointer
    const float pr = br - 5.0f;
    const juce::Point<float> tip (c.x + pr * std::sin (ang), c.y - pr * std::cos (ang));
    const juce::Point<float> base (c.x + (pr * 0.45f) * std::sin (ang), c.y - (pr * 0.45f) * std::cos (ang));
    g.setColour (enabled ? col::text : col::dim);
    g.drawLine ({ base, tip }, 2.0f);
}

void LnF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto accent = b.getProperties().contains ("rec") ? col::rec : col::amber;
    g.setColour (on ? accent.withAlpha (0.16f) : (down ? col::panelHi.brighter (0.1f) : col::panelHi));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? accent.withAlpha (0.8f) : (over ? col::dim : col::stroke));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void LnF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    const auto accent = b.getProperties().contains ("rec") ? col::rec : col::amber;
    g.setColour (b.getToggleState() ? accent : (over ? col::text : col::text.withAlpha (b.isEnabled() ? 0.85f : 0.35f)));
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

juce::Font LnF::getTextButtonFont (juce::TextButton&, int h) { return uiFont (juce::jmin (12.5f, h * 0.48f), true).withExtraKerningFactor (0.06f); }
juce::Font LnF::getComboBoxFont (juce::ComboBox&) { return uiFont (13.0f); }
juce::Font LnF::getLabelFont (juce::Label&) { return uiFont (12.0f); }

void LnF::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (col::panelHi);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.isMouseOver (true) ? col::dim : col::stroke);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (col::dim);
    g.fillPath (arrow);
}

void LnF::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::panelHi);
    g.setColour (col::stroke);
    g.drawRect (0, 0, w, h);
}

juce::Label* LnF::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (uiFont (12.0f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, col::text.withAlpha (0.9f));
    return l;
}

void LnF::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const bool stacked = r.getHeight() > 40.0f;
    auto pill = stacked ? juce::Rectangle<float> (r.getCentreX() - 15, r.getCentreY() - 8, 30, 16)
                        : juce::Rectangle<float> (r.getX() + 2, r.getCentreY() - 8, 30, 16);
    g.setColour (on ? col::amber.withAlpha (0.25f) : col::panelHi);
    g.fillRoundedRectangle (pill, 8.0f);
    g.setColour (on ? col::amber : (over ? col::dim : col::stroke));
    g.drawRoundedRectangle (pill, 8.0f, 1.0f);
    const float kx = on ? pill.getRight() - 14 : pill.getX() + 2;
    g.setColour (on ? col::amber : col::dim);
    g.fillEllipse (kx, pill.getY() + 2, 12, 12);
    if (stacked)
    {
        g.setColour (col::dim.brighter (0.25f));
        g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.1f));
        g.drawText (b.getButtonText().toUpperCase(), r.removeFromTop (16), juce::Justification::centred);
        return;
    }
    g.setColour (on ? col::text : col::dim.brighter (0.3f));
    g.setFont (uiFont (12.0f, true).withExtraKerningFactor (0.05f));
    g.drawText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (40), juce::Justification::centredLeft);
}

void LnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 2, (float) w, 4);
    g.setColour (col::stroke); g.fillRoundedRectangle (r, 2);
    g.setColour (col::amber); g.fillRoundedRectangle (r.withRight (pos), 2);
    g.fillEllipse (pos - 6, r.getCentreY() - 6, 12, 12);
}

// ============================================================================
//  Widgets
// ============================================================================

Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name) : title (name.toUpperCase())
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (220);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    if (auto* p = s.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (16)); }

void Knob::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? col::dim.brighter (0.25f) : col::dim.withAlpha (0.4f));
    g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.1f));
    g.drawText (title, getLocalBounds().removeFromTop (16), juce::Justification::centred);
}

Choice::Choice (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name) : title (name.toUpperCase())
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    box.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (box);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
}

void Choice::resized()
{
    auto r = getLocalBounds().withTrimmedTop (16);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 26).withY (r.getY() + juce::jmin (6, juce::jmax (0, r.getHeight() - 26))));
}

void Choice::paint (juce::Graphics& g)
{
    g.setColour (col::dim.brighter (0.25f));
    g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.1f));
    g.drawText (title, getLocalBounds().removeFromTop (16), juce::Justification::centred);
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name)
{
    button.setButtonText (name);
    addAndMakeVisible (button);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, button);
}

void Section::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::stroke);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
    auto head = getLocalBounds().removeFromTop (28).reduced (14, 0);
    g.setColour (col::amber);
    g.fillRoundedRectangle ((float) head.getX(), (float) head.getCentreY() - 3.0f, 6.0f, 6.0f, 1.5f);
    g.setColour (col::text);
    g.setFont (uiFont (11.5f, true).withExtraKerningFactor (0.18f));
    g.drawText (title, head.withTrimmedLeft (14), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------

void WaveDisplay::refresh()
{
    proc.getDisplayData (data, data.version);
    spin += 0.15f;
    repaint();
}

bool WaveDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".mp3") || f.endsWithIgnoreCase (".ogg"))
            return true;
    return false;
}

void WaveDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (! files.isEmpty()) proc.loadAudioFile (juce::File (files[0]));
    repaint();
}

void WaveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101216));
    g.fillRoundedRectangle (bounds, 10.0f);

    auto area = bounds.reduced (14.0f, 30.0f);
    const float midY = area.getCentreY();
    const int cs = proc.getCaptureState();

    // faint centre line + grid
    g.setColour (col::stroke.withAlpha (0.6f));
    g.drawHorizontalLine ((int) midY, area.getX(), area.getRight());

    auto header = bounds.reduced (14.0f, 0).removeFromTop (28.0f);
    g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.12f));

    if (cs == LumenReverseProcessor::Armed || cs == LumenReverseProcessor::Recording)
    {
        const bool rec = cs == LumenReverseProcessor::Recording;
        g.setColour (rec ? col::rec : col::amber);
        g.fillEllipse (header.getX(), header.getCentreY() - 4, 8, 8);
        const int mode = (int) proc.apvts.getRawParameterValue ("capMode")->load();
        juce::String msg = rec ? "RECORDING INPUT" : (mode == 1 ? "ARMED  -  WAITING FOR SIGNAL" : "ARMED  -  WAITING FOR NEXT BAR (PRESS PLAY)");
        g.drawText (msg, header.withTrimmedLeft (16), juce::Justification::centredLeft);

        const int cols = LumenReverseProcessor::captureCols;
        const float cw = area.getWidth() / (float) cols;
        const int filled = (int) (proc.getCaptureProgress() * cols);
        for (int c = 0; c < cols; ++c)
        {
            const float v = juce::jmin (1.0f, proc.getCaptureDisplay (c));
            const float hh = juce::jmax (1.0f, v * area.getHeight() * 0.5f);
            g.setColour (c <= filled ? col::rec.withAlpha (0.85f) : col::stroke);
            g.fillRect (area.getX() + c * cw, midY - hh, juce::jmax (1.0f, cw - 0.6f), hh * 2.0f);
        }
        return;
    }

    if (data.version < 0 || data.peaks.empty())
    {
        g.setColour (dragHover ? col::amber : col::dim);
        g.setFont (uiFont (15.0f, true));
        g.drawText (dragHover ? "DROP TO LOAD" : "Capture a phrase  or  drop an audio file here", area, juce::Justification::centred);
        g.setFont (uiFont (12.0f));
        g.setColour (col::dim.withAlpha (0.7f));
        g.drawText ("Arm CAPTURE and play your track - the swell renders automatically", area.translated (0, 24), juce::Justification::centred);
        if (proc.isRendering())
        {
            g.setColour (col::amber);
            g.drawText ("RENDERING", header, juce::Justification::centredRight);
        }
        return;
    }

    const int total = data.swellLen + data.srcLen;
    const float boundaryX = area.getX() + area.getWidth() * (float) data.swellLen / (float) total;
    const int cols = juce::jmax (1, (int) data.peaks.size() / 2);
    const float cw = area.getWidth() / (float) cols;

    // beat grid inside swell region
    const double bpm = proc.getHostBpm();
    const double spb = data.sampleRate * 60.0 / bpm;
    g.setColour (col::stroke.withAlpha (0.5f));
    for (double s = data.swellLen; s > 0; s -= spb)
    {
        const float x = area.getX() + area.getWidth() * (float) (s / total);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    // swell region background glow
    juce::ColourGradient glow (col::amber.withAlpha (0.0f), area.getX(), 0, col::coral.withAlpha (0.10f), boundaryX, 0, false);
    g.setGradientFill (glow);
    g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), boundaryX - area.getX(), area.getHeight()));

    float peak = 0.0001f;
    for (auto v : data.peaks) peak = juce::jmax (peak, std::abs (v));
    const float scale = (area.getHeight() * 0.5f) / juce::jmax (0.25f, peak);

    for (int c = 0; c < cols; ++c)
    {
        const float x = area.getX() + c * cw;
        const float mn = data.peaks[(size_t) c * 2] * scale, mx = data.peaks[(size_t) c * 2 + 1] * scale;
        const bool inSwell = x < boundaryX;
        if (inSwell)
        {
            const float t = (x - area.getX()) / juce::jmax (1.0f, boundaryX - area.getX());
            g.setColour (col::amber.interpolatedWith (col::coral, t).withAlpha (0.55f + 0.45f * t));
        }
        else g.setColour (col::ice.withAlpha (0.65f));
        g.fillRect (x, midY - mx, juce::jmax (1.0f, cw), juce::jmax (1.0f, mx - mn));
    }

    // envelope overlay
    if (! data.envelope.empty())
    {
        juce::Path env;
        const int n = (int) data.envelope.size();
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + (boundaryX - area.getX()) * (float) (i + 1) / (float) n;
            const float y = area.getBottom() - data.envelope[(size_t) i] * area.getHeight();
            if (i == 0) env.startNewSubPath (area.getX(), area.getBottom());
            env.lineTo (x, y);
        }
        g.setColour (col::text.withAlpha (0.55f));
        g.strokePath (env, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
    }

    // hit boundary
    g.setColour (col::text.withAlpha (0.8f));
    g.drawLine (boundaryX, area.getY() - 6, boundaryX, area.getBottom() + 6, 1.2f);
    g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.12f));
    g.drawText ("HIT", juce::Rectangle<float> (boundaryX - 20, area.getBottom() + 6, 40, 14), juce::Justification::centred);

    // playhead
    const float ph = proc.getPlayheadFraction();
    if (ph >= 0.0f)
    {
        const float x = area.getX() + area.getWidth() * ph;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (x, area.getY() - 4, x, area.getBottom() + 4, 1.5f);
    }

    // header info
    g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.12f));
    g.setColour (col::amber);
    g.fillEllipse (header.getX(), header.getCentreY() - 3.5f, 7, 7);
    g.setColour (col::text.withAlpha (0.85f));
    const auto algoName = proc.apvts.getParameter ("algo")->getCurrentValueAsText().toUpperCase();
    const auto modeName = proc.apvts.getParameter ("mode")->getCurrentValueAsText().toUpperCase();
    const double sMs = data.swellLen * 1000.0 / data.sampleRate, srcMs = data.srcLen * 1000.0 / data.sampleRate;
    g.drawText (algoName + "   /   " + modeName + "   /   SWELL " + juce::String ((int) sMs) + " MS   +   SOURCE " + juce::String ((int) srcMs) + " MS",
                header.withTrimmedLeft (16), juce::Justification::centredLeft);
    if (proc.isRendering())
    {
        g.setColour (col::amber.withAlpha (0.6f + 0.4f * std::sin (spin)));
        g.drawText ("RENDERING", header, juce::Justification::centredRight);
    }
    if (dragHover)
    {
        g.setColour (col::amber.withAlpha (0.12f));
        g.fillRoundedRectangle (bounds, 10.0f);
        g.setColour (col::amber);
        g.drawRoundedRectangle (bounds.reduced (1), 10.0f, 1.5f);
    }
}

// ---------------------------------------------------------------------------

void DragOut::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool ok = proc.hasSource();
    const bool over = isMouseOver();
    juce::ColourGradient grad (col::amber, r.getX(), r.getY(), col::coral, r.getRight(), r.getBottom(), false);
    if (ok) g.setGradientFill (grad); else g.setColour (col::panelHi);
    g.setOpacity (ok ? (over ? 1.0f : 0.88f) : 1.0f);
    g.fillRoundedRectangle (r, 5.0f);
    g.setOpacity (1.0f);
    g.setColour (ok ? juce::Colour (0xff1a1206) : col::dim);
    // grip dots
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (r.getX() + 10 + j * 5.0f, r.getCentreY() - 6 + i * 5.0f, 2.5f, 2.5f);
    g.setFont (uiFont (12.0f, true).withExtraKerningFactor (0.08f));
    g.drawText ("DRAG TO DAW", r.withTrimmedLeft (22), juce::Justification::centred);
}

void DragOut::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! proc.hasSource() || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    auto f = proc.exportAudio (true);
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

// ============================================================================
//  Editor
// ============================================================================

LumenReverseEditor::LumenReverseEditor (LumenReverseProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      capMode (p.apvts, "capMode", "Capture"), capBars (p.apvts, "capBars", "Bar Capture"), threshold (p.apvts, "threshold", "Threshold"),
      syncToggle (p.apvts, "sync", "Tempo Sync"), freeMs (p.apvts, "lengthMs", "Free Length"),
      trigger (p.apvts, "trigger", "Trigger"), cycle (p.apvts, "cycle", "Sync Cycle"), pitchTrack (p.apvts, "pitchTrack", "Pitch Track"),
      algo (p.apvts, "algo", "Type"), mode (p.apvts, "mode", "Mode"), tremRate (p.apvts, "tremRate", "Rate"), tremShape (p.apvts, "tremShape", "Shape"),
      size (p.apvts, "size", "Size"), decay (p.apvts, "decay", "Decay"), predelay (p.apvts, "predelay", "Pre-Delay"), diffusion (p.apvts, "diffusion", "Diffusion"),
      damping (p.apvts, "damping", "Tone"), mod (p.apvts, "mod", "Mod"), shimmer (p.apvts, "shimmer", "Shimmer"),
      rise (p.apvts, "rise", "Rise"), tailFade (p.apvts, "tailFade", "Tail Fade"), sourceFade (p.apvts, "sourceFade", "Src Fade"),
      blend (p.apvts, "blend", "Src Blend"), sweep (p.apvts, "sweep", "Sweep"),
      tremDepth (p.apvts, "tremDepth", "Depth"), tremPhase (p.apvts, "tremPhase", "Stereo"), tremFollow (p.apvts, "tremFollow", "Follow"),
      lowCut (p.apvts, "lowCut", "Low Cut"), highCut (p.apvts, "highCut", "High Cut"), width (p.apvts, "width", "Width"),
      gain (p.apvts, "gain", "Output"), dry (p.apvts, "dry", "Dry Thru"), normalize (p.apvts, "normalize", "Normalize")
{
    setLookAndFeel (&lnf);

    for (auto* s : { &lengthSec, &spaceSec, &swellSec, &motionSec, &outSec }) addAndMakeVisible (s);

    // header
    refreshPresetBox();
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0 && i != proc.currentPreset) proc.loadPreset (i); };
    prevBtn.onClick = [this] { const int n = LumenReverseProcessor::getPresetNames().size(); proc.loadPreset ((proc.currentPreset + n - 1) % n); refreshPresetBox(); };
    nextBtn.onClick = [this] { const int n = LumenReverseProcessor::getPresetNames().size(); proc.loadPreset ((proc.currentPreset + 1) % n); refreshPresetBox(); };
    aBtn.onClick = [this] { proc.switchAB (0); };
    bBtn.onClick = [this] { proc.switchAB (1); };
    copyBtn.onClick = [this] { proc.copyAtoB(); };
    copyBtn.setTooltip ("Copy the current slot to the other slot");
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    for (auto* b : { &prevBtn, &nextBtn, &aBtn, &bBtn, &copyBtn, &undoBtn, &redoBtn }) addAndMakeVisible (b);
    addAndMakeVisible (presetBox);

    // transport row
    captureBtn.getProperties().set ("rec", true);
    captureBtn.onClick = [this]
    {
        const int cs = proc.getCaptureState();
        if (cs == LumenReverseProcessor::Recording) proc.stopCapture();
        else if (cs == LumenReverseProcessor::Armed) proc.cancelCapture();
        else proc.armCapture();
    };
    captureBtn.setTooltip ("Record the incoming track audio. Manual: records until you press STOP. Auto: starts on signal, stops on silence. Host Bar: records from the next bar.");
    previewBtn.onClick = [this] { if (proc.isVoicePlaying()) proc.stopPreview(); else proc.triggerPreview(); };
    loopBtn.setClickingTogglesState (true);
    loopBtn.onClick = [this] { proc.loopPreview.store (loopBtn.getToggleState()); };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadAudioFile (fc.getResult()); });
    };
    clearBtn.onClick = [this] { proc.clearSource(); };
    saveBtn.onClick = [this]
    {
        auto f = proc.exportAudio (false);
        statusText = f.existsAsFile() ? "Saved: " + f.getFileName() : "Nothing to export yet";
        if (f.existsAsFile()) f.revealToUser();
    };
    saveBtn.setTooltip ("Write the swell to Documents/Lumen Reverse/Exports and show it");
    for (auto* b : { &captureBtn, &previewBtn, &loopBtn, &loadBtn, &clearBtn, &saveBtn }) addAndMakeVisible (b);
    addAndMakeVisible (display);
    addAndMakeVisible (dragOut);
    addAndMakeVisible (capMode); addAndMakeVisible (capBars); addAndMakeVisible (threshold);
    threshold.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);

    // length
    for (int i = 0; i < LumenReverseProcessor::lengthNames.size(); ++i)
    {
        auto* b = lenButtons.add (new juce::TextButton (LumenReverseProcessor::lengthNames[i].toUpperCase()));
        b->onClick = [this, i]
        {
            if (auto* p = proc.apvts.getParameter ("length")) p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
            if (auto* s = proc.apvts.getParameter ("sync")) s->setValueNotifyingHost (1.0f);
        };
        addAndMakeVisible (b);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &syncToggle, &freeMs, &trigger, &cycle, &pitchTrack }) addAndMakeVisible (c);
    trigger.box.setTooltip ("MIDI: play the swell from a MIDI note (route a MIDI track to this plugin). Host Sync: swell lands exactly on every cycle boundary while the DAW plays.");

    // modules
    for (auto* c : std::initializer_list<juce::Component*> { &algo, &mode, &tremRate, &tremShape, &size, &decay, &predelay, &diffusion, &damping, &mod, &shimmer,
                                                            &rise, &tailFade, &sourceFade, &blend, &sweep, &tremDepth, &tremPhase, &tremFollow,
                                                            &lowCut, &highCut, &width, &gain, &dry, &normalize })
        addAndMakeVisible (c);

    rise.slider.setTooltip ("Curve of the swell: 0 = natural reverb build, higher = holds back then surges into the hit");
    sweep.slider.setTooltip ("Low-pass opens across the swell for a filtered whoosh");
    blend.slider.setTooltip ("Adds the forward source after the swell, so the export contains swell + phrase");
    tremFollow.slider.setTooltip ("Motion depth grows as the swell builds");
    dry.slider.setTooltip ("How much of the live track passes through the plugin");

    readout.draw = [this] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float sx = (float) getWidth() / 1080.0f;
        g.setColour (juce::Colour (0xff101216));
        g.fillRoundedRectangle (r, 8.0f);
        const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
        const juce::String big = sync ? proc.apvts.getParameter ("length")->getCurrentValueAsText().toUpperCase() : "FREE";
        g.setFont (uiFont (30.0f * sx, true).withExtraKerningFactor (0.04f));
        juce::ColourGradient tg (col::amber, r.getX(), r.getY(), col::coral, r.getRight(), r.getBottom(), false);
        g.setGradientFill (tg);
        g.drawText (big, r.withTrimmedBottom (r.getHeight() * 0.36f), juce::Justification::centredBottom);
        g.setColour (col::dim.brighter (0.2f));
        g.setFont (uiFont (12.0f * sx));
        g.drawText (proc.getLengthDescription() + "   @   " + juce::String (proc.getHostBpm(), 1) + " BPM",
                    r.withTrimmedTop (r.getHeight() * 0.66f), juce::Justification::centredTop);
    };
    addAndMakeVisible (readout);

    addMouseListener (this, true);
    setResizable (true, true);
    setResizeLimits (920, 560, 1620, 990);
    getConstrainer()->setFixedAspectRatio (1080.0 / 660.0);
    setSize (1080, 660);
    startTimerHz (30);
}

LumenReverseEditor::~LumenReverseEditor()
{
    removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void LumenReverseEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addItemList (LumenReverseProcessor::getPresetNames(), 1);
    presetBox.setSelectedItemIndex (proc.currentPreset, juce::dontSendNotification);
}

void LumenReverseEditor::mouseDown (const juce::MouseEvent&)
{
    proc.undo.beginNewTransaction();
}

void LumenReverseEditor::paint (juce::Graphics& g)
{
    // backdrop
    g.fillAll (col::bg);
    const float sx = (float) getWidth() / 1080.0f;
    juce::ColourGradient haze (col::coral.withAlpha (0.07f), 0, (float) getHeight(), juce::Colours::transparentBlack, (float) getWidth() * 0.6f, 0, true);
    g.setGradientFill (haze);
    g.fillAll();

    // logo
    auto head = juce::Rectangle<float> (16 * sx, 0, 300 * sx, 52 * sx);
    g.setFont (uiFont (19.0f * sx, true).withExtraKerningFactor (0.22f));
    juce::ColourGradient lg (col::amber, head.getX(), 0, col::coral, head.getX() + 120 * sx, 0, false);
    g.setGradientFill (lg);
    g.drawText ("LUMEN", head, juce::Justification::centredLeft);
    g.setColour (col::text);
    g.setFont (uiFont (19.0f * sx, false).withExtraKerningFactor (0.22f));
    g.drawText ("REVERSE", head.withTrimmedLeft (92 * sx), juce::Justification::centredLeft);

    if (statusText.isNotEmpty())
    {
        g.setColour (col::dim);
        g.setFont (uiFont (11.0f * sx));
        g.drawText (statusText, getLocalBounds().removeFromBottom ((int) (14 * sx)).withTrimmedRight ((int) (18 * sx)), juce::Justification::centredRight);
    }
}

void LumenReverseEditor::layoutSection (juce::Rectangle<int> area, std::vector<juce::Component*> items, int cols)
{
    area.removeFromTop (juce::roundToInt (28.0f * getWidth() / 1080.0f));
    area = area.reduced (8, 4);
    const int rows = ((int) items.size() + cols - 1) / cols;
    const int cw = area.getWidth() / cols, rh = area.getHeight() / rows;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i] == nullptr) continue;
        const int c = (int) i % cols, r = (int) i / cols;
        auto cell = juce::Rectangle<int> (area.getX() + c * cw, area.getY() + r * rh, cw, rh).reduced (3, 2);
        if (dynamic_cast<lumen::ui::Choice*> (items[i]) != nullptr) cell = cell.withHeight (juce::jmin (cell.getHeight(), 52)).withY (cell.getY() + 8);
        items[i]->setBounds (cell);
    }
}

void LumenReverseEditor::resized()
{
    const float s = (float) getWidth() / 1080.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };

    // header
    {
        auto h = juce::Rectangle<int> (0, 0, getWidth(), S (52)).reduced (S (16), S (12));
        auto right = h.removeFromRight (S (380));
        redoBtn.setBounds (right.removeFromRight (S (54)));
        right.removeFromRight (S (6));
        undoBtn.setBounds (right.removeFromRight (S (54)));
        right.removeFromRight (S (16));
        copyBtn.setBounds (right.removeFromRight (S (48)));
        right.removeFromRight (S (4));
        bBtn.setBounds (right.removeFromRight (S (30)));
        right.removeFromRight (S (2));
        aBtn.setBounds (right.removeFromRight (S (30)));

        auto mid = juce::Rectangle<int> (S (300), h.getY(), S (330), h.getHeight());
        prevBtn.setBounds (mid.removeFromLeft (S (28)));
        nextBtn.setBounds (mid.removeFromRight (S (28)));
        presetBox.setBounds (mid.reduced (S (6), 0));
    }

    // main display + transport
    auto main = juce::Rectangle<int> (S (16), S (60), S (760), S (300));
    display.setBounds (main.removeFromTop (S (212)));
    main.removeFromTop (S (8));
    auto row = main.removeFromTop (S (30));
    for (auto* b : { &captureBtn, &previewBtn, &loopBtn, &loadBtn, &clearBtn })
    {
        b->setBounds (row.removeFromLeft (b == &captureBtn ? S (104) : S (70)));
        row.removeFromLeft (S (6));
    }
    dragOut.setBounds (row.removeFromRight (S (150)));
    row.removeFromRight (S (6));
    saveBtn.setBounds (row.removeFromRight (S (80)));

    main.removeFromTop (S (6));
    auto capRow = main.removeFromTop (S (48));
    capMode.setBounds (capRow.removeFromLeft (S (180)));
    capRow.removeFromLeft (S (10));
    capBars.setBounds (capRow.removeFromLeft (S (130)));
    capRow.removeFromLeft (S (10));
    threshold.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    threshold.setBounds (capRow.withWidth (S (240)).withX (capBars.getRight() + S (14)));

    // length panel
    auto lp = juce::Rectangle<int> (S (786), S (60), S (278), S (300));
    lengthSec.setBounds (lp);
    auto lin = lp.withTrimmedTop (S (30)).reduced (S (12), S (6));
    readout.setBounds (lin.removeFromTop (S (70)));
    lin.removeFromTop (S (8));
    auto grid = lin.removeFromTop (S (60));
    const int bw = grid.getWidth() / 4;
    for (int i = 0; i < lenButtons.size(); ++i)
    {
        const int r = i / 4, c = i % 4;
        lenButtons[i]->setBounds (juce::Rectangle<int> (grid.getX() + c * bw, grid.getY() + r * S (30), bw, S (30)).reduced (2));
    }
    lin.removeFromTop (S (4));
    auto syncRow = lin.removeFromTop (S (40));
    syncToggle.setBounds (syncRow.removeFromLeft (S (120)).withSizeKeepingCentre (S (120), S (24)));
    freeMs.setBounds (syncRow.withTrimmedTop (-S (2)));
    freeMs.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    freeMs.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, S (58), S (16));
    lin.removeFromTop (S (2));
    auto trigRow = lin.removeFromTop (S (50));
    trigger.setBounds (trigRow.removeFromLeft (trigRow.getWidth() / 2).reduced (2, 0));
    cycle.setBounds (trigRow.reduced (2, 0));
    pitchTrack.setBounds (lin.removeFromTop (S (26)));

    // modules
    auto mods = juce::Rectangle<int> (S (16), S (372), S (1048), S (272));
    const int gap = S (10);
    const float unit = (mods.getWidth() - 3 * gap) / 13.0f;
    auto take = [&] (int cols) { auto r = mods.removeFromLeft (juce::roundToInt (unit * cols)); mods.removeFromLeft (gap); return r; };
    auto spaceR = take (4), swellR = take (3), motionR = take (3);
    auto outR = mods;
    spaceSec.setBounds (spaceR); swellSec.setBounds (swellR); motionSec.setBounds (motionR); outSec.setBounds (outR);

    layoutSection (spaceR, { &algo, &size, &decay, &predelay, &diffusion, &damping, &mod, &shimmer }, 4);
    layoutSection (swellR, { &mode, &rise, &tailFade, &sourceFade, &blend, &sweep }, 3);
    layoutSection (motionR, { &tremRate, &tremShape, &tremDepth, &tremPhase, &tremFollow, nullptr }, 3);
    layoutSection (outR, { &lowCut, &highCut, &width, &gain, &dry, &normalize }, 3);
}

void LumenReverseEditor::timerCallback()
{
    display.refresh();

    const int cs = proc.getCaptureState();
    captureBtn.setButtonText (cs == LumenReverseProcessor::Recording ? "STOP" : cs == LumenReverseProcessor::Armed ? "ARMED" : "CAPTURE");
    captureBtn.setToggleState (cs == LumenReverseProcessor::Recording || cs == LumenReverseProcessor::Armed, juce::dontSendNotification);
    previewBtn.setButtonText (proc.isVoicePlaying() ? "STOP" : "PLAY");
    previewBtn.setToggleState (proc.isVoicePlaying(), juce::dontSendNotification);
    previewBtn.setEnabled (proc.hasSource());
    dragOut.repaint();

    const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    const int li = (int) proc.apvts.getRawParameterValue ("length")->load();
    for (int i = 0; i < lenButtons.size(); ++i) lenButtons[i]->setToggleState (sync && i == li, juce::dontSendNotification);
    freeMs.setEnabled (! sync);

    const int capM = (int) proc.apvts.getRawParameterValue ("capMode")->load();
    threshold.setEnabled (capM == 1);
    capBars.setEnabled (capM == 2);
    const int trig = (int) proc.apvts.getRawParameterValue ("trigger")->load();
    cycle.setEnabled (trig != 0);
    shimmer.setEnabled (true);
    sourceFade.setEnabled (proc.apvts.getRawParameterValue ("blend")->load() > 0.001f);

    aBtn.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bBtn.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
    if (presetBox.getSelectedItemIndex() != proc.currentPreset) presetBox.setSelectedItemIndex (proc.currentPreset, juce::dontSendNotification);

    readout.repaint();
}
