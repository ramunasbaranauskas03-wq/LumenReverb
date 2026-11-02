#pragma once
#include "Widgets.h"

namespace lxl::ui
{
/** Full-page preset browser: categories, genres, search, single-click load. */
class PresetBrowser : public juce::Component, private juce::ListBoxModel, private juce::TextEditor::Listener
{
public:
    explicit PresetBrowser (LumenSynthXLProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed (const juce::KeyPress&) override;
    void refilter();
    std::function<void()> onClose;

private:
    int getNumRows() override { return (int) filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int row) override;
    void textEditorTextChanged (juce::TextEditor&) override { refilter(); }

    void buildFilterButtons();
    void loadRow (int row);

    LumenSynthXLProcessor& proc;
    juce::TextEditor search;
    juce::ListBox list { "presets", this };
    juce::OwnedArray<juce::TextButton> catButtons, genreButtons;
    juce::TextButton closeBtn { "CLOSE" };
    juce::String category = "All", genre = "All";
    std::vector<int> filtered;
    bool loading = false;
};
} // namespace lxl::ui
