// fs1r_emu.cpp - Yamaha FS1R behavioural softsynth.
//
//   fs1r_emu.exe -l                                   list MIDI inputs
//   fs1r_emu.exe [-m N] [-c ch] [-v voice.syx] [-r eprom.bin [-p voice] [-P perf] [-f fseq]] [-g gain]
//                [-w test.wav [-n note] [-d secs]]
//
// Four parts, 32 channels, MIDI in (notes, bend, CC1/2/4/7/11/16-22/64/120/123, aftertouch, FS1R bulk
// dumps and parameter changes), WinMM waveOut 44.1 kHz. DX7 VCED voices are converted the way the
// firmware does it.
//
// Everything the FS1R's CPU does between MIDI and the tone generator registers is reproduced from the
// v1.20 firmware with its own ROM tables (src/fs1r_rom_tables.h): velocity curves and attenuation, level
// key scaling, pitch (note table, detune, tune, bend, portamento), the software pitch EG and LFO1 with
// their 854.5 Hz tick, part levels, mono/poly note handling, performances, Fseq playback. What the
// YMP706 chip does with those register values is not in any file; the conversions below marked
// INFERRED follow the DX7 (same design lineage) and the formant synthesis patent, see docs/.
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
#include "fs1r_algorithms.h"
#include "fs1r_rom_tables.h"

static const int SR = 44100;
static const int BLOCK = 128;                    // frames per waveOut buffer (~2.9 ms)
static const int NBUF = 8;
static const int NCHAN = 32;
static const double PI = 3.14159265358979323846;
static const double CPU_HZ = 28000000.0;         // SCI BRR 27 gives exactly 31250 baud at 28 MHz
static const double TICK_HZ = CPU_HZ / 16.0 / 9099.0;   // MTU2 TGRA compare every 0x238B counts at clock/16 -> 192.3 Hz LFO/PEG/portamento tick
static const double FM_INDEX = 1.0;              // INFERRED: cycles of phase deviation for a full-level modulator (DX7)
static const double LEVEL_DB = 0.375;            // INFERRED: dB per step of the 8-bit level registers (LEVTAB is the DX7 0.75 dB curve, chip gets 2x)

// ------------------------------------------------------------------------------------------ firmware helpers
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int eb86(int v) { return std::min(255, (((v & 0xFF) << 1) * 0xA5) >> 7); }   // 0..99 -> 0..127
static inline int eb70(int v) { return (((v & 0xFF) << 1) * 0xA5) >> 8; }                   // 0..99 -> 0..127 (bandwidth)
static inline int egrate(int t) { return ((99 - clampi(t, 0, 99)) * 0xA4) >> 8; }          // EG time -> chip rate 0..63
static inline int keygroup(int n) { return std::max(0, (KEYFACT[clampi(n, 0, 127)] >> 2) - 3); }
// FUN_00010d7a: key tracking of fixed/formant frequencies, notescale 0..99, pm = pitch word - C3
static inline int keytrack(int ns, int pm) { int k = eb70(ns); return k ? ((k + 1) * pm) / 128 : 0; }
// FUN_00013596: frequency velocity sensitivity, b = raw sysex nibble (sens + 7)
static inline int fvs_term(int b, int vel) {
    b &= 0xF;
    int m = (b & 8) ? FVSTAB[1 + (b & 7)] : FVSTAB[7 - (b & 7)];
    int v = (vel - 64) * m; if (!(b & 8)) v = -v;
    return v >> 3;
}
// FUN_00012a32: velocity attenuation, b = raw sysex nibble (sens + 7), 0.375 dB units
static inline int vel_att(int b, int vel) {
    b &= 0xF; int s, t;
    if (b & 8) { s = (b & 7) + 1; t = VELW[vel]; } else { s = 7 - (b & 7); t = VELW[127 - vel]; }
    return std::min(255, (15 - 2 * s) + ((s * 32 * t) >> 8));
}
static inline double db2lin(double db) { return db <= -150 ? 0.0 : pow(10.0, db / 20.0); }
static inline double word_hz(int w) { return 440.0 * pow(2.0, (w - 26861) / 1024.0); }   // 1024 units per octave

// ------------------------------------------------------------------------------------------ tables (chip side)
static float g_sin[4097];
static float g_win[8][1025];
static void init_tables() {
    for (int i = 0; i <= 4096; i++) g_sin[i] = (float)sin(2 * PI * i / 4096.0);
    for (int s = 0; s < 8; s++) {                                     // INFERRED window shape: sin^(2(skirt+1)), patent
        double p = 2.0 * (s + 1);
        for (int i = 0; i <= 1024; i++) g_win[s][i] = (float)pow(sin(PI * i / 1024.0), p);
    }
}
static inline float fsin(double ph) { double x = (ph - floor(ph)) * 4096.0; int i = (int)x; float f = (float)(x - i); return g_sin[i] + (g_sin[i + 1] - g_sin[i]) * f; }
static inline float fwin(int s, double x) { double y = x * 1024.0; int i = (int)y; if (i >= 1024) return 0.f; float f = (float)(y - i); return g_win[s][i] + (g_win[s][i + 1] - g_win[s][i]) * f; }
// INFERRED: chip EG rate 0..63 -> seconds for a full 96 dB traverse. The firmware maps time T to rate (99-T)*0xA4>>8, the
// exact inverse of the DX7's (R*41)>>6, and its DX7 converter uses T = 99 - R, so the chip is assumed to time its EG like the
// DX7 EGS: increment (4 + (q & 3)) << (q >> 2) per 64 samples on a 2^28 = 96 dB scale (Dexed). 6.6 ms at 63, 380 s at 0.
static inline double rate_secs(int q) { q = clampi(q, 0, 63); return pow(2.0, 26 - (q >> 2)) / (4 + (q & 3)) / SR; }
// INFERRED: per-op pitch mod sensitivity 0..7 -> fraction of the channel LFO pitch word (DX7 PMS curve)
static const double PMS_FRAC[8] = {0, 0.0264, 0.0534, 0.0889, 0.1612, 0.2769, 0.4967, 1.0};

