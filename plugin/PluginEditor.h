// PluginEditor - a working editor until tier 3 replaces it with the FS1R front panel.
//
// Part buttons, a patch browser over the EPROM banks and .syx files, and every parameter of the
// selected part in a searchable list with MIDI learn on the right-click menu. Deliberately plain: the
// skinned panel is a separate job and this one only has to be usable while the engine is the subject.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace fs1rplug {

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildRows();
    void refreshHeader();
    void openRom();
    void loadSyx();
    void saveSyx();

    class Row;

    Processor& proc;
    juce::TextButton partButtons[4];
    juce::TextButton romButton, loadButton, saveButton;
    juce::ComboBox groupBox, voiceBox, perfBox;
    juce::TextEditor searchBox;
    juce::Label header;
    juce::Viewport viewport;
    juce::Component rows;
    juce::OwnedArray<Row> rowList;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastHeader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

}  // namespace fs1rplug
