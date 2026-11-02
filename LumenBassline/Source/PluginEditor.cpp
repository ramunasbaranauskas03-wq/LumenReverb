#include "PluginEditor.h"

using namespace lbui;

static juce::Font uiFont (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
LumenLnf::LumenLnf()
{
    setColour (juce::ComboBox::backgroundColourId, Pal::panelHi);
    setColour (juce::ComboBox::textColourId, Pal::text);
    setColour (juce::ComboBox::outlineColourId, Pal::line);
    setColour (juce::ComboBox::arrowColourId, Pal::amber);
    setColour (juce::PopupMenu::backgroundColourId, Pal::panel);
    setColour (juce::PopupMenu::textColourId, Pal::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::amber.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Pal::text);
    setColour (juce::Label::textColourId, Pal::dim);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, Pal::amber);
    setColour (juce::TextEditor::backgroundColourId, Pal::panelHi);
    setColour (juce::Slider::textBoxTextColourId, Pal::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, Pal::text);
    setColour (juce::TextButton::textColourOnId, Pal::bg);
    setColour (juce::TooltipWindow::backgroundColourId, Pal::panelHi);
    setColour (juce::TooltipWindow::textColourId, Pal::text);
}

void LumenLnf::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);

    g.setColour (Pal::panelHi);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 2, r - 2, 0, start, end, true);
    g.setColour (Pal::line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, r - 2, r - 2, 0, start, angle, true);
    juce::ColourGradient grad (Pal::amber, c.x - r, c.y + r, Pal::coral, c.x + r, c.y - r, false);
    g.setGradientFill (grad);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float inner = r * 0.62f;
    g.setColour (Pal::bg);
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2, inner * 2);
    juce::Point<float> tip (c.x + std::sin (angle) * (inner - 3), c.y - std::cos (angle) * (inner - 3));
    g.setColour (s.isMouseOverOrDragging() ? Pal::amber : Pal::text);
    g.drawLine (c.x + std::sin (angle) * inner * 0.3f, c.y - std::cos (angle) * inner * 0.3f, tip.x, tip.y, 2.0f);
}

void LumenLnf::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getProperties()["primary"];
    if (primary)
    {
        juce::ColourGradient grad (Pal::amber, r.getX(), r.getY(), Pal::coral, r.getRight(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 6.0f);
        if (over || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.25f : 0.12f)); g.fillRoundedRectangle (r, 6.0f); }
        return;
    }
    g.setColour (b.isEnabled() ? (over ? Pal::line : Pal::panelHi) : Pal::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (down ? Pal::amber : Pal::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LumenLnf::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? Pal::amber.withAlpha (0.16f) : (over ? Pal::line : Pal::panelHi));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? Pal::amber : Pal::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    auto dot = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.36f);
    g.setColour (on ? Pal::amber : Pal::dim.withAlpha (0.5f));
    g.fillEllipse (dot);
    g.setColour (on ? Pal::text : Pal::dim);
    g.setFont (uiFont (11.5f, true));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedRight (6), juce::Justification::centredLeft, 1);
}

void LumenLnf::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? Pal::line : Pal::panelHi);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Pal::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (Pal::amber);
    g.fillPath (arrow);
}

juce::Font LumenLnf::getComboBoxFont (juce::ComboBox&) { return uiFont (13.0f, true); }
juce::Font LumenLnf::getTextButtonFont (juce::TextButton&, int) { return uiFont (12.5f, true); }
void LumenLnf::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 26, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
// Piano roll
//==============================================================================
void PianoRoll::computeRows()
{
    int mn = 0, mx = 7;
    for (int i = 0; i < pat.numNotes; ++i)
    {
        mn = juce::jmin (mn, (int) pat.notes[(size_t) i].degree);
        mx = juce::jmax (mx, (int) pat.notes[(size_t) i].degree);
    }
    lo = juce::jmin (-3, mn - 1);
    hi = juce::jmax (lo + 14, mx + 2);
}

void PianoRoll::refresh (bool force)
{
    const int v = proc.getPatternVersion();
    if (force || v != version)
    {
        if (dragIndex < 0) // don't yank the pattern out from under a drag
        {
            pat = proc.getPattern();
            version = v;
            computeRows();
        }
    }
    repaint();
}