// ------------------------------------------------------------------------------------------ voice data
struct OpV {
    int keysync, transpose, coarse, fine, notescale, bwbias, form, fixed, skirt, fseqtrk, bw, detune;
    int fegInit, fegAtt, fegAttT, fegDecT;
    int L[4], T[4], hold, tscale, level, bp, ld, rd, lc, rc;
    int fbias, pms, fmsb, amsb, ams, fms, egbias;   // fmsb/amsb = raw sense nibbles (sens+7)
};
struct OpU {
    int transpose, mode, coarse, fine, notescale, bw, bwbias, res, skirt;
    int fegInit, fegAtt, fegAttT, fegDecT, level, lks;
    int L[4], T[4], hold, tscale;
    int fbias, fmsb, amsb, ams, fms, egbias;
};
struct Voice {
    uint8_t raw[608];
    char name[11]; int cat;
    int lfo1wave, lfo1speed, lfo1delay, lfo1sync, pmd, amd, fmd;
    int noteshift, pegL[5], pegT[4], pegVel, pegRange, pegTscale;
    int fseqV, fseqU, alg, corr[8], fb;
    OpV v[8]; OpU u[8];
};
static void decode_voice(Voice& V) {
    const uint8_t* b = V.raw;
    memcpy(V.name, b, 10); V.name[10] = 0; V.cat = b[0x0E];
    V.lfo1wave = std::min<int>(b[0x10], 5); V.lfo1speed = b[0x11]; V.lfo1delay = b[0x12]; V.lfo1sync = b[0x13] & 1;
    V.pmd = b[0x15]; V.amd = b[0x16]; V.fmd = b[0x17];
    V.noteshift = (b[0x1E] & 0x3F) - 24;
    V.pegL[0] = b[0x1F]; V.pegL[1] = b[0x20]; V.pegL[2] = b[0x21]; V.pegL[4] = b[0x22]; V.pegL[3] = b[0x3E];
    for (int i = 0; i < 4; i++) V.pegT[i] = b[0x23 + i];
    V.pegVel = b[0x27] & 7; V.pegRange = b[0x3B] & 3; V.pegTscale = b[0x3C] & 7;
    V.fseqV = (b[0x28] & 1) << 7 | (b[0x29] & 0x7F); V.fseqU = (b[0x2A] & 1) << 7 | (b[0x2B] & 0x7F);
    V.alg = std::min<int>(b[0x2C], 87); for (int i = 0; i < 8; i++) V.corr[i] = b[0x2D + i] & 0xF; V.fb = b[0x3D] & 7;
    for (int o = 0; o < 8; o++) {
        const uint8_t* p = b + 112 + o * 62; OpV& v = V.v[o];
        v.keysync = p[0] >> 6 & 1; v.transpose = (p[0] & 0x3F) - 24;
        v.coarse = p[1] & 31; v.fine = p[2]; v.notescale = p[3];
        v.bwbias = (p[4] >> 3 & 0xF); v.form = p[4] & 7;
        v.fixed = p[5] >> 6 & 1; v.skirt = p[5] >> 3 & 7; v.fseqtrk = p[5] & 7;
        v.bw = p[6]; v.detune = p[7];
        v.fegInit = p[8] - 50; v.fegAtt = p[9] - 50; v.fegAttT = p[10]; v.fegDecT = p[11];
        for (int i = 0; i < 4; i++) { v.L[i] = p[12 + i]; v.T[i] = p[16 + i]; }
        v.hold = p[20]; v.tscale = p[21] & 7; v.level = p[22]; v.bp = p[23]; v.ld = p[24]; v.rd = p[25]; v.lc = p[26] & 3; v.rc = p[27] & 3;
        v.fbias = (p[31] >> 3 & 0xF); v.pms = p[31] & 7;
        v.fms = p[32] >> 4 & 7; v.fmsb = p[32] & 0xF;
        v.ams = p[33] >> 4 & 7; v.amsb = p[33] & 0xF; v.egbias = (p[34] & 0xF) - 7;
        const uint8_t* q = p + 35; OpU& u = V.u[o];
        u.transpose = (q[0] & 0x3F) - 24; u.mode = q[1] >> 5 & 3; u.coarse = q[1] & 0x1F; u.fine = q[2]; u.notescale = q[3];
        u.bw = q[4]; u.bwbias = q[5] & 0xF; u.res = q[6] >> 3 & 7; u.skirt = q[6] & 7;
        u.fegInit = q[7] - 50; u.fegAtt = q[8] - 50; u.fegAttT = q[9]; u.fegDecT = q[10];
        u.level = q[11]; u.lks = (q[12] & 0xF) - 7;
        for (int i = 0; i < 4; i++) { u.L[i] = q[13 + i]; u.T[i] = q[17 + i]; }
        u.hold = q[21]; u.tscale = q[22] & 7;
        u.fbias = (q[23] & 0xF); u.fms = q[24] >> 4 & 7; u.fmsb = q[24] & 0xF;
        u.ams = q[25] >> 4 & 7; u.amsb = q[25] & 0xF; u.egbias = (q[26] & 0xF) - 7;
    }
}
static void init_blank_voice(uint8_t* b) {
    memset(b, 0, 608); memcpy(b, "Init      ", 10);
    b[0x1E] = 24; b[0x1F] = b[0x20] = b[0x21] = b[0x22] = b[0x3E] = 50; b[0x2C] = 0; b[0x3D] = 0;
    for (int o = 0; o < 8; o++) {
        uint8_t* p = b + 112 + o * 62;
        p[0] = 24; p[1] = 1; p[4] = 7 << 3; p[5] = (uint8_t)o; p[7] = 15; p[8] = p[9] = 50;
        p[12] = 99; p[13] = 99; p[14] = 99; p[15] = 0; p[16] = 0; p[17] = 50; p[18] = 50; p[19] = 40;
        p[22] = 0; p[23] = 39; p[31] = 7 << 3; p[32] = 7; p[33] = 7; p[34] = 7;
        uint8_t* q = p + 35; q[0] = 24; q[7] = q[8] = 50; q[12] = 7; q[23] = 7; q[24] = 7; q[25] = 7; q[26] = 7;
    }
}
static void init_default_voice(Voice& V) {
    init_blank_voice(V.raw); uint8_t* b = V.raw;
    memcpy(b, "InitEP    ", 10); b[0x2C] = 12; b[0x3D] = 5;
    auto op = [&](int o) { return b + 112 + o * 62; };
    op(2)[22] = 70; op(2)[1] = 14; op(2)[13] = 0; op(2)[17] = 30;
    op(3)[22] = 99;
    op(4)[22] = 78; op(4)[1] = 1;  op(4)[13] = 60; op(4)[17] = 55;
    op(5)[22] = 95;
    decode_voice(V);
}
// DX7 VCED (155 bytes) -> native voice: algorithm k -> k+8, DX op 6..1 -> FS op 3..8
static void convert_dx7(const uint8_t* v, uint8_t* out) {
    init_blank_voice(out); memcpy(out, v + 145, 10);
    out[0x2C] = (uint8_t)std::min(v[134] + 8, 87); out[0x3D] = v[135] & 7;
    out[0x11] = v[137]; out[0x12] = v[138]; out[0x15] = v[139]; out[0x16] = v[140]; out[0x13] = v[141] & 1; out[0x10] = (uint8_t)std::min<int>(v[142], 5);
    out[0x1E] = (uint8_t)std::min<int>(v[144], 48);
    out[0x1F] = v[133]; out[0x20] = v[130]; out[0x21] = v[131]; out[0x3E] = v[132]; out[0x22] = v[133];
    for (int i = 0; i < 4; i++) out[0x23 + i] = (uint8_t)(99 - std::min<int>(v[126 + i], 99));
    for (int j = 0; j < 6; j++) {
        const uint8_t* d = v + j * 21; uint8_t* p = out + 112 + (2 + j) * 62;
        for (int i = 0; i < 4; i++) { p[16 + i] = (uint8_t)(99 - std::min<int>(d[i], 99)); p[12 + i] = (uint8_t)std::min<int>(d[4 + i], 99); }
        p[23] = d[8]; p[24] = d[9]; p[25] = d[10]; p[26] = d[11] & 3; p[27] = d[12] & 3; p[21] = d[13] & 7;
        int ams = std::min<int>(d[14], 3) * 2, ts = std::min<int>(d[15], 7);
        p[33] = (uint8_t)(ams << 4 | (ts + 7)); p[22] = (uint8_t)std::min<int>(d[16], 99);
        int fixed = d[17] & 1, coarse = d[18] & 31, fine = std::min<int>(d[19], 99);
        if (fixed) {
            double hz = pow(10.0, coarse % 4) * pow(10.0, fine / 100.0);
            double x = log2(hz / 440.0) + 16; int c = (int)floor(x); int f = (int)lround((x - c) * 128);
            if (f > 127) { f = 0; c++; }
            coarse = clampi(c, 0, 31); fine = clampi(f, 0, 127);
        }
        p[1] = (uint8_t)coarse; p[2] = (uint8_t)fine; p[5] = (uint8_t)(fixed << 6 | (2 + j));
        p[7] = (uint8_t)clampi((d[20] - 7) * 2 + 15, 0, 30);
        p[0] = (uint8_t)((v[136] & 1) << 6 | 24);
        p[31] = (uint8_t)(7 << 3 | (v[143] & 7));
    }
}

// ------------------------------------------------------------------------------------------ performance / part / Fseq
struct Fseq {
    bool valid = false; char name[9] = "";
    int nframes = 0, loopStart = 0, loopEnd = 0, loopMode = 0, speedAdj = 64, velTempo = 0, pitchMode = 0, noteAssign = 60, tuning = 63, delay = 0, endStep = 0;
    uint8_t frame[512][50];   // pitch hi/lo, 8 voiced freq hi, 8 lo, 8 voiced level, 8 unvoiced hi, 8 lo, 8 unvoiced level
    void from_bytes(const uint8_t* h, const uint8_t* f, int frames) {
        memcpy(name, h, 8); name[8] = 0;
        loopStart = h[0x10] << 7 | h[0x11]; loopEnd = h[0x12] << 7 | h[0x13]; loopMode = h[0x14] & 1; speedAdj = h[0x15] & 0x7F;
        velTempo = h[0x16] & 7; pitchMode = h[0x17] & 1; noteAssign = h[0x18] & 0x7F; tuning = h[0x19] & 0x7F; delay = h[0x1A];
        nframes = std::min(frames, 128 * ((h[0x1B] & 3) + 1)); endStep = std::min(nframes - 1, h[0x1E] << 7 | h[0x1F]);
        memcpy(frame, f, (size_t)nframes * 50); valid = true;
    }
};
struct Part {
    uint8_t p[52]; Voice voice;
    // controller state (MIDI)
    int bend = 0;            // (msb - 64) * 16, the firmware keeps the MSB only
    int expr = 254;          // DAT_010297fa
    int src[14] = {};        // controller sources KN1-4 MC1-4 FC BC MW CAT PAT PB, 0..127
    int held[32]; int nheld = 0; int lastPitch = -1;   // mono handling and portamento start
    bool sustain = false;
    int rcv() const { return p[4]; }
};
struct Perf {
    uint8_t c[80]; uint8_t fx[112]; Part part[4];
    char name[13];
};
static void init_perf(Perf& P) {
    memset(P.c, 0, 80); memset(P.fx, 0, 112); memcpy(P.c, "Init        ", 12); P.c[0x10] = 127; P.c[0x11] = 64; P.c[0x12] = 24;
    P.c[0x18] = 7; P.c[0x19] = 104;   // Fseq speed ratio 1000 = 100 %
    P.c[0x21] = 2;
    for (int i = 0; i < 4; i++) {
        Part& pt = P.part[i]; memset(pt.p, 0, 52);
        pt.p[0] = 8; pt.p[1] = 1; pt.p[3] = 0x7F; pt.p[4] = i ? 0x7F : 0x10; pt.p[5] = 1; pt.p[8] = 24; pt.p[9] = 64; pt.p[10] = 64; pt.p[11] = 127; pt.p[12] = 64; pt.p[13] = 64;
        pt.p[14] = 64; pt.p[15] = 0; pt.p[16] = 127; pt.p[17] = 127; pt.p[0x15] = pt.p[0x16] = pt.p[0x17] = 64;
        for (int k = 0x18; k <= 0x23; k++) pt.p[k] = 64;
        pt.p[0x25] = 0; pt.p[0x26] = 0x40 + 2; pt.p[0x27] = 0x40 - 2; pt.p[0x2A] = 1; pt.p[0x2B] = 127; pt.p[0x2D] = 1; pt.p[0x2E] = pt.p[0x2F] = 64;
        init_default_voice(pt.voice);
    }
    memcpy(P.name, P.c, 12); P.name[12] = 0;
}

