// Self-check for src/fs1r/chips/vop3_effects.h: parameter decoding against the Effect Parameter List defaults, and
// an impulse through every effect type to catch silence, blow-ups and NaNs.
//   build.bat tools\test_effects.cpp /Fe:build\test_effects.exe && build\test_effects.exe
#include <cassert>
#include <cmath>
#include <cstdio>
#include <algorithm>
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static const double PI = 3.14159265358979323846;
#include "fs1r/chips/vop3_effects.h"

static int fails = 0;
static void near(const char* what, double got, double want, double tol) {
    if (fabs(got - want) > tol) { printf("  FAIL %-22s got %.4f want %.4f\n", what, got, want); fails++; }
}

int main() {
    // Parameter decoding, checked against the documented defaults of the preset effect types.
    near("EQ low 32 Hz",      fx_freq_hz(4),   32.0,   1.0);
    near("EQ mid 1.1 kHz",    fx_freq_hz(35),  1100.0, 45.0);
    near("LPF 9.0 kHz",       fx_freq_hz(53),  9000.0, 350.0);
    near("EQ high 16 kHz",    fx_freq_hz(58),  16000.0, 300.0);
    near("gain +8 dB",        fx_gain_db(72),  8.0,    0.001);
    near("Q 1.0",             fx_q(10),        1.0,    0.001);
    near("delay 365.0 ms",    fx_ms(3650),     365.0,  0.001);
    near("chorus LFO 0.229",  fx_lfo_hz(5),    0.229,  0.002);
    near("flanger LFO 0.504", fx_lfo_hz(11),   0.504,  0.002);
    near("rotary LFO 2.335",  fx_lfo_hz(51),   2.335,  0.01);
    near("tremolo LFO 5.493", fx_lfo_hz(84),   5.493,  0.01);
    near("autopan LFO 4.028", fx_lfo_hz(76),   4.028,  0.01);
    near("revtime 1.4 s",     fx_revtime_s(11), 1.4,   0.001);
    near("revtime 30 s",      fx_revtime_s(69), 30.0,  0.001);
    near("init delay 44.2 ms", fx_init_ms(14), 44.2,   0.3);
    near("rev delay 53.6 ms", fx_revdly_ms(34), 53.6,  0.3);
    near("threshold -48 dB",  fx_thresh_db(79), -48.0, 0.001);

    // Impulse response of every type in every slot: must produce output, stay finite and stay bounded.
    const double sr = 44100;
    struct { FxSlot slot; int n; const char* name; } slots[3] = {{FX_REVERB, 17, "reverb"}, {FX_VARIATION, 29, "variation"}, {FX_INSERTION, 41, "insertion"}};
    for (auto& s : slots) {
        for (int t = 0; t < s.n; t++) {
            FxBlock fx; fx.init(sr);
            // mid-scale parameters: every index in range, times short enough to land inside 2 s
            int w[16], b[8];
            for (int i = 0; i < 16; i++) w[i] = 40;
            for (int i = 0; i < 8; i++) b[i] = 40;
            w[0] = 20; b[3] = 3; b[4] = 64; b[5] = 8; b[6] = 64;
            if (s.slot == FX_INSERTION && t == 20) w[2] = 6;   // Lo-Fi output gain is -6..+36 dB: 40 is +34
            fx.configure(s.slot, t, w, b);
            double peak = 0, energy = 0; bool bad = false;
            for (int i = 0; i < (int)(2 * sr); i++) {
                double x = i < 64 ? sin(2 * PI * 440 * i / sr) : 0.0, l, r;
                fx.process(x, x, l, r);
                if (!std::isfinite(l) || !std::isfinite(r)) { bad = true; break; }
                peak = std::max(peak, std::max(fabs(l), fabs(r)));
                if (i > (int)(0.2 * sr)) energy += l * l + r * r;
            }
            if (bad) { printf("  FAIL %s type %d: not finite\n", s.name, t); fails++; continue; }
            if (peak > 40) { printf("  FAIL %s type %d: runaway, peak %.1f\n", s.name, t, peak); fails++; continue; }
            bool silent = peak < 1e-6;
            bool thru = (s.slot == FX_REVERB && t == 0) || (s.slot != FX_REVERB && t == 0);
            if (silent && !thru) { printf("  FAIL %s type %d: silent\n", s.name, t); fails++; }
            (void)energy;
        }
    }

    // A reverb must still be ringing well after the input stops.
    {
        FxBlock fx; fx.init(sr);
        int w[16] = {}, b[8] = {};
        w[0] = 27;                       // reverb time 3.0 s
        w[1] = 10; w[2] = 8; w[3] = 0; w[4] = 49;
        b[2] = 20; b[3] = 4; b[4] = 64; b[5] = 10; b[6] = 64;
        fx.configure(FX_REVERB, 1, w, b);     // Hall1
        double early = 0, late = 0;
        for (int i = 0; i < (int)(3 * sr); i++) {
            double x = i < (int)(0.2 * sr) ? ((i * 2654435761u) % 1000) / 500.0 - 1.0 : 0.0, l, r;
            fx.process(x, x, l, r);
            if (i > (int)(0.3 * sr) && i < (int)(0.5 * sr)) early += l * l + r * r;
            if (i > (int)(2.0 * sr) && i < (int)(2.2 * sr)) late += l * l + r * r;
        }
        double db = 10 * log10((late + 1e-30) / (early + 1e-30));
        printf("  Hall1 3.0 s: tail at 2.0 s is %.1f dB below 0.3 s\n", db);
        if (db < -60 || db > -5) { printf("  FAIL reverb decay out of range\n"); fails++; }
    }

    // The sweep above uses mid-scale parameters, which no preset uses. These two insertion types are
    // driven with the factory words out of the preset performances instead, because both were silent
    // with them while passing the sweep: Lo-Fi read Word Length as a bit depth (1 bit rounds every
    // sample to zero) and its output gain as v-64, and Pitch Change read its levels one word high.
    {
        struct { int type; const char* name; int w[16]; } cases[2] = {
            {20, "Lo-Fi",        {4, 1, 3, 60, 2, 29, 1, 0, 10, 127, 104, 0, 0, 0, 0, 0}},
            {7,  "Pitch Change", {64, 127, 71, 57, 82, 28, 46, 0, 0, 46, 1, 127, 127, 127, 0, 0}},
        };
        for (auto& c : cases) {
            FxBlock fx; fx.init(sr);
            fx.configure(FX_INSERTION, c.type, c.w, nullptr);
            double pl = 0, pr = 0;
            for (int i = 0; i < (int)(0.5 * sr); i++) {
                double x = sin(2 * PI * 440 * i / sr) * 0.3, l, r;
                fx.process(x, x, l, r);
                if (i > (int)(0.1 * sr)) { pl = std::max(pl, fabs(l)); pr = std::max(pr, fabs(r)); }
            }
            if (pl < 0.01 || pr < 0.01) {
                printf("  FAIL insertion %s with its factory parameters: peak L %.4f R %.4f\n", c.name, pl, pr);
                fails++;
            }
        }
    }

    printf(fails ? "test_effects: %d FAILURES\n" : "test_effects: ok\n", fails);
    return fails ? 1 : 0;
}