juce::Rectangle<float> PianoRoll::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (keyW).withTrimmedTop (headerH);
}

int PianoRoll::stepAt (float x) const
{
    const auto g = gridArea();
    return juce::jlimit (0, pat.lengthSteps() - 1, (int) ((x - g.getX()) / g.getWidth() * pat.lengthSteps()));
}

int PianoRoll::degreeAt (float y) const
{
    const auto g = gridArea();
    const int rows = hi - lo + 1;
    const int row = juce::jlimit (0, rows - 1, (int) ((y - g.getY()) / g.getHeight() * rows));
    return hi - row;
}

juce::Rectangle<float> PianoRoll::noteRect (const lb::Note& n) const
{
    const auto g = gridArea();
    const float sw = g.getWidth() / (float) pat.lengthSteps();
    const float rh = g.getHeight() / (float) (hi - lo + 1);
    const int row = hi - n.degree;
    return { g.getX() + n.step * sw + 1.0f, g.getY() + row * rh + 1.0f, n.len * sw - 2.0f, rh - 2.0f };
}

int PianoRoll::hitNote (juce::Point<float> p) const
{
    for (int i = pat.numNotes - 1; i >= 0; --i)
        if (noteRect (pat.notes[(size_t) i]).expanded (0, 1).contains (p)) return i;
    return -1;
}