// ------------------------------------------------------------------------------------------ channel state
struct EG {                      // amplitude EG on the chip: hold, 4 segments. INFERRED shape/timing, DX7 style
    int stage = 5; double cur = -200, target = -200, rate = 0, holdLeft = 0; bool rising = false;
    int L[4] = {}, R[4] = {}; int hold = 0; int rs = 0;
    static double lvl_db(int a) { return a >= 63 ? -200.0 : -1.5 * a; }   // 6-bit attenuation (LEVTAB >> 1), 1.5 dB per step INFERRED
    void start(const int* lv, const int* rt, int h, int rateScale) {
        for (int i = 0; i < 4; i++) { L[i] = lv[i]; R[i] = rt[i]; } hold = h; rs = rateScale; cur = lvl_db(L[3]); stage = 0;
        holdLeft = h ? rate_secs(std::min(63, std::min(egrate(h) + 4, 0x3E) + rs)) * SR : 0;   // firmware: hold rate + 4 capped 0x3E
        if (holdLeft < 1) next(1);
    }
    void next(int s) {
        stage = s; if (s > 4) { target = -200; return; }
        target = lvl_db(L[s - 1]);
        double secs = rate_secs(std::min(63, R[s - 1] + rs));
        rising = target > cur;
        rate = rising ? 1.0 - exp(-1.0 / (secs * 0.25 * SR + 1)) : 96.0 / (secs * SR + 1);
    }
    void release() { if (stage < 4) next(4); }
    inline double tick() {
        if (stage == 0) { if (--holdLeft <= 0) next(1); return cur; }
        if (stage > 4) return cur;
        if (rising) { cur += (target + 6 - cur) * rate; if (cur >= target) { cur = target; if (stage < 3) next(stage + 1); else if (stage == 4) stage = 5; } }
        else { cur -= rate; if (cur <= target) { cur = target; if (stage < 3) next(stage + 1); else if (stage == 4) stage = 5; } }
        return cur;
    }
    bool done() const { return stage == 5 || (stage == 4 && cur <= -120); }
};
struct FreqEG {                  // INFERRED: init -> attack level -> 0, +-50 = +-4 octaves, EG time curve as the amplitude EG
    double cur = 0, target = 0, k = 0, kdec = 0; int stage = 2;
    void start(int init, int att, int attT, int decT) {
        cur = init * 48.0 / 50.0; target = att * 48.0 / 50.0; stage = (init == 0 && att == 0) ? 2 : 0;
        k = 1.0 - exp(-1.0 / (rate_secs(egrate(attT)) * 0.3 * SR + 1)); kdec = 1.0 - exp(-1.0 / (rate_secs(egrate(decT)) * 0.3 * SR + 1));
    }
    inline double tick() {
        if (stage == 0) { cur += (target - cur) * k; if (fabs(target - cur) < 0.01) stage = 1; }
        else if (stage == 1) { cur += (0 - cur) * kdec; if (fabs(cur) < 0.001) { cur = 0; stage = 2; } }
        return cur;
    }
};
struct WinGen { double w = 1.0, c = 0.0; bool on = false; };
struct OpState {
    double phase = 0; WinGen g[2]; int nextGen = 0; double fphase = 0; int halfCount = 0;
    EG eg; FreqEG feg;
    EG ueg; FreqEG ufeg; double nphase = 0; double lp[8] = {}; uint32_t rng = 0x12345678;
};
struct Chan {
    bool active = false; int part = 0, note = 0, vel = 0; bool held = false, sustained = false; uint32_t age = 0;
    // CPU side, computed at note-on
    int noteP = 60;            // note after all shifts (DAT_0103937a)
    int pitchNote = 0;         // NOTETAB + detune + tune (DAT_01028a84)
    int keyfact = 0;           // KEYFACT >> 2
    int levelOff[8], ulevelOff[8], freqWord[8], ufreqWord[8], frmtWord[8], bwReg[8], ubwReg[8];
    int egbias[8], uegbias[8]; // image 0x1C0/0x1C8
    int egL[8][4], egR[8][4], egHold[8], uegL[8][4], uegR[8][4], uegHold[8];
    // pitch EG (software, FUN_00028c8e)
    int pegStage = 5, pegCur = 0, pegTarget = 0, pegRate = 255, velP = 128;
    int pegLvl[5], pegRt[5];
    // portamento (FUN_00029048)
    int portaCur = 0, portaTarget = 0, portaRate = 0; bool portaOn = false;
    // LFO1 (FUN_00028828)
    uint32_t lfoPhase = 0, lfoDelay = 0, lfoFade = 0; int lfoVal = 0; int lfoSH = 0;
    // registers refreshed each tick
    int regPitch = 0, regPM = 0, regFM = 0, regAM = 0, regLevel[8], regULevel[8], regC0 = 73;
    int fqWord[8] = {}, fquWord[8] = {}; double partV = 1, partU = 1;
    OpState op[8]; double fbBus = 0, fbPrev = 0;
    bool fseqOp[8] = {}, fseqUOp[8] = {};
};

struct Synth {
    Perf perf; Chan ch[NCHAN]; uint32_t clock = 0; double gain = 0.25;
    int sysTune = 64, sysNoteShift = 64, velCurve = 0;
    std::mutex mtx;
    // Fseq playback
    Fseq fseq; bool fseqRun = false; double fseqAcc = 0, fseqPeriod = 0.01; int fseqStep = 0, fseqDir = 1; int fseqVel = 100; int fseqPart = -1;
    double tickAcc = 0;

    Synth() { init_perf(perf); }

    // ---------------------------------------------------------------- per-part derived values
    int ctrl_offset(int part, int dest) const {          // controller sets -> offset on the 0..255 scale (INFERRED scaling: value*depth/64)
        int sum = 0;
        for (int s = 0; s < 8; s++) {
            if (!(perf.c[0x28 + s] & (1 << part))) continue;
            if ((perf.c[0x40 + s] & 0x3F) != dest) continue;
            int bits = perf.c[0x30 + 2 * s] << 7 | perf.c[0x31 + 2 * s]; int depth = perf.c[0x48 + s] - 64;
            for (int b = 0; b < 14; b++) if (bits & (1 << b)) sum += (perf.part[part].src[b] * depth) >> 6;   // source bit order: KN1..MC4, FC, BC, MW, CAT, PAT, PB (assumed)
        }
        return sum;
    }
    int bend_word(const Part& pt) const {                // FUN_00020194 + pitch bias controller (dest 34)
        int b = pt.bend; int r = (b < 0 ? pt.p[0x27] : pt.p[0x26]) - 0x40; int v;
        if (r < 0) { v = BENDTAB[std::min(48, -r)]; if (b >= 0) v = -v; } else { v = BENDTAB[std::min(48, r)]; if (b < 0) v = -v; }
        int k = b >> 3; if (k == 0x7F) k = 0x80;
        int w = (v * k) >> 7;
        int bias = clampi(ctrl_offset((int)(&pt - perf.part), 34) / 4, -24, 24);
        w += bias < 0 ? -BENDTAB[-bias] : BENDTAB[bias];
        return w;
    }
    void part_levels(int part, int& vAtt, int& uAtt) const {   // FUN_00020044 -> registers 0x228/0x229 (+0x10 dropped here)
        const Part& pt = perf.part[part];
        int expr = clampi(pt.expr + ctrl_offset(part, 17), 0, 254);
        int u1 = ((pt.p[0x0B] + 1) * expr) >> 8; int iu = u1 + 1, iv = u1 + 1;
        int bal = clampi(pt.p[0x0A] + ctrl_offset(part, 31) / 2, 0, 127);
        if (bal < 0x40) iu = ((u1 + 2) * bal) >> 6; else if (bal > 0x40) iv = ((u1 + 2) * ((bal ^ 0x7F) + 1)) >> 6;
        vAtt = VNBAL[clampi(iv, 0, 127)]; uAtt = VNBAL[clampi(iu, 0, 127)];
    }

