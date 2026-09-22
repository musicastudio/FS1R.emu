// render_capture - play a capture request MIDI file through the engine and write the result.
//
//   render_capture [-r eprom.bin] [-g gain] [-f] out.wav file.mid
//
// The same render fs1r_emu.exe -smf does, minus the console, so the calibration loop in
// tools/analyze_capture.py runs anywhere rather than only on Windows. -f writes 32-bit float, which is
// what the comparison against a 24-bit hardware capture wants; without it the output is 16-bit.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "fs1r.h"
#include "fsvr/smf.h"

int main(int argc, char** argv) {
    const char* rom = nullptr;
    const char* wav = nullptr;
    const char* mid = nullptr;
    double gain = 1.0, tail = 1.0;
    fs1r_smf::WavFormat fmt = fs1r_smf::WAV_S16;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-r" && i + 1 < argc) rom = argv[++i];
        else if (a == "-g" && i + 1 < argc) gain = atof(argv[++i]);
        else if (a == "-d" && i + 1 < argc) tail = atof(argv[++i]);
        else if (a == "-f") fmt = fs1r_smf::WAV_F32;
        else if (!wav) wav = argv[i];
        else if (!mid) mid = argv[i];
    }
    if (!wav || !mid) {
        printf("usage: render_capture [-r eprom.bin] [-g gain] [-d tail secs] [-f] out.wav file.mid\n");
        return 1;
    }
    fs1r::Device dev;
    if (rom && !dev.loadRom(rom)) { printf("cannot load %s\n", rom); return 1; }
    dev.setSampleRate(fs1r::ENGINE_RATE);
    dev.setGain(gain);
    dev.forceChannel(-1);
    return fs1r_smf::render_smf(dev, mid, wav, tail, fmt);
}