void PianoRoll::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Pal::panel);
    g.fillRoundedRectangle (b, 8.0f);

    const auto grid = gridArea();
    const int rows = hi - lo + 1;
    const float rh = grid.getHeight() / (float) rows;
    const int steps = pat.lengthSteps();
    const float sw = grid.getWidth() / (float) steps;

    // rows + key labels
    for (int r = 0; r < rows; ++r)
    {
        const int deg = hi - r;
        const bool root = ((deg % 7) + 7) % 7 == 0;
        const float y = grid.getY() + r * rh;
        g.setColour (root ? Pal::amber.withAlpha (0.07f) : ((r & 1) ? Pal::panel : Pal::panelHi.withAlpha (0.5f)));
        g.fillRect (grid.getX(), y, grid.getWidth(), rh);

        lb::Note probe; probe.degree = (int16_t) deg;
        const int pitch = proc.pitchForNote (probe, false);
        g.setColour (root ? Pal::amber : Pal::dim);
        g.setFont (uiFont (juce::jmin (11.0f, rh * 0.8f), root));
        g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 3), 4, (int) y, (int) keyW - 8, (int) rh,
                    juce::Justification::centredRight);
    }

    // vertical grid
    for (int s = 0; s <= steps; ++s)
    {
        const float x = grid.getX() + s * sw;
        const bool bar = s % lb::stepsPerBar == 0, beat = s % 4 == 0;
        g.setColour (bar ? Pal::dim.withAlpha (0.55f) : beat ? Pal::line.brighter (0.15f) : Pal::line.withAlpha (0.45f));
        g.drawVerticalLine ((int) x, grid.getY(), grid.getBottom());
    }

    // bar header with progression chords
    static const char* roman[] = { "i", "ii", "III", "iv", "v", "VI", "VII" };
    const auto& prog = lb::progression ((int) proc.apvts.getRawParameterValue ("progression")->load());
    for (int bar = 0; bar < pat.bars; ++bar)
    {
        const float x = grid.getX() + bar * lb::stepsPerBar * sw;
        g.setColour (Pal::dim);
        g.setFont (uiFont (10.5f, true));
        juce::String t = juce::String (bar + 1);
        if (prog.length > 1) t << "  " << roman[prog.degrees[bar % prog.length] % 7];
        g.drawText (t, (int) x + 5, 1, (int) (lb::stepsPerBar * sw) - 6, (int) headerH - 2, juce::Justification::centredLeft);
    }

    // slide connectors
    for (int i = 0; i + 1 < pat.numNotes; ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        if (! n.slide) continue;
        const auto a = noteRect (n), c = noteRect (pat.notes[(size_t) i + 1]);
        g.setColour (Pal::teal.withAlpha (0.8f));
        g.drawLine (a.getRight() - 2, a.getCentreY(), c.getX() + 2, c.getCentreY(), 2.0f);
    }

    // notes
    for (int i = 0; i < pat.numNotes; ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        auto r = noteRect (n);
        const float velA = 0.45f + 0.55f * (n.vel / 127.0f);
        juce::Colour c = n.ghost ? Pal::dim : (n.slide ? Pal::teal : Pal::amber);
        if (n.accent && ! n.ghost) c = c.brighter (0.25f);
        g.setColour (c.withAlpha (n.ghost ? 0.45f : velA));
        g.fillRoundedRectangle (r, 3.0f);
        if (n.accent && ! n.ghost)
        {
            g.setColour (Pal::coral);
            g.fillRoundedRectangle (r.withHeight (2.5f), 1.0f);
        }
        if (n.chroma != 0)
        {
            g.setColour (Pal::bg.withAlpha (0.8f));
            g.setFont (uiFont (9.0f, true));
            g.drawText (n.chroma < 0 ? "b" : "#", r.toNearestInt(), juce::Justification::centred);
        }
        if (n.locked)
        {
            g.setColour (Pal::text);
            g.drawRoundedRectangle (r, 3.0f, 1.5f);
            g.fillEllipse (r.getRight() - 6, r.getY() + 2, 4, 4);
        }
    }

    // playhead
    const float ph = proc.getPlayheadStep();
    if (ph >= 0.0f)
    {
        const float x = grid.getX() + ph * sw;
        g.setColour (Pal::text.withAlpha (0.85f));
        g.drawLine (x, grid.getY() - headerH + 2, x, grid.getBottom(), 1.5f);
    }
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const int i = hitNote (e.position);
    if (i >= 0 && e.position.x > noteRect (pat.notes[(size_t) i]).getRight() - 6)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    if (! gridArea().contains (e.position)) return;
    downPos = e.position;
    moved = false;
    createdOnDown = false;
    int i = hitNote (e.position);

    if (e.mods.isPopupMenu()) { if (i >= 0) showMenu (i); return; }
    if (i >= 0 && e.mods.isShiftDown())
    {
        pat.notes[(size_t) i].locked = ! pat.notes[(size_t) i].locked;
        commit();
        return;
    }
    if (i < 0)
    {
        lb::Note n;
        n.step = (int16_t) stepAt (e.position.x);
        n.degree = (int16_t) degreeAt (e.position.y);
        n.len = 1; n.vel = 100;
        // replace a note already starting on this step
        const int existing = pat.noteAtStep (n.step);
        if (existing >= 0) pat.remove (existing);
        if (! pat.add (n)) return;
        pat.sortByStep();
        i = pat.noteAtStep (n.step);
        createdOnDown = true;
        dragMode = 2;
    }
    else
        dragMode = (e.position.x > noteRect (pat.notes[(size_t) i]).getRight() - 6) ? 2 : 1;

    dragIndex = i;
    dragStartStep = pat.notes[(size_t) i].step;
    dragStartDeg  = pat.notes[(size_t) i].degree;
    dragStartLen  = pat.notes[(size_t) i].len;
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0) return;
    if (e.getDistanceFromDragStart() > 3) moved = true;
    auto& n = pat.notes[(size_t) dragIndex];
    const auto g = gridArea();
    const float sw = g.getWidth() / (float) pat.lengthSteps();
    const float rh = g.getHeight() / (float) (hi - lo + 1);
    const int dSteps = (int) std::round ((e.position.x - downPos.x) / sw);

    if (dragMode == 2)
        n.len = (int16_t) juce::jlimit (1, pat.lengthSteps() - n.step, dragStartLen + dSteps);
    else
    {
        n.step = (int16_t) juce::jlimit (0, pat.lengthSteps() - n.len, dragStartStep + dSteps);
        n.degree = (int16_t) juce::jlimit (lo, hi, dragStartDeg - (int) std::round ((e.position.y - downPos.y) / rh));
    }
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex < 0) return;
    if (! moved && ! createdOnDown && dragMode == 1)
        pat.remove (dragIndex); // click on a note = delete
    else
    {
        // a moved note replaces whatever it landed on
        const auto moving = pat.notes[(size_t) dragIndex];
        for (int j = pat.numNotes - 1; j >= 0; --j)
            if (j != dragIndex && pat.notes[(size_t) j].step == moving.step) { pat.remove (j); if (j < dragIndex) --dragIndex; }
    }
    dragIndex = -1;
    commit();
}