    // ---------------------------------------------------------------- note on (FUN_00010b48 / FUN_000112c0 / FUN_000125f8)
    void note_on(int part, int note, int vel) {
        Part& pt = perf.part[part]; const Voice& V = pt.voice;
        int lo = pt.p[0x0F], hi = pt.p[0x10];
        if (hi < lo ? (note > hi && note < lo) : (note > hi || note < lo)) return;
        lo = pt.p[0x2A]; hi = pt.p[0x2B];
        if (hi < lo ? (vel > hi && vel < lo) : (vel > hi || vel < lo)) return;
        vel = clampi(((pt.p[0x0C] * VELCURVES[clampi(velCurve, 0, 4) * 128 + vel]) >> 6) + (pt.p[0x0D] - 0x40) * 2, 1, 127);
        bool mono = pt.p[5] == 0;
        // mono: remember held notes, choose the sounding note by priority
        if (mono) {
            if (pt.nheld < 32) pt.held[pt.nheld++] = note;
            int prio = pt.p[6] & 3, target = note;
            if (prio == 1) { target = -1; for (int i = 0; i < pt.nheld; i++) target = std::max(target, pt.held[i]); }
            else if (prio == 2) { target = 128; for (int i = 0; i < pt.nheld; i++) target = std::min(target, pt.held[i]); }
            else if (prio == 3) target = pt.held[0];
            Chan* cur = nullptr; for (auto& c : ch) if (c.active && c.part == part && c.held) cur = &c;
            if (cur && pt.nheld > 1) {          // legato: retune the sounding channel (FUN_000119d0), no retrigger
                if (target != cur->note) retune(*cur, target);
                return;
            }
            note = target;
        }
        Chan* c = nullptr;
        for (auto& x : ch) if (!x.active) { c = &x; break; }
        if (!c) { c = &ch[0]; for (auto& x : ch) if (x.age < c->age) c = &x; }
        Chan& C = *c; bool sync = V.lfo1sync != 0; uint32_t keepPhase = C.lfoPhase;
        C = Chan(); C.active = true; C.part = part; C.note = note; C.vel = vel; C.held = true; C.age = ++clock;
        C.lfoPhase = sync ? 0 : keepPhase;
        compute_pitch(C, pt, note);
        // portamento start (FUN_000124fe)
        int porta = pt.p[0x24]; C.portaTarget = C.pitchNote; C.portaCur = C.pitchNote;
        if ((porta & 1) && pt.p[0x25] && pt.lastPitch >= 0 && ((porta & 2) || (mono && pt.nheld > 1))) C.portaCur = pt.lastPitch;
        C.portaOn = (porta & 1) && pt.p[0x25]; C.portaRate = PEGTIME[clampi((pt.p[0x25] * 100) >> 7, 0, 99)];
        pt.lastPitch = C.pitchNote;
        setup_ops(C, pt, vel);
        setup_peg(C, pt, vel);
        // Fseq trigger (FUN_0000fffa): first note only, or every note
        if (fseq.valid && fseqPart == part && ((perf.c[0x24] & 1) || !anyOther(C))) fseq_start(vel);
        refresh_regs(C, pt);
        if (getenv("FS1R_DEBUG")) {
            printf("note %d vel %d -> note' %d pitch %d keyfact %d C0 %d\n", note, vel, C.noteP, C.pitchNote, C.keyfact, C.regC0);
            for (int o = 0; o < 8; o++) printf("  op%d lvl %3d ulvl %3d egb %3d freq %5d ufreq %5d frmt %5d bw %2d egL %2d %2d %2d %2d egR %2d %2d %2d %2d hold %d\n", o + 1, C.levelOff[o], C.ulevelOff[o], C.egbias[o], C.freqWord[o], C.ufreqWord[o], C.frmtWord[o], C.bwReg[o], C.egL[o][0], C.egL[o][1], C.egL[o][2], C.egL[o][3], C.egR[o][0], C.egR[o][1], C.egR[o][2], C.egR[o][3], C.egHold[o]);
            printf("  peg lvl %d %d %d %d %d rate %d %d %d %d %d velP %d\n", C.pegLvl[0], C.pegLvl[1], C.pegLvl[2], C.pegLvl[3], C.pegLvl[4], C.pegRt[0], C.pegRt[1], C.pegRt[2], C.pegRt[3], C.pegRt[4], C.velP);
            int sp = eb86(clampi(V.lfo1speed + pt.p[0x15] - 64, 0, 99)); uint32_t inc = (sp == 0 ? 0xB : sp * (sp < 0xA0 ? 0xB : 0xB + ((sp - 0xA0) >> 2))) << 1;
            int q = 99 - clampi(V.lfo1delay + pt.p[0x17] - 64, 0, 99); uint32_t dinc = ((16 + (q & 15)) << 10) >> (7 - (q >> 4));
            printf("  lfo wave %d speed %d -> inc %u (%.2f Hz) delay %d -> inc %u (%.2f s) pmd %d amd %d fmd %d porta %d rate %d\n", V.lfo1wave, V.lfo1speed, inc, inc * TICK_HZ / 65536.0, V.lfo1delay, dinc, 65536.0 / (dinc ? dinc : 1) / TICK_HZ, V.pmd, V.amd, V.fmd, C.portaOn, C.portaRate);
        }
        for (int o = 0; o < 8; o++) {
            OpState& s = C.op[o]; const OpV& v = V.v[o]; const OpU& u = V.u[o];
            int rs = (v.tscale * clampi(C.regC0 - 80, 0, 31)) >> 3;   // INFERRED rate scaling: 0xC0 = pitch>>8 + 10 is note/3 + 10, DX7 uses tscale*(note/3-7)>>3
            s.eg.start(C.egL[o], C.egR[o], C.egHold[o], rs);
            s.feg.start(v.fegInit, v.fegAtt, v.fegAttT, v.fegDecT);
            s.phase = v.keysync ? 0.0 : (double)rand() / RAND_MAX;
            s.ueg.start(C.uegL[o], C.uegR[o], C.uegHold[o], (u.tscale * std::max(0, C.regC0 - 76)) >> 3);
            s.ufeg.start(u.fegInit, u.fegAtt, u.fegAttT, u.fegDecT);
            s.rng = 0x9E3779B9u * (o + 1) ^ clock; if (!s.rng) s.rng = 1;
        }
    }
    bool anyOther(const Chan& C) const { for (auto& x : ch) if (&x != &C && x.active && x.held) return true; return false; }

    // FUN_00010dc4: note shifts, note table, part detune, master tune
    void compute_pitch(Chan& C, const Part& pt, int note) {
        int n = clampi(note + (sysNoteShift & 0x7F) - 0x40, 0, 127);
        n = clampi(n + (perf.c[0x12] & 0x3F) - 24, 0, 127);
        n = clampi(n + (pt.p[8] & 0x3F) - 24, 0, 127);
        n = clampi(n + (pt.voice.raw[0x1E] & 0x3F) - 24, 0, 127);
        C.noteP = n; C.keyfact = KEYFACT[n] >> 2;
        int pitch = NOTETAB[n];
        int d = pt.p[9] - 0x40;
        if (d) { int band = (n >> 3) & 0xF; int prod = std::abs(d) * (15 - band); if (!prod) prod = 1; pitch += (d < 0 ? -prod : prod) / 15; }
        pitch += (sysTune & 0x7F) - 0x40;
        C.pitchNote = pitch;
    }
    void retune(Chan& C, int note) {                    // mono legato (FUN_000119d0): new pitch, glide from the current one, EGs keep running
        const Part& pt = perf.part[C.part];
        C.note = note; compute_pitch(C, pt, note);
        C.portaTarget = C.pitchNote; if (!C.portaOn) C.portaCur = C.pitchNote;
        perf.part[C.part].lastPitch = C.pitchNote;
        setup_ops(C, pt, C.vel); refresh_regs(C, pt);
    }
    // per-operator note-dependent values: levels (FUN_00012a32/FUN_00012fcc/FUN_00012e2c), frequency words (FUN_0001e838/FUN_000135d8),
    // formant transpose words (FUN_00013bc6), bandwidth registers (FUN_0001ba06), EG rates with part offsets
    void setup_ops(Chan& C, const Part& pt, int vel) {
        const Voice& V = pt.voice; int n = C.noteP; int pm = C.pitchNote - 0x53AA; int part = C.part;
        int ctrlV = ctrl_offset(part, 37), ctrlU = ctrl_offset(part, 38), ctrlBias = clampi(ctrl_offset(part, 35), 0, 255);
        int band = (C.pitchNote >> 8) + 10; band = band < 0x50 ? 0 : band < 0x70 ? ((band ^ 0x10) & 0x1F) : 0x1F;
        int fv = (fseq.valid && fseqPart == part) ? V.fseqV : 0, fu = (fseq.valid && fseqPart == part) ? V.fseqU : 0;
        for (int o = 0; o < 8; o++) {
            const OpV& v = V.v[o]; const OpU& u = V.u[o];
            C.fseqOp[o] = (fv >> o) & 1; C.fseqUOp[o] = (fu >> o) & 1;
            // voiced level offset: velocity + level + key scaling (0.375 dB units)
            int ks;
            {
                int base = LEVTAB[std::min(99, v.level)];
                int dist = keygroup(n) - keygroup(v.bp + 21) + 1;
                int att;
                if (dist <= 0) { const unsigned char* t = (v.lc == 1 || v.lc == 2) ? KSEXP : KSLIN; int x = std::min(127, (t[std::min(39, -dist)] * eb86(v.ld)) >> 8); att = (v.lc == 2 || v.lc == 3) ? base - x : base + x; }
                else { const unsigned char* t = (v.rc == 1 || v.rc == 2) ? KSEXP : KSLIN; int x = std::min(127, (t[std::min(39, dist)] * eb86(v.rd)) >> 8); att = (v.rc == 2 || v.rc == 3) ? base - x : base + x; }
                ks = clampi(att, 0, 127) << 1;
            }
            C.levelOff[o] = C.fseqOp[o] ? 0 : std::min(255, vel_att(v.amsb, vel) + ks);
            // unvoiced: 2*LEVTAB + velocity, then level key scaling
            {
                int a = std::min(255, LEVTAB[std::min(99, u.level)] * 2 + vel_att(u.amsb, vel));
                a += -(u.lks * 64 * (n - 60)) >> 8;
                C.ulevelOff[o] = C.fseqUOp[o] ? 0 : clampi(a, 0, 255);
            }
            // EG bias attenuation (event 0x205): |sens| * EGBIAS[ctrl or 255-ctrl] >> 3
            auto bias = [&](int s) { if (!s) return 0; int idx = s < 0 ? 255 - ctrlBias : ctrlBias; return std::min(255, (std::abs(s) * EGBIAS[idx]) >> 3); };
            C.egbias[o] = bias(v.egbias); C.uegbias[o] = bias(u.egbias);
            // frequency words
            if (v.form == 7 || v.fixed) C.freqWord[o] = 8 * (v.coarse * 128 + v.fine) + 0x28ED + keytrack(v.notescale, pm) + fvs_term(v.fmsb, vel);
            else C.freqWord[o] = 0x1243 + COARSE[v.coarse] + FINE[std::min(99, v.fine)];
            C.ufreqWord[o] = std::min(0x7F00, ((u.coarse & 0x1F) * 256 + u.fine * 2) * 4 + 0x28ED + keytrack(u.notescale, pm) + fvs_term(u.fmsb, vel));
            // formant transpose word (register 0x230): 0x1243 + TRANS + note dependent detune for frmt ops, else the raw byte 6
            if (v.form == 7) {
                int d = v.detune; int dd = d < 15 ? (~d) : d - 15; int idx = (dd & 0x1F) * 32 + band; int det = FRMDET[idx & 0x1FF]; if (idx & 0x200) det = -det;
                C.frmtWord[o] = 0x1243 + TRANS[clampi(v.transpose + 24, 0, 48)] + det;
            } else C.frmtWord[o] = v.bw;
            // bandwidth registers: base bw + controller * bias
            int bo = BWBIAS[v.bwbias & 0xF] ? clampi((ctrlV * BWBIAS[v.bwbias & 0xF]) >> 3, -128, 127) : 0;
            C.bwReg[o] = clampi(v.bw + bo, 0, 99);
            int ubo = BWBIAS[u.bwbias & 0xF] ? clampi((ctrlU * BWBIAS[u.bwbias & 0xF]) >> 3, -128, 127) : 0;
            C.ubwReg[o] = clampi(eb70(u.bw) + ubo, 0, 127);
            // EG registers: levels LEVTAB >> 1, rates ((99-T)*0xA4)>>8, part EG offsets on attack/decay/release (assumed T1/T2/T4)
            int offs[4] = {pt.p[0x1A] - 64, pt.p[0x1B] - 64, 0, pt.p[0x1C] - 64};
            for (int i = 0; i < 4; i++) {
                C.egL[o][i] = LEVTAB[std::min(99, v.L[i])] >> 1; C.egR[o][i] = egrate(v.T[i] + offs[i]);
                C.uegL[o][i] = LEVTAB[std::min(99, u.L[i])] >> 1; C.uegR[o][i] = egrate(u.T[i] + offs[i]);
            }
            C.egHold[o] = v.hold; C.uegHold[o] = u.hold;
        }
    }
    // FUN_00026c5a: pitch EG per note. Levels PEGLVL (v-128)<<7 * velocity >> range, rates PEGTIME scaled by velocity and key
    void setup_peg(Chan& C, const Part& pt, int vel) {
        const Voice& V = pt.voice;
        C.velP = V.pegVel ? vel + 1 : 128;        // firmware overrides the graded PEGVEL table with this (see docs)
        int Lp[5], Tp[5];
        for (int i = 0; i < 5; i++) Lp[i] = V.pegL[i];
        Lp[0] = clampi(Lp[0] + pt.p[0x20] - 64, 0, 100); Lp[4] = clampi(Lp[4] + pt.p[0x22] - 64, 0, 100);
        Tp[0] = 255; for (int i = 0; i < 4; i++) Tp[i + 1] = V.pegT[i];
        Tp[1] = clampi(Tp[1] + pt.p[0x21] - 64, 0, 99); Tp[4] = clampi(Tp[4] + pt.p[0x23] - 64, 0, 99);
        for (int i = 0; i < 5; i++) {
            int w = ((int)PEGLVL[clampi(Lp[i], 0, 100)] - 128) << 7;
            w = (w * C.velP) >> 7; if (V.pegRange) w >>= V.pegRange + 1;
            C.pegLvl[i] = w;
            int r = i ? PEGTIME[clampi(Tp[i], 0, 99)] : 255;
            C.pegRt[i] = r == 255 ? 255 : std::min(254, ((r * (C.velP + 1)) >> 7) + std::min(255, V.pegTscale * C.keyfact));
        }
        if (C.pegRt[1] == 255) { C.pegStage = 1; C.pegCur = C.pegLvl[1]; C.pegTarget = C.pegLvl[2]; C.pegRate = C.pegRt[2]; }
        else { C.pegStage = 0; C.pegCur = C.pegLvl[0]; C.pegTarget = C.pegLvl[1]; C.pegRate = C.pegRt[1]; }
    }
    void note_off(int part, int note) {
        Part& pt = perf.part[part];
        if (pt.p[5] == 0) {                       // mono
            for (int i = 0; i < pt.nheld; i++) if (pt.held[i] == note) { memmove(pt.held + i, pt.held + i + 1, (pt.nheld - i - 1) * sizeof(int)); pt.nheld--; break; }
            Chan* cur = nullptr; for (auto& c : ch) if (c.active && c.part == part && c.held) cur = &c;
            if (!cur) return;
            if (pt.nheld > 0) {                   // another key still down: go back to it (legato)
                int prio = pt.p[6] & 3, target = pt.held[pt.nheld - 1];
                if (prio == 1) { target = -1; for (int i = 0; i < pt.nheld; i++) target = std::max(target, pt.held[i]); }
                else if (prio == 2) { target = 128; for (int i = 0; i < pt.nheld; i++) target = std::min(target, pt.held[i]); }
                else if (prio == 3) target = pt.held[0];
                if (target != cur->note) retune(*cur, target);
                return;
            }
            cur->held = false; if (pt.sustain && (pt.p[0x2D] & 1)) { cur->sustained = true; return; }
            release(*cur); return;
        }
        for (auto& c : ch) if (c.active && c.part == part && c.note == note && c.held) {
            c.held = false;
            if (pt.sustain && (pt.p[0x2D] & 1)) { c.sustained = true; continue; }
            release(c);
        }
    }
    void release(Chan& C) {                      // FUN_000236e8: key off -> EG release, PEG stage 4 toward L4 at T4
        for (auto& s : C.op) { s.eg.release(); s.ueg.release(); }
        C.pegStage = 4; C.pegTarget = C.pegLvl[4]; C.pegRate = C.pegRt[4];
    }
    void set_sustain(int part, bool on) { Part& pt = perf.part[part]; pt.sustain = on; if (!on) for (auto& c : ch) if (c.active && c.part == part && c.sustained) { c.sustained = false; release(c); } }
    void all_off() { for (auto& c : ch) c.active = false; for (auto& p : perf.part) { p.nheld = 0; } fseqRun = false; }
    void all_release() { for (auto& c : ch) if (c.active && (c.held || c.sustained)) { c.held = c.sustained = false; release(c); } for (auto& p : perf.part) p.nheld = 0; }

