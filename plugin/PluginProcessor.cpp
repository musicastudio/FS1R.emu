#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace fs1rplug {

// A parameter that carries its description index, so the listener knows what to send without a map.
class SysexParameter : public juce::AudioParameterInt {
public:
    SysexParameter(const ParamDesc& d, int idx)
        : juce::AudioParameterInt(juce::ParameterID{d.uniqueId(), 1}, d.group + " " + d.name,
                                  d.min, d.max, d.def,
                                  juce::AudioParameterIntAttributes()
                                      .withStringFromValueFunction([&d](int v, int) { return d.textFor(v); })),
          desc(d), index(idx) {}
    const ParamDesc& desc;
    const int index;
};

static int addrKey(int hi, int mid, int lo) { return (hi << 16) | (mid << 8) | lo; }

// Holds off the "send this to the engine" path while we adopt values that came from it. A counter
// rather than a flag: the audio thread adopts echoes while the message thread is adopting a bulk dump,
// and a plain bool let whichever finished first reopen the path under the other one.
struct Quiet {
    std::atomic<int>& depth;
    explicit Quiet(std::atomic<int>& d) : depth(d) { ++depth; }
    ~Quiet() { --depth; }
};

Processor::Processor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    if (!loadParameterDescriptions(BinaryData::parameterDescriptions_fs1r_json,
                                   BinaryData::parameterDescriptions_fs1r_jsonSize, descs))
        jassertfalse;                                  // the generated JSON did not parse
    buildParameters();
    bankParam = findParameter("Part", "BANK NUMBER off, Int, PrA~PrK");
    progParam = findParameter("Part", "PROGRAM NUMBER");
    fseqPartParam = findParameter("Performance", "FSEQ PART");
    fseqBankParam = findParameter("Performance", "FSEQ bank");
    fseqNumParam = findParameter("Performance", "FSEQ number int");
    shadow.assign(0x800, 0);
    ccForParam.assign(descs.size(), -1);
    startTimerHz(30);
}

Processor::~Processor() {
    stopTimer();
    for (auto* p : params) p->removeListener(this);
}

void Processor::buildParameters() {
    params.reserve(descs.size());
    for (size_t i = 0; i < descs.size(); ++i) {
        auto* p = new SysexParameter(descs[i], (int)i);
        addParameter(p);
        p->addListener(this);
        params.push_back(p);
        // Indexed by the address as declared; applyIncomingParameter runs on the audio thread and has
        // no business walking all 893 of them per echo.
        byAddress[addrKey(descs[i].addr[0], descs[i].addr[1], descs[i].addr[2])].push_back((int)i);
    }
    dirty = std::vector<std::atomic<bool>>(descs.size());
}

int Processor::findParameter(const char* group, const char* name) const {
    for (size_t i = 0; i < descs.size(); ++i)
        if (descs[i].group == group && descs[i].name == name) return (int)i;
    jassertfalse;                                      // the generated JSON lost a parameter we need
    return -1;
}

int Processor::parameterValue(int index) const {
    if (index < 0 || index >= (int)params.size()) return 0;
    auto* q = params[(size_t)index];
    return (int)std::lround(q->convertFrom0to1(q->getValue()));
}

int Processor::currentVoice() const {
    const int preset = PatchManager::indexFor(parameterValue(bankParam), parameterValue(progParam));
    return preset >= 0 ? preset : importedVoice.load();
}

void Processor::selectVoice(int voiceIndex) {
    // An imported .syx voice has no bank or program of its own, so it goes straight in; the part keeps
    // whatever bank and program it had, which is what the hardware does with an edited voice.
    if (voiceIndex >= PatchManager::kNumVoices) {
        if (!patchManager.loadVoice(voiceIndex, part.load())) return;
        importedVoice = voiceIndex;
        needRefresh = true;
        return;
    }
    int bank = 0, program = 0;
    PatchManager::bankProgramFor(voiceIndex, bank, program);
    const int which[2] = {bankParam, progParam}, to[2] = {bank, program};
    for (int k = 0; k < 2; ++k) {
        if (which[k] < 0) continue;
        auto* q = params[(size_t)which[k]];
        q->setValueNotifyingHost(q->convertTo0to1((float)to[k]));
    }
}

// The engine only loads a voice from a bank and program number when it has the EPROM image and a
// program change arrives, so the plugin does it here instead: the bundled bank covers the same 1408
// voices, and this keeps one path for the panel, the browser and host automation alike.
void Processor::loadSelectedVoice() {
    // "off" and "Int" name no preset voice, so the part keeps the one it is holding.
    const int preset = PatchManager::indexFor(parameterValue(bankParam), parameterValue(progParam));
    if (!patchManager.loadVoice(preset, part.load())) return;
    importedVoice = -1;
    needRefresh = true;
}

// The engine loads the Fseq a performance names out of the EPROM image; with the preset sequences
// bundled it no longer needs one, so the same three bytes are watched here.
void Processor::loadSelectedFseq() {
    if (parameterValue(fseqPartParam) == 0) return;    // no part plays it
    if (parameterValue(fseqBankParam) == 0) return;    // "int": the unit's own Fseq store, which we have none of
    if (patchManager.loadFseq(parameterValue(fseqNumParam))) needRefresh = true;
}

