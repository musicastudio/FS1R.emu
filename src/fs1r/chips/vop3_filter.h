// fs1r/chips/vop3_filter.h - VOP3-1, the per-voice filter, modelled.
//
// INFERRED. The CPU side is read from the firmware, including the cutoff and both resonance
// conversions, so what is modelled here is only what the chip does with a coefficient. Measured
// 2026-09-21 by the 15_filter capture: the three lowpasses are taps off one 4-pole ladder and HPF,
// BPF and BEF a 2-pole state variable filter, which is what the unit reads at 24, 18 and 12 dB per
// octave and 12 on the highpass. docs/filter.md carries the working.
//
// Included by fs1r/internal.h, which Chan needs for its VFilter.
#pragma once
#include "cal.h"

static inline double reso_comp(int type, int r) {
    if (type == 3 || type == 5) return 0.0;                 // the firmware zeroes table B for HPF and BEF
    double x = reso_r(r); return cal::RESO_COMP * std::max(0.0, 0.5 - 2 * x * x);
}

struct SVF {                     // topology-preserving 2-pole state variable filter (Zavalishin)
    double g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set(double fHz, double q) {
        g = tan(PI * std::min(fHz, SR * 0.45) / SR); k = 1.0 / std::max(0.5, q);
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void run(double x, double& lp, double& bp, double& hp) {
        double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    void clear() { ic1 = ic2 = 0; }
};
// Four one-pole stages behind one cutoff coefficient and one feedback, resolved zero-delay (Zavalishin)
// so it stays stable up to the self-oscillation point. The firmware writes one cutoff coefficient per
// filter channel and takes the three lowpass slopes off the same cascade, so they are taps rather than
// three separate topologies. ponytail: the tap is a plain stage output; FUN_0000CA44's literal +-0x4000
// mix word and its 0x40/0x7F input scaler are not modelled, because what the chip does with them needs
// the VOP3 instruction set. All three taps have the same DC gain, which is what that pairing implies.
struct Ladder {
    double G = 0, k = 0, comp = 0, z[4] = {};
    void set(double fHz, double kk, double cc) {
        double g = tan(PI * std::min(fHz, SR * 0.45) / SR);
        G = g / (1.0 + g); k = kk; comp = cc;
    }
    void clear() { z[0] = z[1] = z[2] = z[3] = 0; }
    inline double run(double x, int tap) {
        // (1 + k) holds the passband at unity as the feedback rises, which is what the chip must do:
        // table B only spans 0 to 0.5 and could not pay back the 14 dB an unnormalised loop loses.
        x *= (1.0 + k) * (1.0 + comp);
        double S = 0;                                       // G^3 s1 + G^2 s2 + G s3 + s4, s_i = (1-G) z_i
        for (int i = 0; i < 4; i++) S = S * G + (1.0 - G) * z[i];
        double G4 = G * G * G * G, y4 = (G4 * x + S) / (1.0 + k * G4);
        double u = x - k * y4, out = 0;
        for (int i = 0; i < 4; i++) {
            double v = (u - z[i]) * G, y = v + z[i];
            z[i] = y + v; u = y;
            if (i + 1 == tap) out = y;
        }
        return out;
    }
};
// Per-voice filter, register 0x270 = 11 when any part turns it on. Chip side, so INFERRED. Coefficients
// are refreshed on the 192.3 Hz tick, which is when the CPU would write them. The chip is not the YMP706:
// 0x270 switches a channel loop out to VOP3-1, and the coefficients go over its own register block at
// 0x800200 (docs/research.md 2.0.1 and 8). The three lowpasses are taps off one ladder; HPF, BPF and BEF
// are separate chip modes (FUN_0000C1AC writes the type into bits 5-7) and keep the 2-pole SVF reading.
struct VFilter {
    SVF a; Ladder lad;
    void setup(int type, double fHz, int reso) {
        if (type <= 2) lad.set(fHz, reso_fb(reso), reso_comp(type, reso));
        else a.set(fHz, reso_q(reso));
    }
    void clear() { a.clear(); lad.clear(); }
    inline double run(int type, double x) {
        if (type <= 2) return lad.run(x, 4 - type);                                 // LPF24 / LPF18 / LPF12
        double lp, bp, hp; a.run(x, lp, bp, hp);
        switch (type) {
        case 3: return hp;                                                          // HPF
        case 4: return bp;                                                          // BPF
        default: return lp + hp;                                                    // BEF
        }
    }
};
// 4-segment EG on a linear scale in the FS1R's own order: start at L4, run L1 -> L2 -> L3 and hold, key
// off runs to L4. Stepped on the 192.3 Hz tick like the filter registers.
struct StepEG {
    double cur = 0, target = 0, k = 1; int stage = 9; double L[4] = {}; int R[4] = {};
    void start(const double* lv, const int* rt) { for (int i = 0; i < 4; i++) { L[i] = lv[i]; R[i] = rt[i]; } cur = L[3]; go(0); }
    void go(int s) { stage = s; target = L[s]; k = 1.0 - exp(-1.0 / (rate_secs(clampi(R[s], 0, 63)) * cal::FEG_STEP_K * TICK_HZ + 1)); }
    void release() { go(3); }
    inline double tick() {
        if (stage > 3) return cur;
        cur += (target - cur) * k;
        if (fabs(target - cur) < 1e-4) { cur = target; if (stage < 2) go(stage + 1); else stage = 9; }
        return cur;
    }
};