    // ---------------------------------------------------------------- Fseq (FUN_0000fffa / FUN_0001a59e)
    void fseq_start(int vel) {
        int ratio = clampi(perf.c[0x18] << 7 | perf.c[0x19], 100, 5000);
        int sens = perf.c[0x22] & 7;
        if (sens && ratio > 100) { int x = ((ratio - 100) * sens * (127 - vel)) / 7 / 127; ratio = clampi(ratio - x, 100, 5000); }
        ratio = clampi(ratio + ctrl_offset(fseqPart, 46) * 8, 100, 5000);
        double counts = (double)VELW[clampi(fseq.speedAdj, 0, 127)] * 84.0 * 1000.0 / ratio + 2884.0;
        fseqPeriod = counts * 32.0 / CPU_HZ;
        int start = clampi(perf.c[0x1A] << 7 | perf.c[0x1B], 0, fseq.nframes - 1);
        fseqStep = start; fseqDir = 1; fseqAcc = 0; fseqRun = true; fseqVel = vel;
    }
    void fseq_tick(double dt) {
        if (!fseqRun) return;
        fseqAcc += dt; if (fseqAcc < fseqPeriod) return;
        fseqAcc -= fseqPeriod;
        int ls = perf.c[0x1C] << 7 | perf.c[0x1D], le = perf.c[0x1E] << 7 | perf.c[0x1F];
        if (le <= ls) { ls = fseq.loopStart; le = fseq.loopEnd; }
        le = std::min(le, fseq.endStep); ls = std::min(ls, le);
        int mode = perf.c[0x20] & 1;
        if (mode == 0) { if (fseqStep < le) fseqStep++; }
        else {
            fseqStep += fseqDir;
            if (fseqStep >= le) { fseqStep = le; fseqDir = -1; } else if (fseqStep <= ls) { fseqStep = ls; fseqDir = 1; }
        }
    }