void PianoRoll::commit()
{
    pat.sortByStep();
    for (int i = 0; i < pat.numNotes; ++i)
    {
        auto& n = pat.notes[(size_t) i];
        const int next = (i + 1 < pat.numNotes) ? pat.notes[(size_t) i + 1].step : pat.lengthSteps();
        n.len = (int16_t) juce::jlimit (1, juce::jmax (1, next - n.step), (int) n.len);
        if (n.slide && (i + 1 >= pat.numNotes || n.step + n.len != next)) n.slide = false;
    }
    proc.setPatternFromEditor (pat);
    version = proc.getPatternVersion();
    computeRows();
    repaint();
}

void PianoRoll::showMenu (int index)
{
    const auto n = pat.notes[(size_t) index];
    juce::PopupMenu m;
    m.addItem (1, "Slide into next note", true, n.slide);
    m.addItem (2, "Accent", true, n.accent);
    m.addItem (3, "Ghost note", true, n.ghost);
    m.addItem (4, "Lock (keep on Generate)", true, n.locked);
    m.addSeparator();
    m.addItem (5, "Octave up");
    m.addItem (6, "Octave down");
    m.addItem (7, "Delete");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [this, index, step = n.step] (int r)
        {
            if (r == 0 || index >= pat.numNotes || pat.notes[(size_t) index].step != step) return;
            auto& x = pat.notes[(size_t) index];
            switch (r)
            {
                case 1:
                {
                    x.slide = ! x.slide;
                    if (x.slide && index + 1 < pat.numNotes) x.len = (int16_t) (pat.notes[(size_t) index + 1].step - x.step);
                    break;
                }
                case 2: x.accent = ! x.accent; x.vel = (uint8_t) (x.accent ? 118 : 96); break;
                case 3: x.ghost = ! x.ghost; x.vel = (uint8_t) (x.ghost ? 58 : 96); break;
                case 4: x.locked = ! x.locked; break;
                case 5: x.degree = (int16_t) (x.degree + 7); break;
                case 6: x.degree = (int16_t) (x.degree - 7); break;
                case 7: pat.remove (index); break;
                default: break;
            }
            commit();
        });
}

//==============================================================================
void DragMidiBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (dragging ? Pal::teal.withAlpha (0.2f) : (isMouseOver() ? Pal::line : Pal::panelHi));
    g.fillRoundedRectangle (r, 6.0f);
    const float dash[] = { 4.0f, 3.0f };
    juce::Path p; p.addRoundedRectangle (r, 6.0f);
    juce::Path dashed; juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dash, 2);
    g.setColour (Pal::teal);
    g.fillPath (dashed);

    auto icon = r.removeFromLeft (r.getHeight()).reduced (10.0f, 9.0f);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (icon.getX() + i * icon.getWidth() / 3.0f, icon.getY() + (i % 2) * icon.getHeight() * 0.45f,
                                icon.getWidth() / 3.5f, icon.getHeight() * 0.45f, 1.0f);
    g.setColour (Pal::text);
    g.setFont (uiFont (12.0f, true));
    g.drawText ("DRAG MIDI", r.toNearestInt(), juce::Justification::centredLeft);
}

void DragMidiBox::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4) return;
    dragging = true;
    repaint();
    const auto file = proc.writeTempMidi();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [safe = juce::Component::SafePointer<DragMidiBox> (this)]
                                                                { if (safe) { safe->dragging = false; safe->repaint(); } });
}

