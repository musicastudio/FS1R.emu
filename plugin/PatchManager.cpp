#include "PatchManager.h"

namespace fs1rplug {

// Voice banks as the part's BANK NUMBER byte sees them: 1 = Int, 2..12 = PrA..PrK. PrA to PrI hold the
// DX7-format voices, PrJ and PrK the native ones.
static const char* kVoiceBanks[] = {"PrA", "PrB", "PrC", "PrD", "PrE", "PrF", "PrG", "PrH", "PrI", "PrJ", "PrK"};
static const char* kPerfBanks[] = {"Preset B/C", "Internal", "Preset A"};
static const char* kCategories[] = {
    "--", "Pf", "Cp", "Or", "Gt", "Ba", "St", "En", "Br", "Rd", "Pi", "Ld", "Pd", "Fx",
    "Et", "Pc", "Dr", "Se", "Vo", "Co", "Mw", "Sw", "Dx"};

static juce::String catName(int c) {
    return (c >= 0 && c < (int)(sizeof kCategories / sizeof *kCategories)) ? kCategories[c] : "--";
}

bool PatchManager::setRomFile(const juce::File& f) {
    romLoaded = f.existsAsFile() && dev.loadRom(f.getFullPathName().toRawUTF8());
    rom = romLoaded ? f : juce::File();
    buildRomLists();
    return romLoaded;
}

void PatchManager::buildRomLists() {
    voiceList.clear();
    perfList.clear();
    if (!romLoaded) return;
    // Names come out of the engine: load each patch into part 1, read the name, put the original back.
    // That is slow for 1408 voices, so the list is built from the preset index when it is there and
    // filled in lazily otherwise.
    for (int i = 0; i < 1408; ++i) {
        PatchEntry e;
        e.index = i;
        e.bank = i < 256 ? kVoiceBanks[9 + (i / 128)] : kVoiceBanks[(i - 256) / 128];
        e.name = "Voice " + juce::String(i);
        voiceList.push_back(e);
    }
    for (int i = 0; i < 360; ++i) {
        PatchEntry e;
        e.index = i;
        e.isPerformance = true;
        e.bank = i < 104 ? kPerfBanks[0] : i < 232 ? kPerfBanks[1] : kPerfBanks[2];
        e.name = "Performance " + juce::String(i);
        perfList.push_back(e);
    }
    // presets/index.csv next to the binary or in any folder above it (bin/Standalone/ inside a checkout)
    for (auto dir = juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();
         dir.exists(); dir = dir.getParentDirectory()) {
        auto csv = dir.getChildFile("presets/index.csv");
        if (csv.existsAsFile()) { setPresetIndex(csv); break; }
        if (dir.isRoot()) break;
    }
}

void PatchManager::setPresetIndex(const juce::File& csv) {
    if (!csv.existsAsFile()) return;
    juce::StringArray lines;
    csv.readLines(lines);
    for (int i = 1; i < lines.size(); ++i) {
        auto f = juce::StringArray::fromTokens(lines[i], ",", "\"");
        if (f.size() < 5) continue;
        bool native = f[0].trim() == "native";
        int idx = f[1].getIntValue();
        int flat = native ? idx : 256 + idx;       // the engine numbers the native banks first
        if (flat < 0 || flat >= (int)voiceList.size()) continue;
        voiceList[(size_t)flat].name = f[2].trim();
        voiceList[(size_t)flat].category = catName(f[3].getIntValue());
    }
}

juce::StringArray PatchManager::voiceBanks() const {
    juce::StringArray a;
    for (auto* b : kVoiceBanks) a.add(b);
    return a;
}

juce::StringArray PatchManager::categories() const {
    juce::StringArray a;
    for (auto* c : kCategories) a.add(c);
    return a;
}

bool PatchManager::loadVoice(int romIndex, int part) {
    return romLoaded && dev.loadRomVoice(juce::jlimit(0, 3, part), juce::jlimit(0, 1407, romIndex));
}

bool PatchManager::loadPerformance(int romIndex) {
    return romLoaded && dev.loadRomPerformance(juce::jlimit(0, 359, romIndex));
}

bool PatchManager::loadFile(const juce::File& f, int pick, int part) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb) || mb.getSize() < 10) return false;
    return dev.loadSyx(static_cast<const uint8_t*>(mb.getData()), mb.getSize(),
                       juce::jmax(0, pick), juce::jlimit(0, 3, part));
}

bool PatchManager::saveFile(const juce::File& f) const {
    std::vector<uint8_t> sysex;
    dev.getState(sysex);
    if (sysex.empty()) return false;
    return f.replaceWithData(sysex.data(), sysex.size());
}

}  // namespace fs1rplug