    // ---------------------------------------------------------------- tick: LFO, PEG, portamento, register refresh
    void lfo_tick(Chan& C, const Part& pt) {     // FUN_00028828
        const Voice& V = pt.voice; int part = C.part;
        int dly = clampi(V.lfo1delay + pt.p[0x17] - 64, 0, 99), q = 99 - dly;
        uint32_t dinc = ((16 + (q & 15)) << 10) >> (7 - (q >> 4));
        C.lfoDelay = std::min<uint32_t>(0xFFFF, C.lfoDelay + dinc);
        if (C.lfoDelay == 0xFFFF) C.lfoFade = std::min<uint32_t>(0xFFFF, C.lfoFade + dinc);
        int sp = eb86(clampi(V.lfo1speed + pt.p[0x15] - 64, 0, 99)) + clampi(ctrl_offset(part, 43) * 2, -255, 255);
        sp = clampi(sp, 0, 255);
        uint32_t inc = sp == 0 ? 0xB : sp * (sp < 0xA0 ? 0xB : 0xB + ((sp - 0xA0) >> 2)); inc = (inc & 0xFFFF) << 1;
        uint32_t ph = C.lfoPhase + inc; bool wrapped = ph > 0xFFFF; C.lfoPhase = ph & 0xFFFF;
        int hi = C.lfoPhase >> 8, val;
        switch (V.lfo1wave) {
        case 1: val = (int8_t)(~hi); break;
        case 2: val = (int8_t)hi; break;
        case 3: val = (hi & 0x80) ? -128 : 127; break;
        case 4: { int idx = hi & 0x3F; if (hi & 0x40) idx ^= 0x3F; int s = SINE64[idx]; val = (hi & 0x80) ? (int8_t)(~s) : s; break; }
        case 5: if (wrapped) { C.lfoSH = SHTAB[((C.lfoSH & 0xFF) * 0xB3 + (rand() & 0xFF)) & 0x7FF]; } val = C.lfoSH; break;
        default: { uint32_t t = (C.lfoPhase << 1); if (t > 0xFFFF) t = ~t & 0xFFFF; val = (int)((t >> 8) & 0xFF) - 0x80; }
        }
        C.lfoVal = val;
    }
    void peg_tick(Chan& C) {                     // FUN_00028c8e: linear moves of PEGTIME units per tick, stages 3 and 5 hold
        int s = C.pegStage; if (s == 3 || s == 5) return;
        if (s > 5) { C.pegStage = 3; return; }
        if (C.pegRate < 255) {
            if (C.pegCur < C.pegTarget) C.pegCur = std::min(C.pegTarget, C.pegCur + C.pegRate);
            else if (C.pegCur > C.pegTarget) C.pegCur = std::max(C.pegTarget, C.pegCur - C.pegRate);
            if (C.pegCur != C.pegTarget) return;
        } else C.pegCur = C.pegTarget;
        C.pegStage = s + 1;                       // next segment: rate T[s+2], target L[s+2] (stage 3 holds L3 until key off)
        if (s + 1 < 5) { C.pegRate = C.pegRt[std::min(4, s + 2)]; C.pegTarget = C.pegLvl[std::min(4, s + 2)]; }
    }
    void porta_tick(Chan& C) {                   // FUN_00029048
        if (!C.portaOn || !C.portaRate) { C.portaCur = C.portaTarget; return; }
        int d = C.portaTarget - C.portaCur; if (!d) return;
        if (d < 0) { int n = C.portaCur - (((-d) >> 10) + 1) * C.portaRate; C.portaCur = std::max(C.portaTarget, n); }
        else { int n = C.portaCur + ((d >> 10) + 1) * C.portaRate; C.portaCur = std::min(C.portaTarget, n); }
    }
    void refresh_regs(Chan& C, const Part& pt) {  // FUN_0002c798 / FUN_0002c214 / FUN_0002ad72 / FUN_0002b458
        const Voice& V = pt.voice; int part = C.part;
        int fseqPitch = 0;
        if (fseqRun && fseqPart == part && !(perf.c[0x23] & 1) && fseq.valid) {
            const uint8_t* f = fseq.frame[fseqStep];
            fseqPitch = (f[0] * 256 + f[1] * 2) - (NOTETAB[fseq.noteAssign & 0x7F] + 0x1243) + (fseq.tuning - 63);
        }
        C.regPitch = C.portaCur + (C.pegCur >> 2) + bend_word(pt) + fseqPitch;
        C.regC0 = (C.pitchNote >> 8) + 10;
        int fade = C.lfoFade >> 8;
        int pmd = clampi(eb86(clampi(V.pmd + pt.p[0x16] - 64, 0, 99)) + ctrl_offset(part, 39), 0, 255);
        C.regPM = clampi((C.lfoVal * ((pmd * fade) >> 8 & 0xFF)) >> 4, -0x7FF, 0x7FF);
        int fmd = clampi(eb86(V.fmd) + ctrl_offset(part, 41), 0, 255);
        C.regFM = clampi((C.lfoVal * ((fmd * fade) >> 8 & 0xFF)) >> 4, -0x7FF, 0x7FF);
        int amc = ctrl_offset(part, 40);
        int amd = clampi(eb86(V.amd) + amc, 0, 255), fadeA = clampi(fade + amc, 0, 255);
        C.regAM = EGBIAS[(((amd * fadeA) >> 8) * ((127 - C.lfoVal) & 0xFF)) >> 8];
        int lvVel = 0x80;                                     // Fseq level velocity multiplier (FUN_000125f8)
        if (fseqPart == part && perf.c[0x27] != 0x40) {
            int s = perf.c[0x27]; int mul;
            if (s & 0x40) mul = ((s & 0x3F) + 1) * VELW[VELCURVE[fseqVel] & 0x7F]; else mul = ((~s) & 0x3F) * VELW[127 - (VELCURVE[fseqVel] & 0x7F)];
            lvVel = (~(mul >> 6)) & 0xFF;
        }
        int trackOff = fseq.pitchMode == 0 ? C.pitchNote - NOTETAB[fseq.noteAssign & 0x7F] : 0;   // formant tracking of the key (assumed from FUN_000127fc)
        for (int o = 0; o < 8; o++) {
            int l = C.levelOff[o], ul = C.ulevelOff[o];
            if (fseqRun && fseqPart == part && fseq.valid) {
                const uint8_t* f = fseq.frame[fseqStep]; int t = V.v[o].fseqtrk;
                if (C.fseqOp[o]) { l = f[0x12 + t] << 1; if (lvVel != 0x80) l = (~(((~l) & 0xFF) * lvVel >> 8)) & 0xFF; C.fqWord[o] = f[2 + t] * 256 + f[0xA + t] * 2 + trackOff; }
                if (C.fseqUOp[o]) { ul = f[0x2A + t] << 1; if (lvVel != 0x80) ul = (~(((~ul) & 0xFF) * lvVel >> 8)) & 0xFF; C.fquWord[o] = f[0x1A + t] * 256 + f[0x22 + t] * 2 + trackOff; }
            }
            C.regLevel[o] = clampi(l + C.egbias[o], 0, 255);
            C.regULevel[o] = clampi(ul + C.uegbias[o], 0, 255);
        }
        int vAtt, uAtt; part_levels(part, vAtt, uAtt);
        C.partV = db2lin(-LEVEL_DB * vAtt); C.partU = db2lin(-LEVEL_DB * uAtt);
    }
    void tick() {
        double dt = 1.0 / TICK_HZ;
        fseq_tick(dt);
        for (auto& C : ch) {
            if (!C.active) continue;
            const Part& pt = perf.part[C.part];
            lfo_tick(C, pt); peg_tick(C); porta_tick(C); refresh_regs(C, pt);
        }
    }

    // ---------------------------------------------------------------- chip model (INFERRED beyond the register values)
    inline double op_sample(OpState& s, const OpV& v, double f0, double fop, int bw, double pm) {
        if (v.form == 0) { s.phase += fop / SR; if (s.phase >= 1) s.phase -= 1; return fsin(s.phase + pm); }
        double fw, fc, wl; bool dc = false;
        if (v.form == 7) { fw = f0; fc = fop; wl = 2.0 * pow(2.0, -bw / 20.0); }                       // formant: window at the fundamental (INFERRED bw curve)
        else if (v.form == 5 || v.form == 6) { fw = fop; fc = fop * (1 + bw * 31.0 / 99.0); wl = v.form == 5 ? 0.5 : 2.0; }
        else { fw = (v.form >= 3 ? 2 * fop : fop); fc = 0; dc = true; wl = (v.form & 1) ? 0.25 : 1.0; }
        wl = std::min(wl, 2.0);
        s.fphase += fw / SR;
        if (s.fphase >= 1) { s.fphase -= 1; WinGen& g = s.g[s.nextGen]; g.on = true; g.w = 0; g.c = 0; s.nextGen ^= 1; s.halfCount++; }
        double winc = fw / (wl * SR), y = 0;
        for (int k = 0; k < 2; k++) {
            WinGen& g = s.g[k]; if (!g.on) continue;
            g.w += winc; if (g.w >= 1) { g.on = false; continue; }
            if (dc) { double x = g.w + pm * 0.25; x -= floor(x); int sign = (v.form >= 3 && ((s.halfCount - k) & 1)) ? -1 : 1; y += fwin(v.skirt, x) * sign * 2.0; }
            else { y += fwin(v.skirt, g.w) * fsin(g.c + pm); g.c += fc / SR; }
        }
        return y;
    }
    inline double render_chan(Chan& C) {
        const Part& pt = perf.part[C.part]; const Voice& V = pt.voice; const unsigned char* alg = FS1R_ALG[V.alg];
        double partV = C.partV, partU = C.partU;
        bool fs = fseqRun && fseqPart == C.part && fseq.valid;
        double f0 = word_hz(C.regPitch + 0x1243 + C.regPM);          // channel fundamental incl. LFO pitch mod (pms 7)
        double Cb = 0, H = 0, S = 0, fbNew = 0, mix = 0;
        double fbGain = V.fb ? 0.5 * pow(2.0, V.fb - 7) : 0.0;        // INFERRED feedback scale
        for (int o = 0; o < 8; o++) {
            OpState& s = C.op[o]; const OpV& v = V.v[o];
            unsigned char t0 = alg[2 * o], t1 = alg[2 * o + 1]; int F = (t0 >> 3) & 7;
            double in = F == 3 ? C.fbBus * fbGain : F == 4 ? Cb : F == 5 ? H : F == 6 ? S : 0.0;
            if (F == 6) S = 0;
            double egdb = s.eg.tick();
            double att = C.regLevel[o] * LEVEL_DB + C.regAM * LEVEL_DB * v.ams / 7.0;   // INFERRED: ams scales the channel AM attenuation linearly
            if (t1 & 1) att += 1.5 * V.corr[o];                                            // carrier level correction (bits in the 0x200 word), 1.5 dB steps INFERRED
            double y = 0;
            if (egdb - att > -100) {
                double amp = db2lin(egdb - att);
                double fegSemi = s.feg.tick();
                int pmw = (int)(C.regPM * PMS_FRAC[v.pms]);
                double fop;
                if (fs && C.fseqOp[o]) fop = word_hz(C.fqWord[o] + pmw);
                else if (v.form == 7) fop = word_hz(C.freqWord[o] + (C.frmtWord[o] - 0x1243) + pmw + (C.regFM * v.fms) / 7);
                else if (!v.fixed) fop = word_hz(C.freqWord[o] + C.regPitch + pmw);
                else fop = word_hz(C.freqWord[o] + pmw + (C.regFM * v.fms) / 7);
                if (v.form != 7) fop *= pow(2.0, ((v.detune - 15) * 2.0) / 1200.0);       // INFERRED: detune 2 cents per step on non-formant ops
                fop *= pow(2.0, fegSemi / 12.0);
                y = op_sample(s, v, f0, fop, C.bwReg[o], in * FM_INDEX) * amp;
            }
            Cb = y; if (t0 & 2) H = y; if (t1 & 4) S += y; if (t0 & 4) fbNew = y;
            if (t1 & 1) mix += y * partV;
            // unvoiced (noise formant) operator
            const OpU& u = V.u[o];
            double uegdb = s.ueg.tick();
            double uatt = C.regULevel[o] * LEVEL_DB + C.regAM * LEVEL_DB * u.ams / 7.0;
            if (uegdb - uatt > -100) {
                double uamp = db2lin(uegdb - uatt);
                double nf;
                if (fs && C.fseqUOp[o]) nf = word_hz(C.fquWord[o]);
                else if (u.mode == 1) nf = f0;
                else if (u.mode == 2 && v.form == 7) nf = word_hz(C.freqWord[o] + (C.frmtWord[o] - 0x1243));
                else nf = word_hz(C.ufreqWord[o] + (C.regFM * u.fms) / 7);
                nf *= pow(2.0, u.transpose / 12.0 + s.ufeg.tick() / 12.0);
                s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
                double nz = ((int32_t)s.rng) * (1.0 / 2147483648.0);
                double fcut = 20.0 * pow(2.0, C.ubwReg[o] / 127.0 * 9.0);    // INFERRED noise formant model, see docs
                double a = 1.0 - exp(-2 * PI * fcut / SR);
                int stages = 1 + u.skirt;
                for (int k = 0; k < stages; k++) { s.lp[k] += (nz - s.lp[k]) * a; nz = s.lp[k]; }
                nz = nz * sqrt(stages * (2.0 / a)) * 0.5 + u.res / 7.0;
                s.nphase += nf / SR; if (s.nphase >= 1) s.nphase -= 1;
                mix += nz * fsin(s.nphase) * uamp * partU;
            }
        }
        C.fbBus = (fbNew + C.fbPrev) * 0.5; C.fbPrev = fbNew;
        bool alive = false;
        for (auto& s : C.op) if (!s.eg.done() || !s.ueg.done()) { alive = true; break; }
        if (!alive) C.active = false;
        return mix;
    }
    void render(int16_t* buf, int frames) {
        std::lock_guard<std::mutex> lk(mtx);
        for (int i = 0; i < frames; i++) {
            tickAcc += TICK_HZ / SR; while (tickAcc >= 1) { tickAcc -= 1; tick(); }
            double l = 0;
            for (auto& c : ch) if (c.active) l += render_chan(c);
            l *= gain; l = l / (1.0 + fabs(l) * 0.5);
            int16_t s = (int16_t)clampi((int)(l * 32767.0), -32767, 32767);
            buf[2 * i] = s; buf[2 * i + 1] = s;
        }
    }
};