void Processor::selectPerformance(int index) {
    if (!patchManager.loadPerformance(index)) return;
    perfIndex = index;
    needRefresh = true;
}

bool Processor::importSyx(const juce::File& f) {
    const bool ok = patchManager.importFile(f, part.load());
    perfIndex = -1;                                    // whatever the file left, it is not a preset one
    importedVoice = patchManager.hasImport() ? PatchManager::kNumVoices : -1;
    needRefresh = true;
    return ok;
}

void Processor::prepareToPlay(double sampleRate, int) {
    dev.setSampleRate(sampleRate);
    dev.setEchoParameters(true);
    needRefresh = true;
}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    for (const auto meta : midi) {
        auto m = meta.getMessage();
        if (m.isSysEx()) {
            // A sysex message reaching the plugin is a patch or a parameter change from an editor.
            std::vector<uint8_t> bytes(m.getRawData(), m.getRawData() + m.getRawDataSize());
            dev.sendMidi(bytes.data(), bytes.size());
            needRefresh = true;
            continue;
        }
        if (m.isController()) {
            if (learnTarget >= 0) {
                ccForParam[(size_t)learnTarget] = m.getControllerNumber();
                learnTarget = -1;
            }
            int cc = m.getControllerNumber();
            for (size_t i = 0; i < ccForParam.size(); ++i)
                if (ccForParam[i] == cc) {
                    auto& d = descs[i];
                    int v = d.min + (int)std::lround(m.getControllerValue() / 127.0 * (d.max - d.min));
                    params[i]->beginChangeGesture();
                    params[i]->setValueNotifyingHost(params[i]->convertTo0to1((float)v));
                    params[i]->endChangeGesture();
                }
            // The FS1R's own controller sets are exposed as they are, so the CC still reaches the engine.
        }
        const auto* raw = m.getRawData();
        dev.sendMidi(raw, (size_t)m.getRawDataSize());
    }
    midi.clear();

    buffer.clear();
    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    dev.process(l, r, n);

    // The engine's MIDI output is what tells the editor and the host what actually changed.
    std::vector<uint8_t> out;
    while (dev.nextMidiOut(out)) {
        if (out.size() >= 10 && out[0] == 0xF0 && out[3] == 0x5E && (out[2] & 0xF0) == 0x10)
            applyIncomingParameter(out[4], out[5], out[6], out[7] << 7 | out[8]);
        midi.addEvent(juce::MidiMessage::createSysExMessage(out.data() + 1, (int)out.size() - 2), 0);
    }
}

void Processor::parameterValueChanged(int index, float) {
    if (suppressSend.load() != 0) return;
    if (index >= 0 && index < (int)dirty.size()) dirty[(size_t)index] = true;
}

void Processor::timerCallback() {
    if (needRefresh.exchange(false)) refreshFromEngine();
    bool voiceMoved = false, fseqMoved = false;
    for (size_t i = 0; i < dirty.size(); ++i)
        if (dirty[i].exchange(false)) {
            sendParameter((int)i, (int)((juce::AudioParameterInt*)params[i])->get());
            voiceMoved |= (int)i == bankParam || (int)i == progParam;
            fseqMoved |= (int)i == fseqPartParam || (int)i == fseqBankParam || (int)i == fseqNumParam;
        }
    if (voiceMoved) loadSelectedVoice();
    if (fseqMoved) loadSelectedFseq();
}

void Processor::sendParameter(int index, int value) {
    const auto& d = descs[(size_t)index];
    int hi = d.addr[0] + (d.partRelative ? part.load() : 0);
    int mid = d.addr[1], lo = d.addr[2];
    int v = value;
    if (d.width > 0) {                                  // packed field: merge into the byte we last saw
        int mask = ((1 << d.width) - 1) << d.shift;
        int key = addrKey(hi, mid, lo) & 0x7FF;
        v = (shadow[(size_t)key] & ~mask) | ((value << d.shift) & mask);
        shadow[(size_t)key] = (uint8_t)v;
    }
    uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)hi, (uint8_t)mid, (uint8_t)lo,
                     (uint8_t)(v >> 7 & 0x7F), (uint8_t)(v & 0x7F), 0xF7};
    dev.sendMidi(m, 10);
}

void Processor::applyIncomingParameter(int hi, int mid, int lo, int value) {
    int key = addrKey(hi, mid, lo) & 0x7FF;
    shadow[(size_t)key] = (uint8_t)(value & 0x7F);
    const int p = part.load();
    Quiet guard(suppressSend);
    // Pass 0 is the parameters that address this byte outright, pass 1 the part-relative ones, whose
    // declared address is this one less the selected part.
    for (int pass = 0; pass < 2; ++pass) {
        auto it = byAddress.find(addrKey(pass == 0 ? hi : hi - p, mid, lo));
        if (it == byAddress.end()) continue;
        for (int i : it->second) {
            const auto& d = descs[(size_t)i];
            if (d.partRelative != (pass == 1)) continue;
            int v = d.width > 0 ? (value >> d.shift) & ((1 << d.width) - 1) : value;
            v = juce::jlimit(d.min, d.max, v);
            params[(size_t)i]->setValueNotifyingHost(params[(size_t)i]->convertTo0to1((float)v));
        }
    }
}

