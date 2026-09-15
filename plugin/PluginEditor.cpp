#include "PluginEditor.h"

namespace fs1rplug {

// One parameter: name, slider, value text, and a right-click menu for MIDI learn.
class Editor::Row : public juce::Component, private juce::Slider::Listener {
public:
    Row(Processor& p, int index) : proc(p), idx(index) {
        const auto& d = p.descriptions()[(size_t)index];
        name.setText(d.name, juce::dontSendNotification);
        name.setTooltip(d.description.isNotEmpty() ? d.description : d.group + " " + d.name);
        addAndMakeVisible(name);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange(d.min, d.max, 1.0);
        slider.addListener(this);
        addAndMakeVisible(slider);
        value.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(value);
        refresh();
    }
    void refresh() {
        auto* p = proc.parameterFor(idx);
        const auto& d = proc.descriptions()[(size_t)idx];
        int v = (int)std::lround(p->convertFrom0to1(p->getValue()));
        if (v != last) {
            last = v;
            slider.setValue(v, juce::dontSendNotification);
        }
        auto t = d.textFor(v);
        int cc = proc.mappedCcFor(idx);
        if (cc >= 0) t += "  [CC" + juce::String(cc) + "]";
        if (t != valueText) { valueText = t; value.setText(t, juce::dontSendNotification); }
    }
    void resized() override {
        auto r = getLocalBounds();
        name.setBounds(r.removeFromLeft(240));
        value.setBounds(r.removeFromRight(130));
        slider.setBounds(r.reduced(4, 2));
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addItem(1, "MIDI learn");
        m.addItem(2, "Clear MIDI mapping", proc.mappedCcFor(idx) >= 0);
        m.addItem(3, "Reset to default");
        m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
            if (r == 1) proc.startMidiLearn(idx);
            else if (r == 2) proc.clearMidiMapping(idx);
            else if (r == 3) {
                auto* p = proc.parameterFor(idx);
                p->setValueNotifyingHost(p->getDefaultValue());
            }
        });
    }

private:
    void sliderValueChanged(juce::Slider* s) override {
        auto* p = proc.parameterFor(idx);
        p->setValueNotifyingHost(p->convertTo0to1((float)s->getValue()));
    }
    Processor& proc;
    const int idx;
    juce::Label name, value;
    juce::Slider slider;
    int last = -99999;
    juce::String valueText;
};

