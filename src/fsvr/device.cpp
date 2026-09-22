// fsvr/device.cpp - fs1r::Device: host sample rate, state, the public surface. Ours.
#include "fs1r/internal.h"

// ------------------------------------------------------------------------------------------ fs1r::Device
namespace fs1r {

struct Device::Impl {
    Synth s;
    Rom rom;
    double hostRate = ENGINE_RATE;
    double pos = 0;                          // fractional read position between engine samples
    float h[2][4] = {};                      // four-point history per channel for the interpolator
    float eng[2][256]; int engFill = 0, engRead = 0;
    bool echo = false;
    // ponytail: cubic interpolation between engine samples. Audible aliasing above ~15 kHz when the host
    // runs at 44.1; swap in a polyphase FIR if a spectrum measurement ever shows it matters.
    inline void pull() {
        if (engRead >= engFill) {
            engFill = 256; engRead = 0;
            s.render(eng[0], eng[1], engFill);
        }
        for (int c = 0; c < 2; c++) { h[c][0] = h[c][1]; h[c][1] = h[c][2]; h[c][2] = h[c][3]; h[c][3] = eng[c][engRead]; }
        engRead++;
    }
    static inline float cubic(const float* v, double t) {
        double a = v[3] - v[2] - v[0] + v[1], b = v[0] - v[1] - a, c = v[2] - v[0];
        return (float)(((a * t + b) * t + c) * t + v[1]);
    }
};

Device::Device() : p(new Impl) { init_tables(); }
Device::~Device() = default;

void Device::setSampleRate(double hostRate) { if (hostRate > 1000) p->hostRate = hostRate; }
double Device::sampleRate() const { return p->hostRate; }
void Device::setGain(double g) { p->s.gain = g; }
void Device::setEchoParameters(bool on) { p->echo = on; }
void Device::forceChannel(int channel) { p->s.forceChannel = channel; }

void Device::process(float* outL, float* outR, int n) {
    if (p->hostRate == ENGINE_RATE) {        // the common case: no resampling at all
        p->s.render(outL, outR, n);
        return;
    }
    const double step = ENGINE_RATE / p->hostRate;
    for (int i = 0; i < n; i++) {
        p->pos += step;
        while (p->pos >= 1.0) { p->pos -= 1.0; p->pull(); }
        outL[i] = Impl::cubic(p->h[0], p->pos);
        outR[i] = Impl::cubic(p->h[1], p->pos);
    }
}

void Device::sendMidi(const uint8_t* b, size_t len) {
    if (!len) return;
    if (b[0] == 0xF0) {
        std::lock_guard<std::mutex> lk(p->s.mtx);
        if (load_sysex(p->s, p->rom.ok ? &p->rom : nullptr, b, len, 0, 0)) return;
        if (apply_param_change_locked(p->s, b, len) && p->echo && len >= 10 && (b[2] & 0xF0) == 0x10)
            p->s.push_param(b[4], b[5], b[6], b[7] << 7 | b[8]);
        return;
    }
    std::lock_guard<std::mutex> lk(p->s.mtx);
    p->s.midi_in(b[0], len > 1 ? b[1] & 0x7F : 0, len > 2 ? b[2] & 0x7F : 0);
}

bool Device::nextMidiOut(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lk(p->s.mtx);
    if (p->s.outQ.empty()) return false;
    out.swap(p->s.outQ.front());
    p->s.outQ.erase(p->s.outQ.begin());
    return true;
}

void Device::allNotesOff() { std::lock_guard<std::mutex> lk(p->s.mtx); p->s.all_off(); }

void Device::getState(std::vector<uint8_t>& sysex) const {
    std::lock_guard<std::mutex> lk(p->s.mtx);
    Synth& S = p->s;
    S.outQ.clear();
    S.push_bulk(0x00, 0, 0, S.sys, 76);
    uint8_t perfBytes[400]; S.current_perf_bytes(perfBytes);
    S.push_bulk(0x10, 0, 0, perfBytes, 400);
    for (int i = 0; i < 4; i++) S.push_bulk(0x40 + i, 0, 0, S.perf.part[i].voice.raw, 608);
    if (S.fseq.valid) { std::vector<uint8_t> f; S.fseq_bytes(f); S.push_bulk(0x70, 0, 0, f.data(), (int)f.size()); }
    sysex.clear();
    for (auto& m : S.outQ) sysex.insert(sysex.end(), m.begin(), m.end());
    S.outQ.clear();
}

bool Device::setState(const uint8_t* d, size_t len) {
    bool any = false;
    for (size_t i = 0; i < len; ) {
        if (d[i] != 0xF0) { i++; continue; }
        size_t j = i + 1;
        while (j < len && d[j] != 0xF7) j++;
        if (j >= len) break;
        size_t n = j - i + 1;
        {
            std::lock_guard<std::mutex> lk(p->s.mtx);
            if (load_sysex(p->s, p->rom.ok ? &p->rom : nullptr, d + i, n, 0, 0)) any = true;
            else if (apply_param_change_locked(p->s, d + i, n)) any = true;
        }
        i = j + 1;
    }
    return any;
}

bool Device::loadRom(const char* path) {
    if (!load_rom(p->rom, path)) return false;
    p->s.rom = &p->rom;
    return true;
}
bool Device::romLoaded() const { return p->rom.ok; }
bool Device::loadRomPerformance(int idx) { if (!p->rom.ok) return false; return rom_perf(p->s, p->rom, idx); }
bool Device::loadRomVoice(int part, int idx) {
    if (!p->rom.ok || part < 0 || part > 3) return false;
    std::lock_guard<std::mutex> lk(p->s.mtx);
    rom_voice(p->rom, idx, p->s.perf.part[part].voice);
    return true;
}
bool Device::loadRomFseq(int n) { if (!p->rom.ok) return false; return rom_fseq(p->s, p->rom, n); }
bool Device::loadSyx(const uint8_t* d, size_t len, int pick, int part) {
    return load_sysex(p->s, p->rom.ok ? &p->rom : nullptr, d, len, pick, part);
}

const char* Device::performanceName() const { return p->s.perf.name; }
const char* Device::voiceName(int part) const { return p->s.perf.part[clampi(part, 0, 3)].voice.name; }
int Device::algorithm(int part) const { return p->s.perf.part[clampi(part, 0, 3)].voice.alg; }
bool Device::partActive(int part) const { return p->s.perf.part[clampi(part, 0, 3)].rcv() != 0x7F; }
const char* Device::fseqName() const { return p->s.fseq.valid ? p->s.fseq.name : ""; }
int Device::fseqFrames() const { return p->s.fseq.valid ? p->s.fseq.nframes : 0; }
bool Device::fseqFrame(int step, uint8_t out[50]) const {
    if (!p->s.fseq.valid || step < 0 || step >= p->s.fseq.nframes) return false;
    memcpy(out, p->s.fseq.frame[step], 50);
    return true;
}
int Device::fseqPosition() const { return p->s.fseq.valid ? p->s.fseqStep : 0; }
int Device::fseqPart() const { return p->s.fseqPart; }

int Device::selfTest() { init_tables(); Synth s; return selftest(s); }

}  // namespace fs1r

