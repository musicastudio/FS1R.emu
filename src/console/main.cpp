// console/main.cpp - the test console on top of fs1r_lib: WinMM MIDI in and out, waveOut, offline render.
//
//   fs1r_emu.exe -l                                   list MIDI ports
//   fs1r_emu.exe -selftest                            run the engine self check
//   fs1r_emu.exe [-m N] [-o N] [-c ch] [-v voice.syx] [-r eprom.bin [-p voice] [-P perf] [-f fseq]]
//                [-g gain] [-w test.wav [-n note] [-n2 note] [-cc num=val] [-d secs]]
//
// The console owns no synthesis. It feeds MIDI bytes to fs1r::Device and pulls audio back.
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "fs1r.h"
#include "fsvr/smf.h"

using fs1r_smf::write_wav;
using fs1r_smf::render_smf;
static const int SR = (int)fs1r::ENGINE_RATE;    // the console runs the host at the engine rate
static const int BLOCK = 128;                    // frames per waveOut buffer (~2.7 ms)
static const int NBUF = 8;

// ------------------------------------------------------------------------------------------ MIDI plumbing
struct MidiQueue {
    std::atomic<uint32_t> head{0}, tail{0}; uint32_t buf[1024];
    void push(uint32_t m) { uint32_t h = head.load(); if (((h + 1) & 1023) != (tail.load() & 1023)) { buf[h & 1023] = m; head.store(h + 1); } }
    bool pop(uint32_t& m) { uint32_t t = tail.load(); if (t == head.load()) return false; m = buf[t & 1023]; tail.store(t + 1); return true; }
};
static MidiQueue g_q;
static std::mutex g_sxmtx; static std::vector<std::vector<uint8_t>> g_sysex;
static MIDIHDR g_hdr[2]; static uint8_t g_sxbuf[2][65536];
static HMIDIIN g_hmi = nullptr;
static HMIDIOUT g_hmo = nullptr;

static void CALLBACK midi_cb(HMIDIIN h, UINT msg, DWORD_PTR, DWORD_PTR p1, DWORD_PTR) {
    if (msg == MIM_DATA) g_q.push((uint32_t)p1);
    else if (msg == MIM_LONGDATA) {
        MIDIHDR* hdr = (MIDIHDR*)p1;
        if (hdr->dwBytesRecorded) { std::lock_guard<std::mutex> lk(g_sxmtx); g_sysex.emplace_back(hdr->lpData, hdr->lpData + hdr->dwBytesRecorded); }
        midiInAddBuffer(h, hdr, sizeof(MIDIHDR));
    }
}
static void send_sysex(const std::vector<uint8_t>& m) {
    if (!g_hmo) return;
    MIDIHDR h{}; h.lpData = (LPSTR)m.data(); h.dwBufferLength = (DWORD)m.size();
    if (midiOutPrepareHeader(g_hmo, &h, sizeof h) != MMSYSERR_NOERROR) return;
    midiOutLongMsg(g_hmo, &h, sizeof h);
    while (!(h.dwFlags & MHDR_DONE)) Sleep(0);
    midiOutUnprepareHeader(g_hmo, &h, sizeof h);
}
static void pump_midi(fs1r::Device& dev) {
    uint32_t m;
    while (g_q.pop(m)) { uint8_t b[3] = {(uint8_t)(m & 0xFF), (uint8_t)(m >> 8 & 0x7F), (uint8_t)(m >> 16 & 0x7F)}; dev.sendMidi(b, 3); }
    std::vector<std::vector<uint8_t>> sx;
    { std::lock_guard<std::mutex> lk(g_sxmtx); sx.swap(g_sysex); }
    for (auto& v : sx) {
        dev.sendMidi(v.data(), v.size());
        if (v.size() > 9 && v[3] == 0x5E && (v[2] & 0xF0) == 0x00) printf("bulk received: %s / %s\n", dev.performanceName(), dev.voiceName(0));
    }
    std::vector<uint8_t> out;
    while (dev.nextMidiOut(out)) send_sysex(out);
}

// ------------------------------------------------------------------------------------------ offline render
static int g_note2 = -1;   // -n2: second note played at 1/3 while the first is held (mono legato / portamento)
static std::vector<std::pair<int, int>> g_cc;   // -cc num=val: control changes sent before the note
static int render_wav(fs1r::Device& dev, const char* path, int note, double secs) {
    int frames = (int)(secs * SR), half = frames / 2, third = frames / 3;
    std::vector<float> l(frames, 0.f), r(frames, 0.f);
    dev.forceChannel(0);                                   // offline: every part with a receive channel plays
    auto note_on = [&](int n, int v) { uint8_t m[3] = {0x90, (uint8_t)n, (uint8_t)v}; dev.sendMidi(m, 3); };
    auto note_off = [&](int n) { uint8_t m[3] = {0x80, (uint8_t)n, 0}; dev.sendMidi(m, 3); };
    for (auto& c : g_cc) { uint8_t m[3] = {0xB0, (uint8_t)c.first, (uint8_t)c.second}; dev.sendMidi(m, 3); }
    note_on(note, 100);
    if (g_note2 >= 0) {
        dev.process(l.data(), r.data(), third);
        note_on(g_note2, 100);
        dev.process(l.data() + third, r.data() + third, half - third);
    } else dev.process(l.data(), r.data(), half);
    note_off(note); if (g_note2 >= 0) note_off(g_note2);
    dev.process(l.data() + half, r.data() + half, frames - half);
    write_wav(path, l, r);
    return 0;
}