//==============================================================================
// Editor
//==============================================================================
LumenBasslineEditor::LumenBasslineEditor (LumenBasslineProcessor& p)
    : AudioProcessorEditor (&p), proc (p), roll (p), dragBox (p)
{
    setLookAndFeel (&lnf);

    setupCombo (styleBox, "style");
    setupCombo (soundBox, "sound");
    setupCombo (keyBox, "key");
    setupCombo (scaleBox, "scale");
    setupCombo (barsBox, "bars");
    setupCombo (rangeBox, "range");
    setupCombo (progBox, "progression");

    for (auto* b : { &stylePrev, &styleNext })
    {
        addAndMakeVisible (*b);
        b->onClick = [this, b]
        {
            const int n = styleBox.getNumItems();
            styleBox.setSelectedItemIndex ((styleBox.getSelectedItemIndex() + (b == &styleNext ? 1 : n - 1)) % n);
        };
    }

    octaveSlider.setSliderStyle (juce::Slider::IncDecButtons);
    octaveSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 26);
    octaveSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    addAndMakeVisible (octaveSlider);
    octaveAtt = std::make_unique<SA> (proc.apvts, "octave", octaveSlider);

    setupToggle (playBtn, "preview", "PLAY");
    setupToggle (drumsBtn, "drumsOn", "DRUMS");
    setupToggle (followBtn, "follow", "FOLLOW MIDI");
    setupToggle (liveBtn, "liveRegen", "LIVE RESHAPE");
    setupToggle (fillBtn, "fill", "FILLS");
    setupToggle (synthBtn, "synthOn", "SYNTH");
    playBtn.setTooltip ("Preview without starting your DAW (follows the host when it plays)");
    followBtn.setTooltip ("Hold a note or chord on the plugin's MIDI input to move the bassline's root live");
    liveBtn.setTooltip ("Knobs reshape the current groove with the same seed. Locked notes are always kept.");
    synthBtn.setTooltip ("Turn off to use only the MIDI output with your own bass synth");

    generateBtn.getProperties().set ("primary", true);
    generateBtn.setTooltip ("New bassline (locked notes are kept)");
    mutateBtn.setTooltip ("Evolve the current bassline (amount follows Variation)");
    backBtn.setTooltip ("Previous bassline");
    fwdBtn.setTooltip ("Next bassline");
    clearLocksBtn.setTooltip ("Shift-click a note to lock it; this clears all locks");
    for (auto* b : { &generateBtn, &mutateBtn, &backBtn, &fwdBtn, &clearLocksBtn, &saveBtn }) addAndMakeVisible (*b);
    generateBtn.onClick   = [this] { proc.generateNew(); roll.refresh (true); };
    mutateBtn.onClick     = [this] { proc.mutatePattern(); roll.refresh (true); };
    backBtn.onClick       = [this] { proc.historyBack(); roll.refresh (true); };
    fwdBtn.onClick        = [this] { proc.historyForward(); roll.refresh (true); };
    clearLocksBtn.onClick = [this] { proc.clearLocks(); roll.refresh (true); };
    saveBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save bassline MIDI",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (proc.writeTempMidi().getFileName()), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f != juce::File()) proc.writeMidiFile (f.withFileExtension ("mid"));
                              });
    };

    for (auto* l : { &seedLabel, &statusLabel })
    {
        l->setFont (uiFont (12.0f, true));
        l->setColour (juce::Label::textColourId, Pal::dim);
        addAndMakeVisible (*l);
    }
    statusLabel.setJustificationType (juce::Justification::centredRight);

    const char* rowNames[] = { "STYLE", "SOUND", "KEY", "SCALE", "OCTAVE", "BARS", "RANGE", "CHORDS" };
    for (auto* n : rowNames)
    {
        auto l = std::make_unique<juce::Label> (n, n);
        l->setFont (uiFont (10.0f, true));
        l->setColour (juce::Label::textColourId, Pal::dim);
        addAndMakeVisible (*l);
        rowLabels.push_back (std::move (l));
    }

    addKnob ("density", "DENSITY", 0);      addKnob ("sync", "SYNCOPATION", 0);  addKnob ("kickAvoid", "KICK AVOID", 0);
    addKnob ("swing", "SWING", 0);          addKnob ("gate", "GATE", 0);         addKnob ("humanize", "HUMANIZE", 0);
    addKnob ("octJump", "OCTAVE JUMP", 1);  addKnob ("glide", "SLIDES", 1);      addKnob ("ghost", "GHOST NOTES", 1);
    addKnob ("variation", "VARIATION", 1);  addKnob ("accent", "ACCENT", 1);
    addKnob ("cutoff", "CUTOFF", 2);        addKnob ("reso", "RESONANCE", 2);    addKnob ("envAmt", "ENV AMT", 2);
    addKnob ("decay", "DECAY", 2);          addKnob ("drive", "DRIVE", 2);       addKnob ("sub", "SUB", 2);
    addKnob ("glideTime", "GLIDE TIME", 2); addKnob ("volume", "VOLUME", 2);     addKnob ("drums", "DRUMS", 2);
    addKnob ("duck", "SIDECHAIN", 2);

    addAndMakeVisible (roll);
    addAndMakeVisible (dragBox);

    setSize (1060, 690);
    roll.refresh (true);
    startTimerHz (30);
}

