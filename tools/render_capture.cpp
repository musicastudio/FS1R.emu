// render_capture - render through the engine, offline, on any platform.
//
//   render_capture [-r eprom.bin] [-g gain] [-f] out.wav file.mid        a capture request
//   render_capture [-r eprom.bin] [-P perf] [-p voice] [-fseq n] [-v file.syx]
//                  [-n note] [-n2 note] [-cc num=val] [-mono time] [-d secs] -w out.wav
//
// The same renders fs1r_emu.exe does, minus the console, so the calibration loop in
// tools/analyze_capture.py and the render regression in tools/regress.py both run anywhere rather than
// only on Windows. -f writes 32-bit float, which is what the comparison against a 24-bit hardware
// capture wants; without it the output is 16-bit.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "fs1r.h"
#include "fsvr/smf.h"

int main(int argc, char** argv) {
    const char* rom = nullptr;
    const char* wav = nullptr;
    const char* mid = nullptr;
    const char* syx = nullptr;
    double gain = 1.0, tail = 1.0, secs = 3.0;
    int perfIdx = -1, pick = -1, fseqIdx = -1, note = 60, note2 = -1, monoTime = -1, channel = -1;
    std::vector<std::pair<int, int>> cc;
    fs1r_smf::WavFormat fmt = fs1r_smf::WAV_S16;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-r" && i + 1 < argc) rom = argv[++i];
        else if (a == "-g" && i + 1 < argc) gain = atof(argv[++i]);
        else if (a == "-d" && i + 1 < argc) { tail = atof(argv[i + 1]); secs = atof(argv[i + 1]); i++; }
        else if (a == "-fseq" && i + 1 < argc) fseqIdx = atoi(argv[++i]);
        else if (a == "-f") fmt = fs1r_smf::WAV_F32;
        else if (a == "-P" && i + 1 < argc) perfIdx = atoi(argv[++i]);
        else if (a == "-p" && i + 1 < argc) pick = atoi(argv[++i]);
        else if (a == "-v" && i + 1 < argc) syx = argv[++i];
        else if (a == "-c" && i + 1 < argc) channel = atoi(argv[++i]) - 1;
        else if (a == "-n" && i + 1 < argc) note = atoi(argv[++i]);
        else if (a == "-n2" && i + 1 < argc) note2 = atoi(argv[++i]);
        else if (a == "-mono" && i + 1 < argc) monoTime = atoi(argv[++i]);
        else if (a == "-w" && i + 1 < argc) wav = argv[++i];
        else if (a == "-cc" && i + 1 < argc) {
            std::string t = argv[++i]; size_t e = t.find('=');
            if (e != std::string::npos) cc.emplace_back(atoi(t.substr(0, e).c_str()), atoi(t.substr(e + 1).c_str()));
        }
        else if (a == "-float") fmt = fs1r_smf::WAV_F32;
        else if (!wav) wav = argv[i];
        else if (!mid) mid = argv[i];
    }
    if (!wav) {
        printf("usage: render_capture [-r eprom.bin] [-g gain] [-d tail secs] [-f] out.wav file.mid\n"
               "       render_capture [-r eprom.bin] [-P perf] [-p voice] [-fseq n] [-v file.syx] [-c ch]\n"
               "                      [-n note] [-n2 note] [-cc num=val] [-mono time] [-d secs] -w out.wav\n");
        return 1;
    }
    fs1r::Device dev;
    if (rom && !dev.loadRom(rom)) { printf("cannot load %s\n", rom); return 1; }
    dev.setSampleRate(fs1r::ENGINE_RATE);
    dev.setGain(gain);
    if (perfIdx >= 0 && !dev.loadRomPerformance(perfIdx)) { printf("-P needs -r\n"); return 1; }
    if (syx) {
        FILE* f = fopen(syx, "rb"); if (!f) { printf("cannot open %s\n", syx); return 1; }
        std::vector<uint8_t> d; uint8_t tmp[65536]; size_t n;
        while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) d.insert(d.end(), tmp, tmp + n);
        fclose(f);
        if (!dev.loadSyx(d.data(), d.size(), pick < 0 ? 0 : pick, 0)) { printf("no dump #%d in %s\n", pick, syx); return 1; }
    } else if (rom && pick >= 0) dev.loadRomVoice(0, pick);
    if (fseqIdx >= 0 && !dev.loadRomFseq(fseqIdx)) { printf("-f needs -r\n"); return 1; }
    if (monoTime >= 0) fs1r_smf::set_mono_porta(dev, monoTime);
    printf("performance \"%s\"", dev.performanceName());
    for (int p = 0; p < 4; p++) if (dev.partActive(p)) printf("  part%d \"%s\" alg %d", p + 1, dev.voiceName(p), dev.algorithm(p) + 1);
    if (dev.fseqFrames()) printf("  fseq \"%s\" (%d frames)", dev.fseqName(), dev.fseqFrames());
    printf("\n");
    if (mid) { dev.forceChannel(channel); return fs1r_smf::render_smf(dev, mid, wav, tail, fmt); }
    return fs1r_smf::render_note(dev, wav, note, secs, note2, cc, fmt);
}