// ------------------------------------------------------------------------------------------ ROM and sysex loading
struct Rom { std::vector<uint8_t> d; bool ok = false; };
static bool load_rom(Rom& R, const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    R.d.assign(2097152, 0); size_t n = fread(R.d.data(), 1, R.d.size(), f); fclose(f);
    if (n != R.d.size()) return false;
    if (R.d[0x1C8000] == 0 && R.d[0x1C8001] == 0 && R.d[0x1C8002] == 0 && R.d[0x1C8003] == 4)
        for (size_t i = 0; i < R.d.size(); i += 2) std::swap(R.d[i], R.d[i + 1]);
    R.ok = true; return true;
}
// voices: 0-255 native at 0x5C280 (PrJ, PrK), 256-1407 DX7 format at 0x30091 (PrA..PrI)
static void rom_voice(const Rom& R, int idx, Voice& V) {
    idx = clampi(idx, 0, 1407);
    if (idx < 256) memcpy(V.raw, R.d.data() + 0x5C280 + (size_t)idx * 608, 608);
    else { const uint8_t* r = R.d.data() + 0x30091 + (size_t)(idx - 256) * 155; uint8_t vced[155]; memcpy(vced, r + 10, 145); memcpy(vced + 145, r, 10); convert_dx7(vced, V.raw); }
    decode_voice(V);
}
static int bank_voice_index(int bank, int prog) {   // part bank 1=Int (mapped to PrJ), 2..12 = PrA..PrK
    prog &= 0x7F;
    if (bank >= 2 && bank <= 10) return 256 + (bank - 2) * 128 + prog;
    if (bank == 11) return prog; if (bank == 12) return 128 + prog;
    return prog;
}
// performances: one table of 400-byte entries at 0xC580 (PrA, PrB, then the factory internal set from index 357)
static void perf_from_bytes(Synth& S, const Rom* R, const uint8_t* d) {
    Perf& P = S.perf;
    memcpy(P.c, d, 80); memcpy(P.fx, d + 80, 112);
    for (int i = 0; i < 4; i++) {
        Part& pt = P.part[i]; memcpy(pt.p, d + 192 + 52 * i, 52); pt.nheld = 0; pt.lastPitch = -1;
        if (R && R->ok && pt.p[1]) rom_voice(*R, bank_voice_index(pt.p[1], pt.p[2]), pt.voice);
    }
    memcpy(P.name, P.c, 12); P.name[12] = 0;
    S.fseqPart = (P.c[0x15] & 7) ? (P.c[0x15] & 7) - 1 : -1;
    if (R && R->ok && S.fseqPart >= 0 && (P.c[0x16] & 1)) {
        int n = P.c[0x17]; size_t off = n < 10 ? 0x100A00 + (size_t)n * 25632 : 0x83000 + (size_t)(n - 10) * 6432;
        S.fseq.from_bytes(R->d.data() + off, R->d.data() + off + 32, n < 10 ? 512 : 128);
    }
}
static bool rom_perf(Synth& S, const Rom& R, int idx) {
    idx = clampi(idx, 0, 359); size_t off = 0xC580 + (size_t)idx * 400;
    std::lock_guard<std::mutex> lk(S.mtx); perf_from_bytes(S, &R, R.d.data() + off); return true;
}
static bool rom_fseq(Synth& S, const Rom& R, int n) {   // preset Fseq 1..90
    n = clampi(n, 1, 90); size_t off = n <= 10 ? 0x100A00 + (size_t)(n - 1) * 25632 : 0x83000 + (size_t)(n - 11) * 6432;
    std::lock_guard<std::mutex> lk(S.mtx); S.fseq.from_bytes(R.d.data() + off, R.d.data() + off + 32, n <= 10 ? 512 : 128);
    if (S.fseqPart < 0) S.fseqPart = 0;
    return true;
}
static bool load_sysex(Synth& S, const Rom* R, const uint8_t* d, size_t len, int pick, int part) {
    // FS1R bulk: F0 43 0n 5E bc bc ah am al <data> cs F7. Voice 608 bytes (ah 40-43/51), performance 400 (ah 10/11), Fseq (ah 70)
    // DX7 VCED:  F0 43 0n 00 01 1B <155 bytes> cs F7
    int found = 0;
    for (size_t i = 0; i + 10 < len; i++) {
        if (d[i] != 0xF0 || d[i + 1] != 0x43 || (d[i + 2] & 0xF0) != 0x00) continue;
        if (d[i + 3] == 0x5E) {
            int bc = d[i + 4] << 7 | d[i + 5]; int ah = d[i + 6]; const uint8_t* p = d + i + 9;
            if (i + 9 + bc + 2 > len) break;
            if (bc == 608 && ((ah >= 0x40 && ah <= 0x43) || ah == 0x51)) {
                if (found++ != pick) continue;
                std::lock_guard<std::mutex> lk(S.mtx); int pi = ah <= 0x43 ? ah - 0x40 : part;
                memcpy(S.perf.part[pi].voice.raw, p, 608); decode_voice(S.perf.part[pi].voice); return true;
            }
            if (bc == 400 && (ah == 0x10 || ah == 0x11)) {
                if (found++ != pick) continue;
                std::lock_guard<std::mutex> lk(S.mtx); perf_from_bytes(S, R, p); return true;
            }
            if (ah == 0x70 && bc >= 32 + 50) {
                if (found++ != pick) continue;
                std::lock_guard<std::mutex> lk(S.mtx); S.fseq.from_bytes(p, p + 32, std::min(512, (bc - 32) / 50)); if (S.fseqPart < 0) S.fseqPart = 0; return true;
            }
        } else if (d[i + 3] == 0x00 && d[i + 4] == 0x01 && d[i + 5] == 0x1B) {
            if (i + 6 + 155 + 2 > len) break;
            if (found++ != pick) continue;
            std::lock_guard<std::mutex> lk(S.mtx); convert_dx7(d + i + 6, S.perf.part[part].voice.raw); decode_voice(S.perf.part[part].voice); return true;
        }
    }
    return false;
}
static bool apply_param_change(Synth& S, const uint8_t* d, size_t len) {
    // F0 43 1n 5E ah am al vh vl F7 : 10 perf common, 3p part, 4p voice common, 6p op, 00 system
    if (len < 10 || d[1] != 0x43 || (d[2] & 0xF0) != 0x10 || d[3] != 0x5E) return false;
    int ah = d[4], am = d[5], al = d[6], val = d[7] << 7 | d[8];
    std::lock_guard<std::mutex> lk(S.mtx);
    if (ah == 0x10 && am == 0 && al < 80) S.perf.c[al] = (uint8_t)(val & 0x7F);
    else if (ah >= 0x30 && ah <= 0x33 && al < 52) S.perf.part[ah - 0x30].p[al] = (uint8_t)(val & 0x7F);
    else if (ah >= 0x40 && ah <= 0x43 && al < 112) { S.perf.part[ah - 0x40].voice.raw[al] = (uint8_t)(val & 0x7F); decode_voice(S.perf.part[ah - 0x40].voice); }
    else if (ah >= 0x60 && ah <= 0x63 && am < 8 && al < 62) { S.perf.part[ah - 0x60].voice.raw[112 + am * 62 + al] = (uint8_t)(val & 0x7F); decode_voice(S.perf.part[ah - 0x60].voice); }
    else if (ah == 0x00 && am == 0) { if (al == 0) S.sysTune = val & 0x7F; else if (al == 6) S.sysNoteShift = val & 0x7F; else if (al == 0x0E) S.velCurve = val & 7; }
    else return false;
    return true;
}

// ------------------------------------------------------------------------------------------ MIDI in
struct MidiQueue {
    std::atomic<uint32_t> head{0}, tail{0}; uint32_t buf[1024];
    void push(uint32_t m) { uint32_t h = head.load(); if (((h + 1) & 1023) != (tail.load() & 1023)) { buf[h & 1023] = m; head.store(h + 1); } }
    bool pop(uint32_t& m) { uint32_t t = tail.load(); if (t == head.load()) return false; m = buf[t & 1023]; tail.store(t + 1); return true; }
};
static MidiQueue g_q;
static std::mutex g_sxmtx; static std::vector<std::vector<uint8_t>> g_sysex;
static MIDIHDR g_hdr[2]; static uint8_t g_sxbuf[2][65536];
static HMIDIIN g_hmi = nullptr;
static void CALLBACK midi_cb(HMIDIIN h, UINT msg, DWORD_PTR, DWORD_PTR p1, DWORD_PTR) {
    if (msg == MIM_DATA) g_q.push((uint32_t)p1);
    else if (msg == MIM_LONGDATA) {
        MIDIHDR* hdr = (MIDIHDR*)p1;
        if (hdr->dwBytesRecorded) { std::lock_guard<std::mutex> lk(g_sxmtx); g_sysex.emplace_back(hdr->lpData, hdr->lpData + hdr->dwBytesRecorded); }
        midiInAddBuffer(h, hdr, sizeof(MIDIHDR));
    }
}
static int g_channel = -1;   // -1 = parts use their own receive channels, else every part listens here
static int g_perfCh = 0;     // system "performance channel" (part rcv 0x10)
static const Rom* g_rom = nullptr;