LumenBasslineEditor::~LumenBasslineEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LumenBasslineEditor::setupCombo (juce::ComboBox& box, const char* id)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        box.addItemList (param->choices, 1);
    addAndMakeVisible (box);
    comboAtts.push_back (std::make_unique<CA> (proc.apvts, id, box));
}

void LumenBasslineEditor::setupToggle (juce::ToggleButton& b, const char* id, const char* text)
{
    b.setButtonText (text);
    addAndMakeVisible (b);
    buttonAtts.push_back (std::make_unique<BA> (proc.apvts, id, b));
}

void LumenBasslineEditor::addKnob (const char* id, const char* label, int group)
{
    auto k = std::make_unique<Knob>();
    k->group = group;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setColour (juce::Slider::textBoxTextColourId, Pal::text);
    k->slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k->label.setText (label, juce::dontSendNotification);
    k->label.setFont (uiFont (10.0f, true));
    k->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->att = std::make_unique<SA> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
}

void LumenBasslineEditor::paint (juce::Graphics& g)
{
    g.fillAll (Pal::bg);

    // header
    g.setColour (Pal::dim);
    g.setFont (uiFont (10.5f, true));
    g.drawText ("L U M E N   A U D I O", 20, 12, 220, 14, juce::Justification::left);
    juce::ColourGradient grad (Pal::amber, 20, 28, Pal::coral, 240, 50, false);
    g.setGradientFill (grad);
    g.setFont (uiFont (25.0f, true));
    g.drawText ("BASSLINE", 18, 24, 200, 30, juce::Justification::left);
    g.setColour (Pal::text.withAlpha (0.5f));
    g.setFont (uiFont (10.5f, true));
    g.drawText ("PRO  v1.0", 146, 34, 80, 14, juce::Justification::left);

    for (auto [r, name] : { std::pair { groovePanel, "GROOVE" }, std::pair { pitchPanel, "MELODY" }, std::pair { soundPanel, "PREVIEW SOUND" } })
    {
        g.setColour (Pal::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (Pal::amber.withAlpha (0.85f));
        g.setFont (uiFont (10.5f, true));
        g.drawText (name, r.getX() + 12, r.getY() + 6, 200, 14, juce::Justification::left);
    }

    g.setColour (Pal::dim.withAlpha (0.6f));
    g.setFont (uiFont (10.5f));
    g.drawText ("Click = add / delete   Drag = move   Edge = length   Shift-click = lock   Right-click = slide, accent, ghost, octave",
                roll.getX(), roll.getBottom() + 3, roll.getWidth(), 14, juce::Justification::centredLeft);
}

void LumenBasslineEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    // --- header row
    auto top = area.removeFromTop (50);
    top.removeFromLeft (230);
    playBtn.setBounds (top.removeFromLeft (84).withSizeKeepingCentre (84, 30));
    top.removeFromLeft (14);
    auto placeLabelled = [] (juce::Label& l, juce::Component& c, juce::Rectangle<int> r)
    {
        l.setBounds (r.removeFromTop (13));
        c.setBounds (r.withHeight (28));
    };
    auto styleArea = top.removeFromLeft (270).withTrimmedTop (4);
    rowLabels[0]->setBounds (styleArea.removeFromTop (13));
    auto sRow = styleArea.withHeight (28);
    stylePrev.setBounds (sRow.removeFromLeft (28)); sRow.removeFromLeft (4);
    styleNext.setBounds (sRow.removeFromRight (28)); sRow.removeFromRight (4);
    styleBox.setBounds (sRow);
    top.removeFromLeft (14);
    placeLabelled (*rowLabels[1], soundBox, top.removeFromLeft (150).withTrimmedTop (4));
    top.removeFromLeft (12);
    synthBtn.setBounds (top.removeFromLeft (80).withSizeKeepingCentre (80, 28).translated (0, 6));
    top.removeFromLeft (8);
    drumsBtn.setBounds (top.removeFromLeft (80).withSizeKeepingCentre (80, 28).translated (0, 6));
    statusLabel.setBounds (top.withTrimmedTop (14).withHeight (24));

    // --- musical setup row
    area.removeFromTop (8);
    auto row = area.removeFromTop (44);
    auto cell = [&row] (int w) { auto r = row.removeFromLeft (w); row.removeFromLeft (10); return r; };
    placeLabelled (*rowLabels[2], keyBox,       cell (70));
    placeLabelled (*rowLabels[3], scaleBox,     cell (160));
    placeLabelled (*rowLabels[4], octaveSlider, cell (96));
    placeLabelled (*rowLabels[5], barsBox,      cell (60));
    placeLabelled (*rowLabels[6], rangeBox,     cell (90));
    placeLabelled (*rowLabels[7], progBox,      cell (170));
    row.removeFromTop (13);
    for (auto* b : { &followBtn, &liveBtn, &fillBtn })
    {
        b->setBounds (row.removeFromLeft (b == &fillBtn ? 74 : 112).withHeight (28));
        row.removeFromLeft (8);
    }

    // --- piano roll
    area.removeFromTop (10);
    roll.setBounds (area.removeFromTop (232));
    area.removeFromTop (20);

    // --- actions
    auto act = area.removeFromTop (38);
    generateBtn.setBounds (act.removeFromLeft (150)); act.removeFromLeft (8);
    mutateBtn.setBounds (act.removeFromLeft (96));    act.removeFromLeft (16);
    backBtn.setBounds (act.removeFromLeft (34));      act.removeFromLeft (4);
    fwdBtn.setBounds (act.removeFromLeft (34));       act.removeFromLeft (10);
    seedLabel.setBounds (act.removeFromLeft (120));
    dragBox.setBounds (act.removeFromRight (150));    act.removeFromRight (8);
    saveBtn.setBounds (act.removeFromRight (96));     act.removeFromRight (8);
    clearLocksBtn.setBounds (act.removeFromRight (104));

    // --- knob panels
    area.removeFromTop (10);
    auto panels = area;
    const int unit = (panels.getWidth() - 20) / 11;
    groovePanel = panels.removeFromLeft (unit * 3); panels.removeFromLeft (10);
    pitchPanel  = panels.removeFromLeft (unit * 3); panels.removeFromLeft (10);
    soundPanel  = panels;

    auto layoutGroup = [this] (int group, juce::Rectangle<int> r, int cols)
    {
        r = r.reduced (6).withTrimmedTop (16);
        const int kw = r.getWidth() / cols, kh = r.getHeight() / 2;
        int idx = 0;
        for (auto& k : knobs)
        {
            if (k->group != group) continue;
            auto c = juce::Rectangle<int> (r.getX() + (idx % cols) * kw, r.getY() + (idx / cols) * kh, kw, kh);
            k->label.setBounds (c.removeFromTop (13));
            k->slider.setBounds (c);
            ++idx;
        }
    };
    layoutGroup (0, groovePanel, 3);
    layoutGroup (1, pitchPanel, 3);
    layoutGroup (2, soundPanel, 5);
}

void LumenBasslineEditor::timerCallback()
{
    roll.refresh();
    backBtn.setEnabled (proc.canGoBack());
    fwdBtn.setEnabled (proc.canGoForward());
    seedLabel.setText ("SEED  " + juce::String (proc.getSeed()), juce::dontSendNotification);

    juce::String status = juce::String (proc.getBpm(), 1) + " BPM";
    if (followBtn.getToggleState())
    {
        const int s = proc.getFollowShift();
        status = "ROOT " + juce::String (s >= 0 ? "+" : "") + juce::String (s) + "   " + status;
    }
    statusLabel.setText (status, juce::dontSendNotification);
}
