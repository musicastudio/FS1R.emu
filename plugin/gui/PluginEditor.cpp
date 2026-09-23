#include "PluginEditor.h"
#include <cmath>

namespace fs1rplug {

static bool startsWith(const juce::String& s, const char* p) { return s.startsWith(p); }

Editor::Editor(Processor& p) : juce::AudioProcessorEditor(&p), proc(p), panel(p) {
    addAndMakeVisible(panel);
    panel.onModeButton = [this](const juce::String& page) {
        // [SEARCH] is the hardware's patch finder, so it lands on the browser's category box.
        if (page == "__search") { catBox.grabKeyboardFocus(); return; }
        showPage(page);
    };

    loadButton.setButtonText("Import .syx");
    saveButton.setButtonText("Save .syx");
    loadButton.onClick = [this] { importSyx(); };
    saveButton.onClick = [this] { saveSyx(); };
    addAndMakeVisible(loadButton);
    addAndMakeVisible(saveButton);

    searchBox.setTextToShowWhenEmpty("search this page", juce::Colours::grey);
    searchBox.onTextChange = [this] {
        for (auto* pg : pages) pg->setSearch(searchBox.getText());
    };
    addAndMakeVisible(searchBox);

    catBox.addItem("all categories", 1);
    for (const auto& c : PatchManager::categories()) catBox.addItem(c, catBox.getNumItems() + 1);
    catBox.setSelectedId(1, juce::dontSendNotification);
    catBox.onChange = [this] { refillVoices(); };
    addAndMakeVisible(catBox);

    bankBox.onChange = [this] { refillVoices(); };
    addAndMakeVisible(bankBox);

    voiceBox.setTextWhenNothingSelected("voice");
    voiceBox.onChange = [this] {
        const int i = voiceBox.getSelectedId() - 1;
        if (i >= 0) { shownVoice = i; proc.selectVoice(i); }
    };
    addAndMakeVisible(voiceBox);

    perfBox.setTextWhenNothingSelected("performance");
    for (auto& e : proc.patches().performances())
        perfBox.addItem(e.code + "  " + e.name + (e.category == "--" ? "" : "  " + e.category),
                        e.index + 1);
    perfBox.onChange = [this] {
        const int i = perfBox.getSelectedId() - 1;
        if (i >= 0) { shownPerf = i; proc.selectPerformance(i); }
    };
    addAndMakeVisible(perfBox);
    refillVoices();

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

// The bank list is whatever the voices actually come from, so "Usr" only appears once a .syx has been
// imported. The voice list is then every voice that matches the bank and the category.
void Editor::refillVoices() {
    const auto& all = proc.patches().voices();
    if (bankBox.getNumItems() == 0 || proc.patches().hasImport() != sawImport) {
        sawImport = proc.patches().hasImport();
        juce::StringArray banks;
        for (auto& e : all) banks.addIfNotAlreadyThere(e.bank);
        bankBox.clear(juce::dontSendNotification);
        bankBox.addItem("all banks", 1);
        for (const auto& b : banks) bankBox.addItem(b, bankBox.getNumItems() + 1);
        bankBox.setSelectedId(1, juce::dontSendNotification);
    }

    const auto bank = bankBox.getSelectedId() > 1 ? bankBox.getText() : juce::String();
    const auto cat = catBox.getSelectedId() > 1 ? catBox.getText() : juce::String();
    voiceBox.clear(juce::dontSendNotification);
    for (auto& e : all) {
        if (bank.isNotEmpty() && e.bank != bank) continue;
        if (cat.isNotEmpty() && e.category != cat) continue;
        voiceBox.addItem(e.code + "  " + e.name + (e.category == "--" ? "" : "  " + e.category),
                         e.index + 1);
    }
    // "If a corresponding performance setup or voice is not found within the specified part, category,
    // and/or bank, 'Not Found!' will appear" - owner's manual page 27.
    voiceBox.setTextWhenNothingSelected(voiceBox.getNumItems() == 0 ? "Not Found!" : "voice");
    voiceBox.setSelectedId(shownVoice + 1, juce::dontSendNotification);
}

void Editor::timerCallback() {
    int part = proc.selectedPart();
    int alg = proc.device().algorithm(part);
    if (part != lastPart || alg != lastAlg) {
        lastPart = part;
        lastAlg = alg;
        algView->setAlgorithm(alg);
    }
    // Both browsers follow the engine, whoever moved it: the panel's VALUE buttons step the
    // performance on the play screen and the part's voice on the part screen, and either has to show up
    // here.
    const int voice = proc.currentVoice();
    if (voice != shownVoice) {
        shownVoice = voice;
        voiceBox.setSelectedId(voice + 1, juce::dontSendNotification);
    }
    const int perf = proc.currentPerformance();
    if (perf != shownPerf) {
        shownPerf = perf;
        perfBox.setSelectedId(perf + 1, juce::dontSendNotification);
    }
}

// Every voice in the file joins the browser as the "Usr" bank, so an imported .syx is browsed the same
// way the factory ones are. A performance or Fseq dump has no voices in it and goes straight in.
void Editor::importSyx() {
    chooser = std::make_unique<juce::FileChooser>("FS1R or DX7 sysex", juce::File(), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult() == juce::File()) return;
                             proc.importSyx(fc.getResult());
                             refillVoices();
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
    // The panel is the manual's drawing, so it gets exactly the height its own proportions ask for.
    panel.setBounds(r.removeFromTop((int)std::lround(r.getWidth() / PanelView::kAspect)));
    r.removeFromTop(6);
    auto bar = r.removeFromTop(26);
    loadButton.setBounds(bar.removeFromLeft(90).reduced(2));
    saveButton.setBounds(bar.removeFromLeft(80).reduced(2));
    searchBox.setBounds(bar.removeFromRight(150).reduced(2));
    // The performance comes first and the voice browser after it, the way the hardware is used: a
    // performance names the four voices, so it is the wider choice and the one made first.
    perfBox.setBounds(bar.removeFromLeft(juce::jmax(180, bar.getWidth() * 2 / 5)).reduced(2));
    catBox.setBounds(bar.removeFromLeft(90).reduced(2));
    bankBox.setBounds(bar.removeFromLeft(80).reduced(2));
    voiceBox.setBounds(bar.reduced(2));
    r.removeFromTop(4);
    tabs.setBounds(r);
}

}  // namespace fs1rplug