// ------------------------------------------------------------------------------------------ main
static std::string ini_path() { char p[MAX_PATH]; GetModuleFileNameA(nullptr, p, MAX_PATH); std::string s(p); size_t k = s.find_last_of("\\/"); return s.substr(0, k + 1) + "fs1r_emu.ini"; }
static int list_midi_out() { int n = midiOutGetNumDevs(); for (int i = 0; i < n; i++) { MIDIOUTCAPSA c; midiOutGetDevCapsA(i, &c, sizeof c); printf("  %d: %s\n", i, c.szPname); } return n; }
static int list_midi() { int n = midiInGetNumDevs(); for (int i = 0; i < n; i++) { MIDIINCAPSA c; midiInGetDevCapsA(i, &c, sizeof c); printf("  %d: %s\n", i, c.szPname); } return n; }

int main(int argc, char** argv) {
    fs1r::Device dev;
    dev.setSampleRate(SR);
    int midiPort = -1, midiOutPort = -1, pick = -1, perfIdx = -1, fseqIdx = -1, channel = -1;
    const char* syx = nullptr; const char* romPath = nullptr; const char* wav = nullptr; const char* smf = nullptr;
    int testNote = 60, monoTime = -1; double testSecs = 3.0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-selftest") return fs1r::Device::selfTest();
        else if (a == "-l") { printf("MIDI inputs:\n"); list_midi(); printf("MIDI outputs:\n"); list_midi_out(); return 0; }
        else if (a == "-m" && i + 1 < argc) midiPort = atoi(argv[++i]);
        else if (a == "-o" && i + 1 < argc) midiOutPort = atoi(argv[++i]);
        else if (a == "-c" && i + 1 < argc) { channel = atoi(argv[++i]) - 1; dev.forceChannel(channel); }
        else if (a == "-v" && i + 1 < argc) syx = argv[++i];
        else if (a == "-p" && i + 1 < argc) pick = atoi(argv[++i]);
        else if (a == "-P" && i + 1 < argc) perfIdx = atoi(argv[++i]);
        else if (a == "-f" && i + 1 < argc) fseqIdx = atoi(argv[++i]);
        else if (a == "-r" && i + 1 < argc) romPath = argv[++i];
        else if (a == "-g" && i + 1 < argc) dev.setGain(atof(argv[++i]));
        else if (a == "-w" && i + 1 < argc) wav = argv[++i];
        else if (a == "-n" && i + 1 < argc) testNote = atoi(argv[++i]);
        else if (a == "-n2" && i + 1 < argc) g_note2 = atoi(argv[++i]);
        else if (a == "-cc" && i + 1 < argc) {
            std::string t = argv[++i]; size_t e = t.find('=');
            if (e != std::string::npos) g_cc.emplace_back(atoi(t.substr(0, e).c_str()), atoi(t.substr(e + 1).c_str()));
        }
        else if (a == "-mono" && i + 1 < argc) monoTime = atoi(argv[++i]);
        else if (a == "-d" && i + 1 < argc) testSecs = atof(argv[++i]);
        else if (a == "-smf" && i + 1 < argc) smf = argv[++i];
        else { printf("usage: fs1r_emu [-l] [-selftest] [-m midiport] [-o midiout] [-c channel] [-v file.syx [-p index]]\n"
                      "                [-r eprom.bin [-p voice] [-P performance] [-f fseq]] [-g gain]\n"
                      "                [-w test.wav [-n note] [-n2 note] [-cc num=val] [-d seconds]] [-mono portatime]\n"
                      "                [-smf file.mid -w out.wav [-d tail seconds]]\n"); return 1; }
    }
    if (romPath && !dev.loadRom(romPath)) { printf("cannot read 2 MB EPROM image %s\n", romPath); return 1; }
    if (perfIdx >= 0 && !dev.loadRomPerformance(perfIdx)) { printf("-P needs -r\n"); return 1; }
    if (syx) {
        FILE* f = fopen(syx, "rb"); if (!f) { printf("cannot open %s\n", syx); return 1; }
        std::vector<uint8_t> d; uint8_t tmp[65536]; size_t n;
        while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) d.insert(d.end(), tmp, tmp + n);
        fclose(f);
        if (!dev.loadSyx(d.data(), d.size(), std::max(0, pick), 0)) { printf("no FS1R voice/performance/Fseq or DX7 voice dump #%d found in %s\n", std::max(0, pick), syx); return 1; }
    } else if (dev.romLoaded() && pick >= 0) dev.loadRomVoice(0, pick);
    if (fseqIdx >= 0 && !dev.loadRomFseq(fseqIdx)) { printf("-f needs -r\n"); return 1; }
    if (monoTime >= 0) {   // part 1 mono, full-time portamento with this time, straight through sysex
        uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, 0x30, 0x00, 0x05, 0, 0, 0xF7};
        dev.sendMidi(m, 10);                                             // mono
        m[6] = 0x06; m[8] = 0; dev.sendMidi(m, 10);                      // last-note priority
        m[6] = 0x24; m[8] = 3; dev.sendMidi(m, 10);                      // portamento on, full time
        m[6] = 0x25; m[8] = (uint8_t)(monoTime & 0x7F); dev.sendMidi(m, 10);
    }
    printf("performance \"%s\"", dev.performanceName());
    for (int p = 0; p < 4; p++) if (dev.partActive(p)) printf("  part%d \"%s\" alg %d", p + 1, dev.voiceName(p), dev.algorithm(p) + 1);
    if (dev.fseqFrames()) printf("  fseq \"%s\" (%d frames)", dev.fseqName(), dev.fseqFrames());
    printf("\n");
    if (smf) { if (!wav) { printf("-smf needs -w out.wav\n"); return 1; } return render_smf(dev, smf, wav, testSecs); }
    if (wav) return render_wav(dev, wav, testNote, testSecs);

    std::string ini = ini_path();
    if (midiPort < 0) { FILE* f = fopen(ini.c_str(), "r"); if (f) { if (fscanf(f, "midi_in=%d", &midiPort) != 1) midiPort = -1; fclose(f); } }
    int ndev = midiInGetNumDevs();
    if (ndev == 0) printf("no MIDI inputs found; running without MIDI (Ctrl-C to quit)\n");
    else if (midiPort < 0 || midiPort >= ndev) {
        printf("MIDI inputs:\n"); list_midi(); printf("choose [0-%d]: ", ndev - 1); fflush(stdout);
        if (scanf("%d", &midiPort) != 1 || midiPort < 0 || midiPort >= ndev) midiPort = 0;
        FILE* f = fopen(ini.c_str(), "w"); if (f) { fprintf(f, "midi_in=%d\n", midiPort); fclose(f); }
    }
    if (ndev) {
        MIDIINCAPSA c; midiInGetDevCapsA(midiPort, &c, sizeof c);
        if (midiInOpen(&g_hmi, midiPort, (DWORD_PTR)midi_cb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { printf("cannot open MIDI input %d\n", midiPort); return 1; }
        for (int i = 0; i < 2; i++) { g_hdr[i] = {}; g_hdr[i].lpData = (LPSTR)g_sxbuf[i]; g_hdr[i].dwBufferLength = sizeof g_sxbuf[i]; midiInPrepareHeader(g_hmi, &g_hdr[i], sizeof(MIDIHDR)); midiInAddBuffer(g_hmi, &g_hdr[i], sizeof(MIDIHDR)); }
        midiInStart(g_hmi);
        if (midiOutPort >= 0 && midiOutPort < (int)midiOutGetNumDevs()) {
            if (midiOutOpen(&g_hmo, midiOutPort, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) { printf("MIDI out: %d (dump and parameter replies)\n", midiOutPort); dev.setEchoParameters(true); }
            else g_hmo = nullptr;
        }
        printf("MIDI in: %d (%s), %s\n", midiPort, c.szPname, channel < 0 ? "parts on their receive channels (part 1 = performance channel 1)" : ("all parts on channel " + std::to_string(channel + 1)).c_str());
    }
    WAVEFORMATEX wf{}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = SR; wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = SR * 4;
    HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT hwo;
    if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { printf("waveOutOpen failed\n"); return 1; }
    static int16_t pcm[NBUF][BLOCK * 2]; static WAVEHDR wh[NBUF];
    for (int i = 0; i < NBUF; i++) { wh[i] = {}; wh[i].lpData = (LPSTR)pcm[i]; wh[i].dwBufferLength = BLOCK * 4; waveOutPrepareHeader(hwo, &wh[i], sizeof(WAVEHDR)); wh[i].dwFlags |= WHDR_DONE; }
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    printf("running, Ctrl-C to quit\n");
    float fl[BLOCK], fr[BLOCK];
    int cur = 0;
    for (;;) {
        WAVEHDR& h = wh[cur];
        while (!(h.dwFlags & WHDR_DONE)) WaitForSingleObject(ev, 10);
        pump_midi(dev);
        dev.process(fl, fr, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            pcm[cur][2 * i] = (int16_t)std::max(-32767.f, std::min(32767.f, fl[i] * 32767.f));
            pcm[cur][2 * i + 1] = (int16_t)std::max(-32767.f, std::min(32767.f, fr[i] * 32767.f));
        }
        h.dwFlags &= ~WHDR_DONE;
        waveOutWrite(hwo, &h, sizeof(WAVEHDR));
        cur = (cur + 1) % NBUF;
    }
}
