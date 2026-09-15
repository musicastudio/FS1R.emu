#include "PluginEditor.h"

namespace fs1rplug {

static bool startsWith(const juce::String& s, const char* p) { return s.startsWith(p); }

Editor::Editor(Processor& p) : juce::AudioProcessorEditor(&p), proc(p), panel(p) {
    addAndMakeVisible(panel);
    panel.onModeButton = [this](const juce::String& page) {
        if (page == "__search") { searchBox.grabKeyboardFocus(); return; }
        showPage(page);
    };

    romButton.setButtonText("EPROM...");
    loadButton.setButtonText("Load .syx");
    saveButton.setButtonText("Save .syx");
    romButton.onClick = [this] { openRom(); };
    loadButton.onClick = [this] { loadSyx(); };
    saveButton.onClick = [this] { saveSyx(); };
    addAndMakeVisible(romButton);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(saveButton);

    searchBox.setTextToShowWhenEmpty("search this page", juce::Colours::grey);
    searchBox.onTextChange = [this] {
        for (auto* pg : pages) pg->setSearch(searchBox.getText());
    };
    addAndMakeVisible(searchBox);

    voiceBox.setTextWhenNothingSelected("voice");
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

    algView = std::make_unique<AlgorithmView>(proc);
    spectrumView = std::make_unique<SpectrumView>(proc);
    ampEg = std::make_unique<EnvelopeView>(proc, EnvelopeView::Amplitude);
    pitchEg = std::make_unique<EnvelopeView>(proc, EnvelopeView::Pitch);
    filterEg = std::make_unique<EnvelopeView>(proc, EnvelopeView::Filter);
    fseqView = std::make_unique<FseqView>(proc);

    // The pages follow the hardware's edit structure rather than the sysex layout.
    addPage("Operators", new ParameterPage(proc, [](const juce::String& g, const juce::String&) {
        return startsWith(g, "Op ") && !g.endsWith("Unvoiced");
    }, ampEg.get(), 130, 4));
    addPage("Unvoiced", new ParameterPage(proc, [](const juce::String& g, const juce::String&) {
        return g.endsWith("Unvoiced");
    }, spectrumView.get(), 130, 4));
    addPage("Algorithm", new ParameterPage(proc, [](const juce::String& g, const juce::String& n) {
        return g == "Voice" && (n.contains("Algorithm") || n.contains("carrier level") ||
                                n.contains("feedback"));
    }, algView.get(), 150, 2));
    addPage("Formant", new ParameterPage(proc, [](const juce::String& g, const juce::String& n) {
        return (startsWith(g, "Op ") && (n.contains("Spectral") || n.contains("band spectrum") ||
                                         n.contains("Bandwidth") || n.contains("Formant") ||
                                         n.contains("formant"))) ||
               (g == "Voice" && n.contains("Formant Control"));
    }, spectrumView.get(), 130, 3));
    addPage("LFO / PEG", new ParameterPage(proc, [](const juce::String& g, const juce::String& n) {
        return g == "Voice" && (n.contains("LFO") || n.contains("Pitch EG") || n.contains("Note shift"));
    }, pitchEg.get(), 130, 2));
    addPage("Filter", new ParameterPage(proc, [](const juce::String& g, const juce::String& n) {
        return (g == "Voice" && n.contains("Filter")) || (g == "Part" && n.contains("FILTER"));
    }, filterEg.get(), 130, 2));
    addPage("Performance", new ParameterPage(proc, [](const juce::String& g, const juce::String& n) {
        return g == "Performance" && !n.startsWith("FSEQ") && !n.startsWith("Fseq");
    }, nullptr, 0, 3));
    addPage("Part", new ParameterPage(proc, [](const juce::String& g, const juce::String&) {
        return g == "Part";
    }, nullptr, 0, 3));
    addPage("Effects", new ParameterPage(proc, [](const juce::String& g, const juce::String&) {
        return g == "Effects";
    }, nullptr, 0, 3));
    addPage("Fseq", new ParameterPage(proc, [](const juce::String& g, const juce::String& n) {
        return (g == "Performance" && (n.startsWith("FSEQ") || n.startsWith("Fseq"))) ||
               (startsWith(g, "Op ") && n.contains("Fseq")) || (g == "Voice" && n.contains("Fseq"));
    }, fseqView.get(), 150, 2));
    addPage("System", new ParameterPage(proc, [](const juce::String& g, const juce::String&) {
        return g == "System";
    }, nullptr, 0, 3));
    addPage("All parameters", new ParameterPage(proc, [](const juce::String&, const juce::String&) {
        return true;
    }, nullptr, 0, 3));

    for (auto* pg : pages)
        pg->onParameterTouched = [this](const juce::String& n, const juce::String& v) {
            panel.showParameterOnLcd(n, v);
        };
    addAndMakeVisible(tabs);

    setResizable(true, true);
    setResizeLimits(900, 560, 2400, 1600);
    setSize(1060, 720);
    startTimerHz(10);
}

Editor::~Editor() { stopTimer(); }

void Editor::addPage(const juce::String& name, ParameterPage* page) {
    pages.add(page);
    pageNames.add(name);
    tabs.addTab(name, juce::Colour(0xff26292c), page, false);
}

void Editor::showPage(const juce::String& name) {
    int i = pageNames.indexOf(name);
    if (i >= 0) tabs.setCurrentTabIndex(i);
}

void Editor::timerCallback() {
    int part = proc.selectedPart();
    int alg = proc.device().algorithm(part);
    if (part != lastPart || alg != lastAlg) {
        lastPart = part;
        lastAlg = alg;
        algView->setAlgorithm(alg);
    }
    if (proc.patches().hasRom() && voiceBox.getNumItems() == 0) {
        for (auto& e : proc.patches().voices())
            voiceBox.addItem(juce::String(e.index) + " " + e.bank + " " + e.name +
                                 (e.category.isEmpty() ? "" : " (" + e.category + ")"),
                             e.index + 1);
        for (auto& e : proc.patches().performances())
            perfBox.addItem(juce::String(e.index) + " " + e.bank + " " + e.name, e.index + 1);
    }
}

void Editor::openRom() {
    chooser = std::make_unique<juce::FileChooser>("FS1R v1.20 EPROM image (2 MB)", juce::File(), "*.bin");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult() == juce::File()) return;
                             proc.patches().setRomFile(fc.getResult());
                             voiceBox.clear();
                             perfBox.clear();
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

void Editor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff1b1e21)); }

void Editor::resized() {
    auto r = getLocalBounds().reduced(8);
    panel.setBounds(r.removeFromTop(150));
    r.removeFromTop(6);
    auto bar = r.removeFromTop(26);
    romButton.setBounds(bar.removeFromLeft(90).reduced(2));
    loadButton.setBounds(bar.removeFromLeft(90).reduced(2));
    saveButton.setBounds(bar.removeFromLeft(90).reduced(2));
    searchBox.setBounds(bar.removeFromRight(190).reduced(2));
    voiceBox.setBounds(bar.removeFromLeft(juce::jmax(180, bar.getWidth() / 2)).reduced(2));
    perfBox.setBounds(bar.reduced(2));
    r.removeFromTop(4);
    tabs.setBounds(r);
}

}  // namespace fs1rplug