Editor::Editor(Processor& p) : juce::AudioProcessorEditor(&p), proc(p) {
    for (int i = 0; i < 4; ++i) {
        partButtons[i].setButtonText("Part " + juce::String(i + 1));
        partButtons[i].setClickingTogglesState(true);
        partButtons[i].setRadioGroupId(1);
        partButtons[i].onClick = [this, i] { proc.selectPart(i); rebuildRows(); };
        addAndMakeVisible(partButtons[i]);
    }
    partButtons[proc.selectedPart()].setToggleState(true, juce::dontSendNotification);

    romButton.setButtonText("EPROM...");
    loadButton.setButtonText("Load .syx");
    saveButton.setButtonText("Save .syx");
    romButton.onClick = [this] { openRom(); };
    loadButton.onClick = [this] { loadSyx(); };
    saveButton.onClick = [this] { saveSyx(); };
    addAndMakeVisible(romButton);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(saveButton);

    juce::StringArray groups;
    for (auto& d : proc.descriptions()) groups.addIfNotAlreadyThere(d.group);
    groupBox.addItem("All parameters", 1);
    for (int i = 0; i < groups.size(); ++i) groupBox.addItem(groups[i], i + 2);
    groupBox.setSelectedId(1, juce::dontSendNotification);
    groupBox.onChange = [this] { rebuildRows(); };
    addAndMakeVisible(groupBox);

    searchBox.setTextToShowWhenEmpty("search", juce::Colours::grey);
    searchBox.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible(searchBox);

    voiceBox.setTextWhenNothingSelected("voice bank");
    voiceBox.onChange = [this] {
        int i = voiceBox.getSelectedId() - 1;
        if (i >= 0 && proc.patches().loadVoice(i, proc.selectedPart())) proc.requestFullRefresh();
    };
    addAndMakeVisible(voiceBox);
    perfBox.setTextWhenNothingSelected("performance");
    perfBox.onChange = [this] {
        int i = perfBox.getSelectedId() - 1;
        if (i >= 0 && proc.patches().loadPerformance(i)) proc.requestFullRefresh();
    };
    addAndMakeVisible(perfBox);

    header.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(header);
    viewport.setViewedComponent(&rows, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    rebuildRows();
    refreshHeader();
    setResizable(true, true);
    setSize(880, 620);
    startTimerHz(20);
}

Editor::~Editor() { stopTimer(); }

void Editor::rebuildRows() {
    rowList.clear();
    auto group = groupBox.getSelectedId() <= 1 ? juce::String() : groupBox.getText();
    auto needle = searchBox.getText().trim().toLowerCase();
    const auto& d = proc.descriptions();
    int y = 0;
    for (size_t i = 0; i < d.size(); ++i) {
        if (group.isNotEmpty() && d[i].group != group) continue;
        if (needle.isNotEmpty() && !d[i].name.toLowerCase().contains(needle) &&
            !d[i].group.toLowerCase().contains(needle)) continue;
        auto* r = new Row(proc, (int)i);
        rows.addAndMakeVisible(r);
        r->setBounds(0, y, juce::jmax(600, viewport.getWidth() - 16), 22);
        rowList.add(r);
        y += 22;
    }
    rows.setSize(juce::jmax(600, viewport.getWidth() - 16), juce::jmax(y, 1));

    if (proc.patches().hasRom() && voiceBox.getNumItems() == 0) {
        int n = 0;
        for (auto& e : proc.patches().voices()) {
            voiceBox.addItem(juce::String(e.index) + " " + e.bank + " " + e.name, e.index + 1);
            if (++n >= 1408) break;
        }
        for (auto& e : proc.patches().performances())
            perfBox.addItem(juce::String(e.index) + " " + e.bank + " " + e.name, e.index + 1);
    }
}

void Editor::refreshHeader() {
    auto& dv = proc.device();
    juce::String t;
    t << "\"" << juce::String(dv.performanceName()).trim() << "\"   part "
      << (proc.selectedPart() + 1) << ": \"" << juce::String(dv.voiceName(proc.selectedPart())).trim()
      << "\"  algorithm " << (dv.algorithm(proc.selectedPart()) + 1);
    if (dv.fseqFrames()) t << "   fseq \"" << juce::String(dv.fseqName()).trim() << "\" ("
                           << dv.fseqFrames() << " frames)";
    if (proc.isLearning()) t << "   -- move a control to learn";
    if (t != lastHeader) { lastHeader = t; header.setText(t, juce::dontSendNotification); }
}

void Editor::timerCallback() {
    for (auto* r : rowList) r->refresh();
    refreshHeader();
}

void Editor::openRom() {
    chooser = std::make_unique<juce::FileChooser>("FS1R v1.20 EPROM image (2 MB)", juce::File(), "*.bin");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult() == juce::File()) return;
                             proc.patches().setRomFile(fc.getResult());
                             voiceBox.clear();
                             perfBox.clear();
                             rebuildRows();
                         });
}

void Editor::loadSyx() {
    chooser = std::make_unique<juce::FileChooser>("FS1R or DX7 sysex", juce::File(), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult() == juce::File()) return;
                             proc.patches().loadFile(fc.getResult(), 0, proc.selectedPart());
                             proc.requestFullRefresh();
                         });
}

void Editor::saveSyx() {
    chooser = std::make_unique<juce::FileChooser>("Save as FS1R sysex", juce::File(), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult() != juce::File())
                                 proc.patches().saveFile(fc.getResult().withFileExtension("syx"));
                         });
}

void Editor::paint(juce::Graphics& g) { g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId)); }

void Editor::resized() {
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(28);
    for (auto& b : partButtons) b.setBounds(top.removeFromLeft(72).reduced(2));
    top.removeFromLeft(12);
    romButton.setBounds(top.removeFromLeft(90).reduced(2));
    loadButton.setBounds(top.removeFromLeft(90).reduced(2));
    saveButton.setBounds(top.removeFromLeft(90).reduced(2));
    r.removeFromTop(4);
    auto row2 = r.removeFromTop(26);
    groupBox.setBounds(row2.removeFromLeft(200).reduced(2));
    searchBox.setBounds(row2.removeFromLeft(180).reduced(2));
    voiceBox.setBounds(row2.removeFromLeft(240).reduced(2));
    perfBox.setBounds(row2.reduced(2));
    r.removeFromTop(4);
    header.setBounds(r.removeFromTop(22));
    r.removeFromTop(4);
    viewport.setBounds(r);
    int w = juce::jmax(600, viewport.getWidth() - 16);
    rows.setSize(w, rows.getHeight());
    for (auto* x : rowList) x->setSize(w, 22);
}

}  // namespace fs1rplug
