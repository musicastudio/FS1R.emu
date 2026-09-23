#include "PatchManager.h"
#include "BinaryData.h"

namespace fs1rplug {

// Voice banks as the part's BANK NUMBER byte sees them: 0 = off, 1 = Int, 2..12 = PrA..PrK. PrA and
// PrB hold the FS1R's own 128 voices each; PrC to PrK are the nine banks of DX-series voices (owner's
// manual page 21), which is also the order the EPROM stores them in.
static const char* kVoiceBanks[] = {"PrA", "PrB", "PrC", "PrD", "PrE", "PrF", "PrG", "PrH", "PrI", "PrJ", "PrK"};
// The EPROM's performance table in its own order: the three preset banks, 128 each. INTERNAL is the
// user's own battery-backed bank, so it is not in the image and there is nothing to browse for it.
static const char* kPerfBanks[] = {"PrA", "PrB", "PrC"};
static const char* kCategories[] = {
    "--", "Pf", "Cp", "Or", "Gt", "Ba", "St", "En", "Br", "Rd", "Pi", "Ld", "Pd", "Fx",
    "Et", "Pc", "Dr", "Se", "Vo", "Co", "Mw", "Sw", "Dx"};
static constexpr int kNumCategories = (int)(sizeof kCategories / sizeof *kCategories);

static juce::String catName(int c) { return (c > 0 && c < kNumCategories) ? kCategories[c] : "--"; }

juce::StringArray PatchManager::voiceBanks() {
    juce::StringArray a{"off", "Int"};
    for (auto* b : kVoiceBanks) a.add(b);
    return a;
}

juce::StringArray PatchManager::categories() {
    juce::StringArray a;
    for (auto* c : kCategories) a.add(c);
    return a;
}

// The same mapping as bank_voice_index() in src/fs1r_lib.cpp, so a bank and program pair picks the
// same voice whether it comes from the EPROM or the bundled bank. "off" and "Int" have no voice to
// name - Int is the unit's own writable bank, which this has no store for - so they come back as -1
// and the part keeps whatever voice it is already holding.
int PatchManager::indexFor(int bank, int program) {
    program = juce::jlimit(0, 127, program);
    if (bank == 2 || bank == 3) return (bank - 2) * 128 + program;           // PrA, PrB: the native voices
    if (bank >= 4 && bank <= 12) return 256 + (bank - 4) * 128 + program;    // PrC..PrK: the DX7 voices
    return -1;
}

void PatchManager::bankProgramFor(int index, int& bank, int& program) {
    index = juce::jlimit(0, kNumVoices - 1, index);
    if (index < 256) { bank = 2 + index / 128; program = index % 128; }              // PrA, PrB
    else { bank = 4 + (index - 256) / 128; program = (index - 256) % 128; }          // PrC..PrK
}

// The short form the display prints on its top line: the bank as one letter and the program number as
// three digits (owner's manual page 22). Int is I, the preset voice banks are A to K.
juce::String PatchManager::voiceCode(int index) {
    if (index >= kNumVoices)
        return "U" + juce::String(index - kNumVoices + 1).paddedLeft('0', 3);
    int bank = 0, program = 0;
    bankProgramFor(index, bank, program);
    const juce::juce_wchar letter = bank == 1 ? 'I' : (juce::juce_wchar)('A' + bank - 2);
    return juce::String::charToString(letter) + juce::String(program + 1).paddedLeft('0', 3);
}

juce::String PatchManager::performanceCode(int index) {
    index = juce::jlimit(0, kNumPerformances - 1, index);
    return juce::String::charToString((juce::juce_wchar)('A' + index / 128)) +
           juce::String(index % 128 + 1).paddedLeft('0', 3);
}

// Walks a run of FS1R bulk dumps, handing each one's data to fn. The ends come from the F0 and F7
// bytes rather than from the byte count, which an Fseq bulk cannot express (Data List 3.2.1).
template <typename Fn>
static void forEachDump(const uint8_t* d, size_t len, Fn fn) {
    for (size_t i = 0; i + 11 < len; ) {
        if (d[i] != 0xF0) { ++i; continue; }
        size_t j = i + 1;
        while (j < len && d[j] != 0xF7) ++j;
        if (j >= len) return;
        if (j >= i + 11) fn(d + i + 9, j - 1 - (i + 9));    // the data between the address and the checksum
        i = j + 1;
    }
}

void PatchManager::buildLists() {
    voiceList.clear();
    perfList.clear();
    // The voice list is the factory set, out of the bundled .syx bank, in the EPROM's own order.
    for (int i = 0; i < kNumVoices; ++i) {
        PatchEntry e;
        e.index = i;
        e.bank = kVoiceBanks[i < 256 ? i / 128 : 2 + (i - 256) / 128];
        e.code = voiceCode(i);
        e.name = "Voice " + juce::String(i);
        voiceList.push_back(e);
    }
    juce::StringArray lines;
    lines.addLines(juce::String::createStringFromData(BinaryData::fs1r_presets_csv,
                                                      BinaryData::fs1r_presets_csvSize));
    for (int i = 1; i < lines.size(); ++i) {
        auto f = juce::StringArray::fromTokens(lines[i], ",", "");
        if (f.size() < 3) continue;
        const int flat = f[0].getIntValue();
        if (flat < 0 || flat >= kNumVoices) continue;
        voiceList[(size_t)flat].name = f[1].trim();
        voiceList[(size_t)flat].category = catName(f[2].getIntValue());
    }
    // Performances and Fseqs carry their own names, so the bundled streams are the index as well as
    // the data. The EPROM holds the same ones in the same order.
    perfData.clear();
    forEachDump((const uint8_t*)BinaryData::fs1r_performances_syx,
                (size_t)BinaryData::fs1r_performances_syxSize, [this](const uint8_t* d, size_t n) {
        if (n < 400) return;
        PatchEntry e;
        e.index = (int)perfList.size();
        e.isPerformance = true;
        e.bank = kPerfBanks[juce::jlimit(0, 2, e.index / 128)];
        e.code = performanceCode(e.index);
        e.name = juce::String::fromUTF8((const char*)d, 12).trimEnd();
        e.category = catName(d[0x0E]);
        perfList.push_back(e);
        perfData.push_back(d);
    });
    forEachDump((const uint8_t*)BinaryData::fs1r_fseqs_syx, (size_t)BinaryData::fs1r_fseqs_syxSize,
                [this](const uint8_t* d, size_t n) {
        if (n < 32) return;
        PatchEntry e;
        e.index = (int)fseqList.size();
        e.name = juce::String::fromUTF8((const char*)d, 8).trimEnd();
        e.bank = "Pre";
        e.code = juce::String(e.index + 1).paddedLeft('0', 2);   // the Fseq banks have no letter
        e.category = juce::String(128 * ((d[0x1B] & 3) + 1)) + " frames";
        fseqList.push_back(e);
    });
    jassert(perfList.size() == kNumPerformances && fseqList.size() == kNumFseqs);
}

// Loading anything silences what is sounding first. A voice or a performance arriving under held
// notes leaves the engine's channels running on a patch that no longer exists, which is a stuck note
// on the hardware too; the effect tails are left alone, as they are there.
bool PatchManager::loadVoice(int index, int part) {
    if (index < 0) return false;
    dev.allNotesOff();
    part = juce::jlimit(0, 3, part);
    if (index >= kNumVoices) {                    // an imported file's n-th voice
        return !imported.empty() &&
               dev.loadSyx(imported.data(), imported.size(), index - kNumVoices, part);
    }
    index = juce::jlimit(0, kNumVoices - 1, index);
    return dev.loadSyx((const uint8_t*)BinaryData::fs1r_presets_syx,
                       (size_t)BinaryData::fs1r_presets_syxSize, index, part);
}

bool PatchManager::loadPerformance(int index) {
    dev.allNotesOff();
    index = juce::jlimit(0, kNumPerformances - 1, index);
    if (index >= (int)perfData.size() ||
        !dev.loadSyx((const uint8_t*)BinaryData::fs1r_performances_syx,
                     (size_t)BinaryData::fs1r_performances_syxSize, index, 0))
        return false;
    // A performance holds nothing but a bank and program number per part and an Fseq number, so those
    // are followed here out of the bundled banks, which is what the hardware does from its own ROM.
    const uint8_t* d = perfData[(size_t)index];
    for (int part = 0; part < 4; ++part) {
        const uint8_t* p = d + 192 + 52 * part;          // the part's own 52 bytes
        loadVoice(indexFor(p[1], p[2]), part);           // bank "off" and "Int" name nothing, and load nothing
    }
    // Common bytes 0x15, 0x16 and 0x17: which part plays the Fseq, which bank it is in, and which one.
    // "int" is the unit's own Fseq store, which this has nothing for.
    if ((d[0x15] & 7) != 0 && (d[0x16] & 1) != 0) loadFseq(d[0x17]);
    return true;
}

bool PatchManager::loadFseq(int index) {
    dev.allNotesOff();
    index = juce::jlimit(0, kNumFseqs - 1, index);
    return dev.loadSyx((const uint8_t*)BinaryData::fs1r_fseqs_syx,
                       (size_t)BinaryData::fs1r_fseqs_syxSize, index, 0);
}

// Counts the voice dumps in a .syx the same way the engine's loader picks them, so the n-th entry the
// browser offers is the n-th voice loadSyx will find.
static int countVoiceDumps(const uint8_t* d, size_t len) {
    int n = 0;
    for (size_t i = 0; i + 10 < len; ++i) {
        if (d[i] != 0xF0 || d[i + 1] != 0x43 || (d[i + 2] & 0xF0) != 0x00) continue;
        if (d[i + 3] == 0x5E) {
            const int bc = d[i + 4] << 7 | d[i + 5], ah = d[i + 6];
            if (bc == 608 && ((ah >= 0x40 && ah <= 0x43) || ah == 0x51)) ++n;
        } else if (d[i + 3] == 0x00 && d[i + 4] == 0x01 && d[i + 5] == 0x1B) {
            ++n;
        }
    }
    return n;
}

bool PatchManager::importFile(const juce::File& f, int part) {
    dev.allNotesOff();
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb) || mb.getSize() < 10) return false;
    const auto* data = static_cast<const uint8_t*>(mb.getData());
    const int found = countVoiceDumps(data, mb.getSize());

    // Drop whatever the last import left in the list, then add this file's voices as the Usr bank.
    voiceList.resize((size_t)kNumVoices);
    imported.assign(data, data + mb.getSize());
    const auto stem = f.getFileNameWithoutExtension();
    for (int i = 0; i < found; ++i) {
        PatchEntry e;
        e.index = kNumVoices + i;
        e.bank = "Usr";
        e.code = voiceCode(e.index);
        e.category = "--";
        e.name = found > 1 ? stem + " " + juce::String(i + 1) : stem;
        voiceList.push_back(e);
    }
    // No voices in it: it is a performance or an Fseq dump, which the engine takes as it is.
    if (found == 0) {
        imported.clear();
        return dev.loadSyx(data, mb.getSize(), 0, juce::jlimit(0, 3, part));
    }
    return loadVoice(kNumVoices, part);
}

bool PatchManager::saveFile(const juce::File& f) const {
    std::vector<uint8_t> sysex;
    dev.getState(sysex);
    if (sysex.empty()) return false;
    return f.replaceWithData(sysex.data(), sysex.size());
}

}  // namespace fs1rplug