void Processor::refreshFromEngine() {
    // Ask the engine for everything and adopt it. The reply arrives as bulk dumps, so walk them and set
    // the parameters straight from the bytes rather than sending anything back.
    std::vector<uint8_t> sysex;
    dev.getState(sysex);
    const int p = part.load();
    Quiet guard(suppressSend);
    for (size_t i = 0; i + 10 < sysex.size();) {
        if (sysex[i] != 0xF0) { ++i; continue; }
        int count = sysex[i + 4] << 7 | sysex[i + 5];
        int ah = sysex[i + 6], am = sysex[i + 7], al = sysex[i + 8];
        const uint8_t* data = sysex.data() + i + 9;
        if (i + 9 + (size_t)count + 2 > sysex.size()) break;
        for (int k = 0; k < count; ++k) {
            int key = addrKey(ah, am, al + k) & 0x7FF;
            shadow[(size_t)key] = data[k];
        }
        for (size_t d = 0; d < descs.size(); ++d) {
            const auto& q = descs[d];
            int qh = q.addr[0] + (q.partRelative ? p : 0);
            // a performance bulk carries the 80 common bytes, the 112 effect bytes and four 52-byte parts
            int off = -1;
            if (ah == 0x10 && qh == 0x10 && q.addr[1] == 0 && q.addr[2] < 80) off = q.addr[2];
            else if (ah == 0x10 && qh == 0x10 && q.addr[1] == 0 && q.addr[2] >= 0x50) off = 80 + q.addr[2] - 0x50;
            else if (ah == 0x10 && qh == 0x10 && q.addr[1] == 1) off = 80 + 48 + q.addr[2];
            else if (ah == 0x10 && qh >= 0x30 && qh <= 0x33) off = 192 + 52 * (qh - 0x30) + q.addr[2];
            else if (ah == qh && ah >= 0x40 && ah <= 0x43) off = q.addr[2];
            else if (ah == qh - 0x20 && qh >= 0x60 && qh <= 0x63) off = 112 + q.addr[1] * 62 + q.addr[2];
            else if (ah == 0x00 && qh == 0x00 && q.addr[2] < 76) off = q.addr[2];
            if (off < 0 || off >= count) continue;
            int v = data[off];
            if (q.wide && off + 1 < count) v = (v << 7) | data[off + 1];
            if (q.width > 0) v = (v >> q.shift) & ((1 << q.width) - 1);
            params[d]->setValueNotifyingHost(params[d]->convertTo0to1((float)juce::jlimit(q.min, q.max, v)));
        }
        i += 9 + (size_t)count + 2;
    }
}

void Processor::selectPart(int p) {
    part = juce::jlimit(0, 3, p);
    needRefresh = true;
}

void Processor::clearMidiMapping(int paramIndex) {
    if (paramIndex >= 0 && paramIndex < (int)ccForParam.size()) ccForParam[(size_t)paramIndex] = -1;
}

int Processor::mappedCcFor(int paramIndex) const {
    return (paramIndex >= 0 && paramIndex < (int)ccForParam.size()) ? ccForParam[(size_t)paramIndex] : -1;
}

void Processor::getStateInformation(juce::MemoryBlock& out) {
    // The engine's bulk dump is the state. Anything the plugin adds on top rides in a small trailer.
    std::vector<uint8_t> sysex;
    dev.getState(sysex);
    juce::MemoryOutputStream s(out, false);
    s.writeInt(2);                                  // format
    s.writeInt(perfIndex.load());
    s.writeInt(part.load());
    s.writeInt((int)ccForParam.size());
    for (int cc : ccForParam) s.writeInt(cc);
    s.writeInt((int)sysex.size());
    s.write(sysex.data(), sysex.size());
    auto romPath = patchManager.romFile().getFullPathName();
    s.writeString(romPath);
}

void Processor::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream s(data, (size_t)size, false);
    const int format = s.readInt();
    if (format != 1 && format != 2) return;
    perfIndex = format >= 2 ? s.readInt() : -1;     // format 1 did not record which performance it was
    part = juce::jlimit(0, 3, s.readInt());
    int n = s.readInt();
    for (int i = 0; i < n; ++i) {
        int cc = s.readInt();
        if (i < (int)ccForParam.size()) ccForParam[(size_t)i] = cc;
    }
    int len = s.readInt();
    if (len > 0 && len < 4 * 1024 * 1024) {
        std::vector<uint8_t> sysex((size_t)len);
        s.read(sysex.data(), len);
        dev.setState(sysex.data(), sysex.size());
    }
    auto romPath = s.readString();
    if (romPath.isNotEmpty()) patchManager.setRomFile(juce::File(romPath));
    needRefresh = true;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

}  // namespace fs1rplug

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new fs1rplug::Processor(); }
