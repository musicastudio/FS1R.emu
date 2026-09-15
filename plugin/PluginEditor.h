// PluginEditor - the FS1R panel reproduced across the top, expanded editor pages under it.
//
// The panel alone cannot reach 900 parameters comfortably, so the hardware sits above and the pages
// carry the rest, switched with tabs, exactly as tier 3 of the TODO describes. The panel's own mode
// buttons select the page, the way pressing EDIT [VOICE] takes you to the operator pages on a real
// unit.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PanelView.h"
#include "EditorPages.h"

namespace fs1rplug {

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addPage(const juce::String& name, ParameterPage* page);
    void showPage(const juce::String& name);
    void openRom();
    void loadSyx();
    void saveSyx();

    Processor& proc;
    PanelView panel;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    juce::TextButton romButton, loadButton, saveButton;
    juce::ComboBox voiceBox, perfBox;
    juce::TextEditor searchBox;
    juce::OwnedArray<ParameterPage> pages;
    juce::StringArray pageNames;
    std::unique_ptr<AlgorithmView> algView;
    std::unique_ptr<SpectrumView> spectrumView;
    std::unique_ptr<EnvelopeView> ampEg, pitchEg, filterEg;
    std::unique_ptr<FseqView> fseqView;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips{this, 600};
    int lastPart = -1, lastAlg = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

}  // namespace fs1rplug