static void handle_midi(Synth& S) {
    uint32_t m;
    while (g_q.pop(m)) {
        int st = m & 0xF0, chn = m & 0x0F, d1 = m >> 8 & 0x7F, d2 = m >> 16 & 0x7F;
        std::lock_guard<std::mutex> lk(S.mtx);
        for (int p = 0; p < 4; p++) {
            Part& pt = S.perf.part[p]; int rc = pt.rcv();
            bool on = rc != 0x7F && (g_channel >= 0 ? chn == g_channel : (rc == 0x10 ? chn == g_perfCh : rc == chn));
            if (!on) continue;
            switch (st) {
            case 0x90: if (d2) { S.note_on(p, d1, d2); break; } // fallthrough
            case 0x80: S.note_off(p, d1); break;
            case 0xA0: pt.src[12] = d2; break;
            case 0xD0: pt.src[11] = d1; break;
            case 0xE0: pt.bend = (d2 - 64) * 16; pt.src[13] = d2; break;
            case 0xB0:
                if (d1 == 1) pt.src[10] = d2; else if (d1 == 2) pt.src[9] = d2; else if (d1 == 4) pt.src[8] = d2;
                else if (d1 >= 16 && d1 <= 19) pt.src[d1 - 16] = d2; else if (d1 >= 20 && d1 <= 22) pt.src[4 + d1 - 20] = d2; else if (d1 == 13) pt.src[7] = d2;
                else if (d1 == 7) pt.p[0x0B] = (uint8_t)d2; else if (d1 == 11) pt.expr = std::min(254, d2 * 2);
                else if (d1 == 64) S.set_sustain(p, d2 >= 64);
                else if (d1 == 65) pt.p[0x24] = (uint8_t)((pt.p[0x24] & 2) | (d2 >= 64)); else if (d1 == 5) pt.p[0x25] = (uint8_t)d2;
                else if (d1 == 120) S.all_off(); else if (d1 == 123) S.all_release();
                else if (d1 == 121) { pt.bend = 0; pt.expr = 254; memset(pt.src, 0, sizeof pt.src); pt.sustain = false; }
                break;
            case 0xC0: if (g_rom && g_rom->ok && pt.p[1]) { pt.p[2] = (uint8_t)d1; rom_voice(*g_rom, bank_voice_index(pt.p[1], d1), pt.voice); } break;
            }
        }
    }
    std::vector<std::vector<uint8_t>> sx;
    { std::lock_guard<std::mutex> lk(g_sxmtx); sx.swap(g_sysex); }
    for (auto& v : sx) {
        if (load_sysex(S, g_rom, v.data(), v.size(), 0, 0)) printf("bulk received: %s / %s\n", S.perf.name, S.perf.part[0].voice.name);
        else apply_param_change(S, v.data(), v.size());
    }
}

// ------------------------------------------------------------------------------------------ offline check
static int g_note2 = -1;   // -n2: second note played at 1/3 while the first is held (mono legato / portamento check)
static int render_wav(Synth& S, const char* path, int note, double secs) {
    int frames = (int)(secs * SR); std::vector<int16_t> pcm((size_t)frames * 2);
    int half = frames / 2, third = frames / 3;
    { std::lock_guard<std::mutex> lk(S.mtx); for (int p = 0; p < 4; p++) if (S.perf.part[p].rcv() != 0x7F) S.note_on(p, note, 100); }
    if (g_note2 >= 0) {
        S.render(pcm.data(), third);
        { std::lock_guard<std::mutex> lk(S.mtx); for (int p = 0; p < 4; p++) if (S.perf.part[p].rcv() != 0x7F) S.note_on(p, g_note2, 100); }
        S.render(pcm.data() + (size_t)third * 2, half - third);
    } else S.render(pcm.data(), half);
    { std::lock_guard<std::mutex> lk(S.mtx); for (int p = 0; p < 4; p++) { S.note_off(p, note); if (g_note2 >= 0) S.note_off(p, g_note2); } }
    S.render(pcm.data() + (size_t)half * 2, frames - half);
    FILE* f = fopen(path, "wb"); if (!f) { printf("cannot write %s\n", path); return 1; }
    uint32_t dataBytes = (uint32_t)pcm.size() * 2, fmtLen = 16, riffLen = 36 + dataBytes; uint16_t one = 1, chn = 2, bits = 16, align = 4; uint32_t rate = SR, bps = SR * 4;
    fwrite("RIFF", 1, 4, f); fwrite(&riffLen, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&chn, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&bps, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
    double peak = 0, rms = 0; for (int i = 0; i < frames; i++) { double x = pcm[2 * i] / 32768.0; peak = std::max(peak, fabs(x)); rms += x * x; }
    printf("wrote %s: %d frames, peak %.3f, rms %.3f\n", path, frames, peak, sqrt(rms / frames));
    return 0;
}

// ------------------------------------------------------------------------------------------ main
static std::string ini_path() { char p[MAX_PATH]; GetModuleFileNameA(nullptr, p, MAX_PATH); std::string s(p); size_t k = s.find_last_of("\\/"); return s.substr(0, k + 1) + "fs1r_emu.ini"; }
static int list_midi() { int n = midiInGetNumDevs(); for (int i = 0; i < n; i++) { MIDIINCAPSA c; midiInGetDevCapsA(i, &c, sizeof c); printf("  %d: %s\n", i, c.szPname); } return n; }

int main(int argc, char** argv) {
    init_tables();
    Synth* S = new Synth(); static Rom rom;
    int midiPort = -1, pick = -1, perfIdx = -1, fseqIdx = -1; const char* syx = nullptr; const char* romPath = nullptr;
    const char* wav = nullptr; int testNote = 60; double testSecs = 3.0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-l") { printf("MIDI inputs:\n"); list_midi(); return 0; }
        else if (a == "-m" && i + 1 < argc) midiPort = atoi(argv[++i]);
        else if (a == "-c" && i + 1 < argc) g_channel = atoi(argv[++i]) - 1;
        else if (a == "-v" && i + 1 < argc) syx = argv[++i];
        else if (a == "-p" && i + 1 < argc) pick = atoi(argv[++i]);
        else if (a == "-P" && i + 1 < argc) perfIdx = atoi(argv[++i]);
        else if (a == "-f" && i + 1 < argc) fseqIdx = atoi(argv[++i]);
        else if (a == "-r" && i + 1 < argc) romPath = argv[++i];
        else if (a == "-g" && i + 1 < argc) S->gain = atof(argv[++i]);
        else if (a == "-w" && i + 1 < argc) wav = argv[++i];
        else if (a == "-n" && i + 1 < argc) testNote = atoi(argv[++i]);
        else if (a == "-n2" && i + 1 < argc) g_note2 = atoi(argv[++i]);
        else if (a == "-mono" && i + 1 < argc) { Part& pt = S->perf.part[0]; pt.p[5] = 0; pt.p[6] = 0; pt.p[0x24] = 3; pt.p[0x25] = (uint8_t)atoi(argv[++i]); }   // part 1 mono, full-time portamento with this time
        else if (a == "-d" && i + 1 < argc) testSecs = atof(argv[++i]);
        else { printf("usage: fs1r_emu [-l] [-m midiport] [-c channel] [-v file.syx [-p index]] [-r eprom.bin [-p voice] [-P performance] [-f fseq]] [-g gain] [-w test.wav [-n note] [-d seconds]]\n"); return 1; }
    }
    if (romPath) { if (!load_rom(rom, romPath)) { printf("cannot read 2 MB EPROM image %s\n", romPath); return 1; } g_rom = &rom; }
    if (perfIdx >= 0) { if (!rom.ok) { printf("-P needs -r\n"); return 1; } rom_perf(*S, rom, perfIdx); }
    if (syx) {
        FILE* f = fopen(syx, "rb"); if (!f) { printf("cannot open %s\n", syx); return 1; }
        std::vector<uint8_t> d; uint8_t tmp[65536]; size_t n; while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) d.insert(d.end(), tmp, tmp + n); fclose(f);
        if (!load_sysex(*S, rom.ok ? &rom : nullptr, d.data(), d.size(), std::max(0, pick), 0)) { printf("no FS1R voice/performance/Fseq or DX7 voice dump #%d found in %s\n", std::max(0, pick), syx); return 1; }
    } else if (rom.ok && pick >= 0) { std::lock_guard<std::mutex> lk(S->mtx); rom_voice(rom, pick, S->perf.part[0].voice); }
    if (fseqIdx >= 0) { if (!rom.ok) { printf("-f needs -r\n"); return 1; } rom_fseq(*S, rom, fseqIdx); }
    printf("performance \"%s\"", S->perf.name);
    for (int p = 0; p < 4; p++) if (S->perf.part[p].rcv() != 0x7F) printf("  part%d \"%s\" alg %d", p + 1, S->perf.part[p].voice.name, S->perf.part[p].voice.alg + 1);
    if (S->fseq.valid) printf("  fseq \"%s\" (%d frames)", S->fseq.name, S->fseq.nframes);
    printf("\n");
    if (wav) return render_wav(*S, wav, testNote, testSecs);
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
        printf("MIDI in: %d (%s), %s\n", midiPort, c.szPname, g_channel < 0 ? "parts on their receive channels (part 1 = performance channel 1)" : ("all parts on channel " + std::to_string(g_channel + 1)).c_str());
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
    int cur = 0;
    for (;;) {
        WAVEHDR& h = wh[cur];
        while (!(h.dwFlags & WHDR_DONE)) WaitForSingleObject(ev, 10);
        handle_midi(*S);
        S->render(pcm[cur], BLOCK);
        h.dwFlags &= ~WHDR_DONE;
        waveOutWrite(hwo, &h, sizeof(WAVEHDR));
        cur = (cur + 1) % NBUF;
    }
}
