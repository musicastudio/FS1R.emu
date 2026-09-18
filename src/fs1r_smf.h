// fs1r_smf.h - Standard MIDI File reading, offline rendering and WAV writing, with no host in them.
//
// These were inside fs1r_console.cpp, which cannot build anywhere but Windows because the rest of it is
// WinMM. The calibration loop needs the renders and nothing about a render is Windows-shaped, so they
// live here and tools/render_capture.cpp is the portable front end. The console includes this too, so
// there is still one copy of each.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "fs1r_lib.h"

namespace fs1r_smf {

static const int SR = (int)fs1r::ENGINE_RATE;

// 16-bit for listening, 32-bit float for measuring: a render compared against a 24-bit capture must not
// carry a quantisation floor of its own, and the quietest segments of the capture set sit below -100 dB.
enum WavFormat { WAV_S16, WAV_F32 };

inline void write_wav(const char* path, const std::vector<float>& l, const std::vector<float>& r,
                      WavFormat format = WAV_S16) {
    size_t frames = l.size();
    double peak = 0, rms = 0;
    for (size_t i = 0; i < frames; i++) { peak = std::max(peak, (double)fabs(l[i])); rms += (double)l[i] * l[i]; }
    std::vector<uint8_t> body;
    if (format == WAV_F32) {
        body.resize(frames * 8);
        for (size_t i = 0; i < frames; i++) {
            memcpy(&body[8 * i], &l[i], 4);
            memcpy(&body[8 * i + 4], &r[i], 4);
        }
    } else {
        body.resize(frames * 4);
        for (size_t i = 0; i < frames; i++) {
            int16_t a = (int16_t)std::max(-32767.0f, std::min(32767.0f, l[i] * 32767.f));
            int16_t b = (int16_t)std::max(-32767.0f, std::min(32767.0f, r[i] * 32767.f));
            memcpy(&body[4 * i], &a, 2); memcpy(&body[4 * i + 2], &b, 2);
        }
    }
    FILE* f = fopen(path, "wb"); if (!f) { printf("cannot write %s\n", path); return; }
    uint16_t tag = format == WAV_F32 ? 3 : 1, chn = 2, bits = format == WAV_F32 ? 32 : 16;
    uint16_t align = (uint16_t)(chn * bits / 8);
    uint32_t dataBytes = (uint32_t)body.size(), fmtLen = 16, riffLen = 36 + dataBytes;
    uint32_t rate = SR, bps = SR * align;
    fwrite("RIFF", 1, 4, f); fwrite(&riffLen, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&tag, 2, 1, f); fwrite(&chn, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&bps, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f); fwrite(body.data(), 1, body.size(), f); fclose(f);
    printf("wrote %s: %d frames, peak %.4f, rms %.4f\n", path, (int)frames, peak,
           sqrt(rms / std::max<size_t>(1, frames)));
}

// ------------------------------------------------------------------------------------------ MIDI file render
// Plays a Standard MIDI File through the engine and writes the result, so the hardware capture requests
// in captures/requests can be rendered by us and compared segment for segment with a recording of the
// real unit. Types 0 and 1, all tracks merged, tempo changes followed.
struct SmfEvent { double t; std::vector<uint8_t> bytes; };

inline bool read_smf(const char* path, std::vector<SmfEvent>& out) {
    FILE* f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); return false; }
    std::vector<uint8_t> d; uint8_t tmp[65536]; size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) d.insert(d.end(), tmp, tmp + n);
    fclose(f);
    auto be32 = [&](size_t i) { return (uint32_t)d[i] << 24 | (uint32_t)d[i + 1] << 16 | (uint32_t)d[i + 2] << 8 | d[i + 3]; };
    if (d.size() < 14 || memcmp(d.data(), "MThd", 4)) { printf("%s is not a MIDI file\n", path); return false; }
    int division = d[12] << 8 | d[13];
    if (division & 0x8000) { printf("SMPTE timing is not supported\n"); return false; }
    // Each track is read into ticks first; tempo maps ticks to seconds afterwards, so a tempo change in
    // one track applies to every track.
    struct TickEvent { uint32_t tick; std::vector<uint8_t> bytes; };
    std::vector<TickEvent> ev;
    std::vector<std::pair<uint32_t, uint32_t>> tempo;   // tick, microseconds per quarter note
    size_t i = 8 + be32(4);
    while (i + 8 <= d.size()) {
        uint32_t len = be32(i + 4);
        size_t end = std::min(d.size(), i + 8 + (size_t)len), j = i + 8;
        if (memcmp(&d[i], "MTrk", 4)) { i = end; continue; }
        uint32_t tick = 0; uint8_t running = 0;
        while (j < end) {
            uint32_t delta = 0;
            while (j < end) { delta = delta << 7 | (d[j] & 0x7F); if (!(d[j++] & 0x80)) break; }
            tick += delta;
            if (j >= end) break;
            uint8_t st = d[j];
            if (st == 0xFF) {                                  // meta
                j++; uint8_t type = d[j++]; uint32_t n2 = 0;
                while (j < end) { n2 = n2 << 7 | (d[j] & 0x7F); if (!(d[j++] & 0x80)) break; }
                if (type == 0x51 && n2 == 3) tempo.push_back({tick, (uint32_t)d[j] << 16 | (uint32_t)d[j + 1] << 8 | d[j + 2]});
                j += n2;
            } else if (st == 0xF0 || st == 0xF7) {             // sysex, possibly in packets
                j++; uint32_t n2 = 0;
                while (j < end) { n2 = n2 << 7 | (d[j] & 0x7F); if (!(d[j++] & 0x80)) break; }
                std::vector<uint8_t> m;
                if (st == 0xF0) m.push_back(0xF0);
                m.insert(m.end(), d.begin() + j, d.begin() + std::min(end, j + n2));
                j += n2;
                ev.push_back({tick, m});
            } else {
                if (st & 0x80) { running = st; j++; } else st = running;
                int nd = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
                std::vector<uint8_t> m{st};
                for (int k = 0; k < nd && j < end; k++) m.push_back(d[j++]);
                ev.push_back({tick, m});
            }
        }
        i = end;
    }
    std::sort(tempo.begin(), tempo.end());
    std::stable_sort(ev.begin(), ev.end(), [](const TickEvent& a, const TickEvent& b) { return a.tick < b.tick; });
    auto seconds = [&](uint32_t tick) {
        double t = 0; uint32_t prev = 0, us = 500000;
        for (auto& tc : tempo) {
            if (tc.first >= tick) break;
            t += (double)(tc.first - prev) * us / 1e6 / division;
            prev = tc.first; us = tc.second;
        }
        return t + (double)(tick - prev) * us / 1e6 / division;
    };
    for (auto& e : ev) out.push_back({seconds(e.tick), e.bytes});
    printf("%s: %d events, %.1f s\n", path, (int)out.size(), out.empty() ? 0.0 : out.back().t);
    return !out.empty();
}

inline int render_smf(fs1r::Device& dev, const char* smfPath, const char* wavPath, double tailSecs,
                      WavFormat format = WAV_S16) {
    std::vector<SmfEvent> ev;
    if (!read_smf(smfPath, ev)) return 1;
    int frames = (int)((ev.back().t + tailSecs) * SR) + 1;
    std::vector<float> l(frames, 0.f), r(frames, 0.f);
    int done = 0;
    for (auto& e : ev) {
        int at = std::min(frames, (int)(e.t * SR));
        if (at > done) { dev.process(l.data() + done, r.data() + done, at - done); done = at; }
        dev.sendMidi(e.bytes.data(), e.bytes.size());
    }
    if (done < frames) dev.process(l.data() + done, r.data() + done, frames - done);
    write_wav(wavPath, l, r, format);
    return 0;
}

}   // namespace fs1r_smf
