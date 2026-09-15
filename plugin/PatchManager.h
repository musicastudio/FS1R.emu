// PatchManager - where patches come from: .syx files, the 1408 voices and 360 performances in the
// EPROM image, and DX7 VCED/ACED dumps. Everything reaches the engine as sysex, so nothing here knows
// how the engine stores a voice.
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "fs1r_lib.h"

namespace fs1rplug {

struct PatchEntry {
    juce::String name;
    juce::String bank;
    juce::String category;
    int index = 0;             // ROM index, or the n-th dump inside a file
    bool isPerformance = false;
};

class PatchManager {
public:
    explicit PatchManager(fs1r::Device& d) : dev(d) {}

    // The EPROM image. Without it the ROM banks are empty and everything else still works.
    bool setRomFile(const juce::File& f);
    bool hasRom() const { return romLoaded; }
    juce::File romFile() const { return rom; }

    const std::vector<PatchEntry>& voices() const { return voiceList; }
    const std::vector<PatchEntry>& performances() const { return perfList; }
    juce::StringArray voiceBanks() const;
    juce::StringArray categories() const;

    bool loadVoice(int romIndex, int part);
    bool loadPerformance(int romIndex);

    // .syx and DX7 files. loadFile picks the n-th dump in the file (voice, performance, Fseq or VCED).
    bool loadFile(const juce::File& f, int pick, int part);
    // Writes the current performance and its four voices as one .syx, the same bytes getState produces.
    bool saveFile(const juce::File& f) const;

    // The index.csv shipped in presets/ names and categorises the ROM voices; used when it is present.
    void setPresetIndex(const juce::File& csv);

private:
    void buildRomLists();

    fs1r::Device& dev;
    juce::File rom;
    bool romLoaded = false;
    std::vector<PatchEntry> voiceList, perfList;
};

}  // namespace fs1rplug
