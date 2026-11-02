#pragma once
#include "Widgets.h"

namespace lxl
{
namespace ui { class Content; }

class PluginEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit PluginEditor (LumenSynthXLProcessor&);
    ~PluginEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void showPageForTest (int page);

    static constexpr int kBaseW = 1200, kBaseH = 800;

private:
    void timerCallback() override;
    LumenSynthXLProcessor& proc;
    ui::LookAndFeel lnf;
    std::unique_ptr<ui::Content> content;
    juce::TooltipWindow tooltips { this, 600 };
};
} // namespace lxl
