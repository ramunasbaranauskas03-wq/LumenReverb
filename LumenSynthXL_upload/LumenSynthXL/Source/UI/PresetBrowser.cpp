#include "PresetBrowser.h"

namespace lxl::ui
{
PresetBrowser::PresetBrowser (LumenSynthXLProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Search presets...", col::textFaint);
    search.setFont (font (14.0f));
    search.setIndents (10, 8);
    search.addListener (this);
    addAndMakeVisible (search);

    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, col::well);
    list.setColour (juce::ListBox::outlineColourId, col::border);
    list.setOutlineThickness (1);
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);
    buildFilterButtons();
    setWantsKeyboardFocus (true);
}

void PresetBrowser::buildFilterButtons()
{
    juce::StringArray cats { "All" };
    cats.addArray (PresetManager::categories());
    cats.add ("User");
    for (auto& c : cats)
    {
        auto* b = catButtons.add (new juce::TextButton (c));
        b->onClick = [this, c] { category = c; refilter(); };
        addAndMakeVisible (b);
    }
    juce::StringArray gs { "All" };
    gs.addArray (PresetManager::genres());
    for (auto& g : gs)
    {
        auto* b = genreButtons.add (new juce::TextButton (g));
        b->onClick = [this, g] { genre = g; refilter(); };
        addAndMakeVisible (b);
    }
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        proc.presets.refresh();
        refilter();
        search.grabKeyboardFocus();
    }
}

void PresetBrowser::refilter()
{
    filtered.clear();
    const auto& e = proc.presets.entries();
    const auto q = search.getText().trim();
    for (int i = 0; i < e.size(); ++i)
    {
        const auto& p = e.getReference (i);
        if (p.category == "Init") continue;
        if (category == "User" ? p.factory : (category != "All" && p.category != category)) continue;
        if (genre != "All" && p.genre != genre) continue;
        if (q.isNotEmpty() && ! (p.name.containsIgnoreCase (q) || p.genre.containsIgnoreCase (q) || p.category.containsIgnoreCase (q))) continue;
        filtered.push_back (i);
    }
    for (auto* b : catButtons) b->setToggleState (b->getButtonText() == category, juce::dontSendNotification);
    for (auto* b : genreButtons) b->setToggleState (b->getButtonText() == genre, juce::dontSendNotification);
    list.updateContent();
    loading = true;
    list.deselectAllRows();
    for (int r = 0; r < (int) filtered.size(); ++r)
        if (filtered[(size_t) r] == proc.presets.currentIndex()) { list.selectRow (r); break; }
    loading = false;
    repaint();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds().reduced (10, 8);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
    g.setFont (labelFont (10.5f));
    g.setColour (col::textDim);
    g.drawText ("CATEGORY", r.getX() + 14, r.getY() + 8, 150, 20, juce::Justification::centredLeft);
    g.drawText ("GENRE", r.getX() + 178, r.getY() + 8, 150, 20, juce::Justification::centredLeft);
    g.drawText (juce::String ((int) filtered.size()) + " PRESETS", r.getRight() - 300, r.getY() + 8, 200, 20, juce::Justification::centredRight);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (10, 8).reduced (14, 8);
    r.removeFromTop (24);
    auto cats = r.removeFromLeft (150);
    for (auto* b : catButtons) { b->setBounds (cats.removeFromTop (34)); cats.removeFromTop (4); }
    r.removeFromLeft (14);
    auto genres = r.removeFromLeft (170);
    const int gh = juce::jmin (34, (genres.getHeight() - 4 * genreButtons.size()) / juce::jmax (1, genreButtons.size()));
    for (auto* b : genreButtons) { b->setBounds (genres.removeFromTop (gh)); genres.removeFromTop (4); }
    r.removeFromLeft (14);
    auto top = r.removeFromTop (34);
    closeBtn.setBounds (top.removeFromRight (80));
    top.removeFromRight (8);
    search.setBounds (top);
    r.removeFromTop (8);
    list.setBounds (r);
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    const auto& e = proc.presets.entries().getReference (filtered[(size_t) row]);
    if (selected)
    {
        g.setColour (col::accentDim.withAlpha (0.7f));
        g.fillRect (0, 0, w, h);
    }
    else if (row % 2)
    {
        g.setColour (col::panel.withAlpha (0.4f));
        g.fillRect (0, 0, w, h);
    }
    g.setColour (selected ? juce::Colours::white : col::text);
    g.setFont (font (14.0f, selected));
    g.drawText (e.name, 14, 0, w / 2, h, juce::Justification::centredLeft);
    g.setFont (font (12.0f));
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : col::textDim);
    g.drawText (e.category, w / 2, 0, w / 5, h, juce::Justification::centredLeft);
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : col::mod.withAlpha (0.85f));
    g.drawText (e.genre, w / 2 + w / 5, 0, w - (w / 2 + w / 5) - 10, h, juce::Justification::centredLeft);
}

void PresetBrowser::loadRow (int row)
{
    if (loading || row < 0 || row >= (int) filtered.size()) return;
    if (filtered[(size_t) row] != proc.presets.currentIndex())
    {
        proc.undoManager.beginNewTransaction ("Load preset");
        proc.presets.load (filtered[(size_t) row]);
    }
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent&) { loadRow (row); }
void PresetBrowser::selectedRowsChanged (int row) { loadRow (row); }

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey)
    {
        const int r = juce::jlimit (0, (int) filtered.size() - 1, list.getSelectedRow() + (k == juce::KeyPress::downKey ? 1 : -1));
        list.selectRow (r);
        return true;
    }
    return false;
}
} // namespace lxl::ui
