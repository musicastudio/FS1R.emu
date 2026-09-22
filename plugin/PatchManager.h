// PatchManager - where patches come from: the 1408 factory voices bundled with the binary, the same
// voices read out of an EPROM image when one is loaded, .syx files the user imports, and DX7
// VCED/ACED dumps. Everything reaches the engine as sysex, so nothing here knows how the engine
// stores a voice.
//
// Voices are numbered the way the engine numbers them: 0-255 are the native banks PrA and PrB,
// 256-1407 the DX7-format banks PrC..PrK. A bank and program number pair - which is how the hardware
// selects a voice (owner's manual page 25) - maps onto that numbering with indexFor().
//
// Performances and Fseqs are bundled the same way: the 384 preset performances in the EPROM's own
// order (Preset A, B then C) and the 90 preset formant sequences. A performance names the Fseq it
// wants, so loading one loads that too, which is what the hardware does from its own ROM.
//
// Every entry also carries the short form the hardware's display uses for it - a bank letter and a
// three digit program number, A001, B009, J128 - so a patch is named here the way it is named there.
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "fs1r.h"

namespace fs1rplug {

struct PatchEntry {
    juce::String name;
    juce::String bank;               // as the hardware's bank field prints it: Int, PrA..PrK, Usr
    juce::String code;               // and as its top line does: A001, B009, J128
    juce::String category;
    int index = 0;             // voice number, or 1408 + n for the n-th voice of an imported file
    bool isPerformance = false;
};

class PatchManager {
public:
    static constexpr int kNumVoices = 1408;
    static constexpr int kNumPerformances = 384;
    static constexpr int kNumFseqs = 90;

    explicit PatchManager(fs1r::Device& d) : dev(d) { buildLists(); }

    // The EPROM image. Nothing needs it any more: the voices, performances and Fseqs it holds are all
    // bundled. Loading one only changes where they are read from.
    bool setRomFile(const juce::File& f);
    bool hasRom() const { return romLoaded; }
    juce::File romFile() const { return rom; }

    const std::vector<PatchEntry>& voices() const { return voiceList; }
    const std::vector<PatchEntry>& performances() const { return perfList; }
    const std::vector<PatchEntry>& fseqs() const { return fseqList; }
    static juce::StringArray voiceBanks();       // off, Int, PrA..PrK, as the part's BANK NUMBER sees them
    static juce::StringArray categories();

    // The hardware's own voice selection: bank 0 = off, 1 = Int, 2..12 = PrA..PrK, program 0..127.
    static int indexFor(int bank, int program);
    static void bankProgramFor(int index, int& bank, int& program);
    static juce::String voiceCode(int index);          // J001 for the first native voice, U001 for an import
    static juce::String performanceCode(int index);    // A001 .. C128

    bool loadVoice(int index, int part);         // the EPROM when there is one, the bundled bank otherwise
    bool loadPerformance(int index);             // and the Fseq that performance asks for
    bool loadFseq(int index);                    // preset Fseq 0-89

    // Imports every voice in a .syx file as a browsable "Usr" bank, and loads the first of them.
    // Performance and Fseq dumps in the file go straight to the engine.
    bool importFile(const juce::File& f, int part);
    bool hasImport() const { return !imported.empty(); }
    // Writes the current performance and its four voices as one .syx, the same bytes getState produces.
    bool saveFile(const juce::File& f) const;

private:
    void buildLists();

    fs1r::Device& dev;
    juce::File rom;
    bool romLoaded = false;
    std::vector<PatchEntry> voiceList, perfList, fseqList;
    // Where each bundled performance's 400 data bytes start, so loading one can read the voices and
    // the Fseq it asks for straight out of them.
    std::vector<const uint8_t*> perfData;
    std::vector<uint8_t> imported;               // the last imported .syx, kept so its voices stay loadable
};

}  // namespace fs1rplug
