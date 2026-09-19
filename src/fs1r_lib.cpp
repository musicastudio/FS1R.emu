// fs1r_lib.cpp - the FS1R engine: everything the firmware does between MIDI and the tone generator
// registers, plus a model of what the two custom chips do with those registers.
//
// Reproduced from the v1.20 firmware with its own ROM tables (src/fs1r_rom_tables.h): velocity curves
// and attenuation, level key scaling, pitch (note table, detune, tune, bend, portamento), the software
// pitch EG and LFO1 on their 192.3 Hz tick, part levels, mono/poly note handling, performances, Fseq
// playback, the whole sysex parameter map. What the YMP706 tone generator and the YSS236 effect DSP do
// with those register values is in no file, so the conversions marked INFERRED follow the DX7 (same
// design lineage), the formant synthesis patent and the Data List; they are gathered in namespace cal.
//
// No Windows, no host, no GUI. src/fs1r_console.cpp is the test console on top of this.
#define _CRT_SECURE_NO_WARNINGS
#include "fs1r_lib.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "fs1r_algorithms.h"
#include "fs1r_rom_tables.h"

static const int SR = 48000;   // the hardware rate; fs1r::Device resamples to the host
static const int NCHAN = 32;
static const double PI = 3.14159265358979323846;
static const double CPU_HZ = 28000000.0;         // SCI BRR 27 gives exactly 31250 baud at 28 MHz
static const double TICK_HZ = CPU_HZ / 16.0 / 9099.0;   // MTU2 TGRA compare every 0x238B counts at clock/16 -> 192.3 Hz LFO/PEG/portamento tick

// ------------------------------------------------------------------------------------------ INFERRED calibration
// Everything the two custom chips do that no file documents. Most are models from the DX7 lineage, the
// formant patent (docs/US5610354...) or the Data List. A few carry "(demo)": those are fitted to rgwan's
// recording of the built-in demo, which is real hardware but a coarse reference, fifteen songs of mixed
// patches with their effects in the path. The capture set (TODO tier 4) is what settles any of them
// properly. They all live here so calibrating against a recording is one table edit rather than a hunt
// through the engine. Names match the TODO's "confirm the INFERRED constants".
namespace cal {
static const double FM_INDEX     = 3.369;   // cycles of phase deviation at full modulator level. MEASURED: two
                                            // sweeps of algorithm 8's modulator level register, 2026-09-19, whose
                                            // sidebands give 21.139 and 21.205 radians at register 0 over 25 and
                                            // 24 fitted steps, 3.3644 and 3.3749 cycles. The 4.0 this replaces
                                            // came off the first of those two takes read one step early: the
                                            // analyzer anchored the sweep on where the tone starts, and the rig
                                            // brings its note up 1.55 s before the first step writes anything, so
                                            // every spectrum was scored against its neighbour's register value.
                                            // The error is exactly one step of the sweep, 10^(4*LEVEL_DB/20) =
                                            // 1.1887, and 25.116/1.1887 is 21.13. FS1R.unlock's sweep_check.py
                                            // anchors on the ladder's first step now and both takes agree.
static const double LEVEL_DB     = 0.376287;// dB per step of the 8-bit level registers (LEVTAB doubled). MEASURED:
                                            // the same sweep's sideband ladder reads 0.3761 with a 0.005 dB residual
                                            // over 36 dB, and its own level ladder reads 0.3767 over the top 21 dB.
                                            // Both land on 20*log10(2)/16, a halving every sixteen steps, which is
                                            // what a binary attenuator does. The 0.3795 this replaces came off the
                                            // 0918 recording, where the fit ran into the output path's own droop
                                            // below -68 dB (see fs1r_capture_session2_results.md) and read high.
static const double EG_LEVEL_DB  = 1.5;     // dB per step of the 6-bit EG level registers (LEVTAB >> 1)
static const double CARRIER_DB   = 1.5;     // dB per step of the carrier level correction (voice 0x2D-0x34)
static const double DETUNE_CENTS = 2.0;     // cents per detune step on non-formant operators
static const double FEEDBACK     = 0.5;     // feedback gain = FEEDBACK * 2^(fb - 7)
static const double EG_ATTACK_K  = 0.0625;  // rising EG time constant as a fraction of rate_secs. MEASURED:
                                            // the eleven attack-rate segments of 02_envelope_2 fit an
                                            // exponential in dB with tau/rate_secs = 0.0615 over rates 36 to 44,
                                            // where the 1 ms envelope resolves the whole rise. 1/16 is also what
                                            // the DX7 EGS does, its attack increment being (17 - level/2^24) times
                                            // the decay's, so the constant and the lineage agree. The 0.25 this
                                            // replaces left a rate-24 attack 45 dB below the unit's after 400 ms.
static const double EG_OVERSHOOT = 3.9;     // dB past the TOP OF THE SCALE that a rising EG aims at, not past
                                            // its own target: 10_envelope2's attackto-70 climbs to a target 21 dB
                                            // down at the same speed a climb to full does, which only an approach
                                            // aimed at the top and clamped at the target can do. The DX7's EGS is
                                            // the same shape, its rising increment being (17 - level/2^24) where
                                            // 16 is full scale. MEASURED 3.94 with the floor below free and
                                            // EG_ATTACK_K pinned at 1/16, 0.85 dB rms over 780 envelope points of
                                            // ten segments that reach three different targets from three levels.
static const double EG_ATTACK_FLOOR = -54.5;// dB the EG jumps to when a segment starts rising from below it.
                                            // MEASURED: attackfrom-20 starts its rise at L4 = 58.5 dB down and the
                                            // unit is at 52.7 dB one millisecond in, where attackfrom-50 starts at
                                            // 36 dB down and stays there, so the floor is a jump the chip takes
                                            // from anywhere below it and not merely where a rise from silence
                                            // begins. The DX7 does the same with a jump target of 1716 of 4096,
                                            // which is 55.8 dB below full.
static const double EG_HOLD_FRAC = 0.5;     // the hold segment as a fraction of a full traverse at the hold rate.
static const double EG_HOLD_LAG  = 0.0081;  // ... plus this, fixed. MEASURED over seven hold settings from 19 ms
                                            // to 3.65 s: a free fit of both terms over the five slowest lands on
                                            // 0.4998 of a traverse, so the fraction is exactly a half, and the
                                            // lag is then the mean of what the seven leave over. It scatters by
                                            // about six milliseconds either way, which is two note-ons quantised
                                            // to the 192.3 Hz tick and is as well as a recording can place it.
                                            // Any pure fraction of rate_secs misses the fastest holds by 80 %.
                                            // Hold register 0x3F means no hold at all, which is why the firmware
                                            // leaves that one value alone.
static const double FEG_SEMIS    = 48.0;    // frequency EG range at the full register swing of 128 (four octaves).
                                            // The sysex byte reaches the register through FEGLVL, which is measured
static const double FEG_TIME_K   = 0.3;     // frequency EG time as a fraction of rate_secs
static const double WIN_SKIRT    = 2.0;     // the grain window is sin^p, p = WIN_SKIRT * step^skirt. The
                                            // skirt is the one shape control all1, all2, odd1 and odd2
                                            // have, voice byte 6 being the formant's bandwidth and
                                            // res1/res2's resonance, which those four do not read.
                                            // MEASURED: skirt 0 is sin^2 on every form, which puts the
                                            // two partials either side of a one-period grain 6.02 dB
                                            // down, exactly as the unit does on res1, res2, odd1 and odd2
static const double WIN_SKIRT_STEP = 2.0;   // what the skirt multiplies p by, per step, on all/odd/res.
                                            // MEASURED on odd2 and res2, whose grain is one period long
                                            // under a carrier at an integer multiple of the grain rate,
                                            // so their line amplitudes are the window's own Fourier
                                            // coefficients: p = 2, 4, 8, 16, 32, 64, 128 reproduces them
                                            // to 0.01 dB at skirt 1 and 0.7 dB at skirt 6. all1, odd1 and
                                            // res1 take the same law here and are still 10 to 14 dB out,
                                            // which is what the pairs differ by and is not yet modelled
static const double WIN_SKIRT_FRMT = 1.4142136;  // the same per step for the formant, sqrt(2) rather than
                                            // 2. FITTED against 04_formant_2's sixteen segments at two
                                            // bandwidths; INFERRED as a law, since the fit is per skirt
                                            // and only its slope is closed-form. The family itself is
                                            // still wrong there: no exponent of any sin^p gets the
                                            // formant's skirt closer than 2.6 dB of band shape
static const double FORM_LEVEL   = 0.520;   // what a grain train is worth on every form but the formant,
                                            // against a grain sum normalised by its own window length.
                                            // MEASURED: it puts all1, all2, odd1, odd2 and both resonant
                                            // forms within 0.14 dB of the unit at once, which the ad-hoc
                                            // root-of-the-grain-rate factor it replaces could not. The
                                            // 3.5 dB between the all forms and the odd ones is not a
                                            // constant at all: it falls out of the odd forms retriggering
                                            // twice a period under a window half as long. docs/formant.md.
static const double FRMT_BW_HZ0  = 3.1;     // the formant window's bandwidth in Hz at byte 0 ...
static const double FRMT_BW_DB   = 8.0;     // ... doubling every FRMT_BW_DB steps, so the window lasts
                                            // 1 / (FRMT_BW_HZ0 * 2^(bw / FRMT_BW_DB)) seconds.
                                            //
                                            // MEASURED, and the units are the finding. The window is a
                                            // fixed TIME, not a fixed number of periods of the fundamental:
                                            // sweeping the bandwidth at note 36 and at note 60 gives groups
                                            // of the same width in Hz, 573 against 559 at byte 56 and 4278
                                            // against 4172 at byte 88, while their widths in partials differ by
                                            // the four the two fundamentals differ by. Which is what a
                                            // bandwidth ought to be, and is not what this engine did: it
                                            // held the window at a fixed fraction of the period, so fitting
                                            // it against note 60 gave a knee at byte 44 and against note 36
                                            // a knee at 27, the same law read in the wrong units twice.
                                            // docs/formant.md.
static const double FRMT_WL_MAX  = 2.0;     // and the window never runs longer than this many periods,
                                            // which is two grains overlapping and is what the engine has
                                            // slots for. Below the bandwidth where it binds the group is
                                            // narrower than one partial at either note recorded, so no
                                            // measurement here can see it and the clamp is INFERRED.
static const double FRMT_NORM    = 0.0;     // how a formant's level follows its window length: 0 leaves the
                                            // window's peak at 1, so a wide bandwidth is quiet; 1 holds the
                                            // spectral peak instead, which is what a FOF generator does
static const double NOISE_BASE_HZ= 20.0;    // unvoiced bandwidth 0 lands here ...
static const double NOISE_OCT    = 9.0;     // ... and 0..127 spans this many octaves
static const double NOISE_BW_POW = 0.5;     // noise level vs bandwidth: 0.5 holds the RMS constant, higher
                                            // makes a narrow band louder, as a resonator driven by a pulse
                                            // train would be. The level is held fixed at NOISE_BW_REF.
static const double NOISE_BW_REF = 0.007;   // the one-pole coefficient at bandwidth 20, roughly 54 Hz
// The filter is not the YMP706's: it runs on VOP3-1 and the CPU hands it coefficients, so these come
// from the firmware's own conversions (FUN_0000C36C, FUN_0000C3D0) and only the chip's reading of them
// is a guess. docs/ymp706_registers.md, "The per-voice filter".
static const double CUT_COEF0    = 0xC0D / 32768.0;   // coefficient at cutoff byte 0 ...
static const double CUT_COEF_STEP= 0xA9 / 32768.0;    // ... plus this per step, capped at 0x6000
static const double CUT_COEF_FS  = 48000.0;           // INFERRED: read as a one-pole a = 1 - e^(-2 pi f / fs)
static const double RESO_Q0      = 1.0;     // filter Q at raw resonance 0 (displayed -16) ...
static const double RESO_PER_OCT = 32.0;    // ... doubling every 32 raw steps, for the HPF/BPF/BEF modes only.
                                            // The three lowpasses go through the ladder below instead.
static const double LADDER_K     = 2.0;     // ladder feedback when resonance table A reads 1. Four would be
                                            // the self-oscillation point of a 4-pole ladder; the demo wants a
                                            // much milder peak than that, which is the same thing the old
                                            // single-1/Q reading was saying when it needed RESO_PER_OCT = 32.
static const double RESO_COMP    = 0.0;     // how much of resonance table B is spent lifting the passband on
                                            // top of the ladder's own (1 + k) normalisation. The demo says
                                            // none of it: any broadband lift here shows up directly as a level
                                            // error on the songs whose patches sit near 0 dB already.
static const double FSEQ_DELAY_S = 1.0;     // performance Fseq start delay at its maximum of 99
static const double VCTRL_FREQ   = 8.0;     // voice Formant/FM control: pitch word units per depth step
static const double PMS_FRAC[8]  = {0, 0.0264, 0.0534, 0.0889, 0.1612, 0.2769, 0.4967, 1.0};  // per-op pitch mod sensitivity, DX7 curve
// The output path, MEASURED from rgwan's recording of the whole capture set on 2026-09-18. These three
// are no longer inferred: they come off the digital tap itself. captures/analysis/ holds the numbers and
// docs/capture_0918.md the working.
static const double OUT_GAIN     = 0.14992; // fixed gain between the summed bus and the digital tap. Nine
                                            // single-operator segments agree to 0.005 dB.
static const double CHAN_CLIP    = 1.1919;  // the channel accumulator saturates here, hard and memoryless,
                                            // before the filter loop. stack-4 and stack-8 are driven 4x and
                                            // 8x past one carrier and recover the same ceiling to five places.
static const double FLT_LOSS     = 0.8681;  // flat 1.23 dB the VOP3-1 filter loop costs, constant to 0.02 dB
                                            // across every type, every resonance from 0 to 100, every input
                                            // gain and every cutoff from 48 up. The 0.3190 this replaces read
                                            // 9.93 dB off the same recording on 2026-09-18, because both
                                            // filter files play at note 24 and every request file leaves the
                                            // performance's pan scaling at its extreme, so the segments are
                                            // panned hard and were being measured on the left channel alone
                                            // while the engine's own pan law was 28 % shy. Two errors that
                                            // cancelled; docs/aeg.md, "The pan was in front of everything".
                                            // Re-derived again on 2026-09-19: both filter files drive the
                                            // loop with an all1 operator, and the engine was giving that a
                                            // whole harmonic series the unit does not produce, so the loss
                                            // had been absorbing the spectral form's error too. 9.93, then
                                            // 5.69, now 1.23 dB. docs/formant.md.
}
using cal::FM_INDEX;
using cal::LEVEL_DB;

// ------------------------------------------------------------------------------------------ firmware helpers
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int eb86(int v) { return std::min(255, (((v & 0xFF) << 1) * 0xA5) >> 7); }   // 0..99 -> 0..127
static inline int eb70(int v) { return (((v & 0xFF) << 1) * 0xA5) >> 8; }                   // 0..99 -> 0..127 (bandwidth)
static inline int egrate(int t) { return ((99 - clampi(t, 0, 99)) * 0xA4) >> 8; }          // EG time -> chip rate 0..63
// The chip's own rate scaling: register 0x50 is the operator's time scaling 0..7 and register 0xC0 the key code,
// and it adds (tscale * keyoff) / 8 to every rate, truncating toward zero. The 24 tscale segments of 02_envelope_3
// and the 20 tkey segments of 10_envelope2 recover the rate exactly off the (4 + (q & 3)) << (q >> 2) ladder, and
// between them they pin keyoff at ten key codes, four apart, which is one note per octave from 12 to 120. It
// saturates at both ends: notes 12 and 24 both read -13, and note 120 reads +13 where 108 reads +12. Nothing
// linear fits the ten, a search over every trunc((a * c0 + b) / c) with a and c under 32 comes back empty, so
// this is a table on the chip. Four key codes is three semitones, and the engine interpolates between the
// samples it has. Sweeping 0xC0 as a register is what closes the gap: FS1R.unlock/docs/unknowns.md experiment 8.
static const int EG_KEYOFF[10] = {-13, -13, -10, -5, -2, 2, 4, 8, 12, 13};   // key codes 77, 81, 85 ... 113
static inline int eg_keyoff(int c0) {
    int i = (c0 - 77) >> 2;
    if (i < 0) return EG_KEYOFF[0];
    if (i >= 9) return EG_KEYOFF[9];
    int a = EG_KEYOFF[i], b = EG_KEYOFF[i + 1];
    return a + ((b - a) * ((c0 - 77) & 3) + 2) / 4;
}
static inline int eg_ratescale(int tscale, int c0) {
    int x = (tscale & 7) * eg_keyoff(c0);
    return x < 0 ? -((-x) >> 3) : x >> 3;                                                 // truncates toward zero
}
// Pan key scaling, MEASURED off 10_envelope2 on 2026-09-19. Every request file leaves the performance's
// PAN SCALING byte at 0, the extreme, so the recording sweeps the pan right across the keyboard and reads
// the law out: the index is 64 - 4 * (note - 60) / 3, hard right at note 12 and hard left from note 108
// up, which matches all ten notes and both ends of the table. That is 64 index steps per 48 semitones at
// byte 0, where the engine had 50, so every pan-scaled patch was 28 % shy of the unit. In the firmware's
// own 0..255 pan domain (docs/ymp706_registers.md, Pan) a byte of 0 is the whole 128 of one side.
static inline int pan_index(int base, int scaling, int note) {
    return base + ((clampi(scaling, 0, 100) - 50) * 64 / 50) * (note - 60) / 48;
}
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
static float g_win[2][8][1025];       // [formant?][skirt]: the two window families
static float g_db2lin[2305];                                          // -128 .. +16 dB in 1/16 dB steps
static void init_tables() {
    for (int i = 0; i <= 4096; i++) g_sin[i] = (float)sin(2 * PI * i / 4096.0);
    for (int i = 0; i <= 2304; i++) g_db2lin[i] = (float)pow(10.0, (i / 16.0 - 128.0) / 20.0);
    // MEASURED 2026-09-19, FS1R.unlock's skirt sweep: the grain window is sin^p and the skirt multiplies
    // p, it does not step it. On odd2 and res2, whose grain is exactly one period long under a carrier at
    // an integer multiple of the grain rate, p = 2 * 2^skirt reproduces the unit's line amplitudes to
    // 0.01 dB at skirt 1 and under 0.7 dB out to skirt 6, which is the measurement's own floor. The
    // formant goes slower, p = 2 * sqrt(2)^skirt, fitted against 04_formant_2's sixteen segments at two
    // bandwidths; no exponent of any sin^p family gets its shape closer than 2.6 dB, so the family itself
    // is still wrong there and this is the best member of it. docs/formant.md.
    for (int f = 0; f < 2; f++)
        for (int s = 0; s < 8; s++) {
            double p = cal::WIN_SKIRT * pow(f ? cal::WIN_SKIRT_FRMT : cal::WIN_SKIRT_STEP, s);
            for (int i = 0; i <= 1024; i++) g_win[f][s][i] = (float)pow(sin(PI * i / 1024.0), p);
        }
}
static inline float fsin(double ph) { double x = (ph - floor(ph)) * 4096.0; int i = (int)x; float f = (float)(x - i); return g_sin[i] + (g_sin[i + 1] - g_sin[i]) * f; }
static inline float fwin(int fam, int s, double x) { double y = x * 1024.0; int i = (int)y; if (i >= 1024) return 0.f; float f = (float)(y - i); const float* w = g_win[fam][s]; return w[i] + (w[i + 1] - w[i]) * f; }
// The per-sample level path: the table above with linear interpolation (error under 1e-5) instead of a
// pow() per operator per sample. Anything above the table is rare enough to compute.
static inline double db2lin_fast(double db) {
    if (db <= -128) return 0.0;
    if (db >= 16) return db2lin(db);
    double x = (db + 128) * 16.0; int i = (int)x; float f = (float)(x - i);
    return g_db2lin[i] + (g_db2lin[i + 1] - g_db2lin[i]) * f;
}
// INFERRED: chip EG rate 0..63 -> seconds for a full 96 dB traverse. The firmware maps time T to rate (99-T)*0xA4>>8, the
// exact inverse of the DX7's (R*41)>>6, and its DX7 converter uses T = 99 - R, so the chip is assumed to time its EG like the
// DX7 EGS: increment (4 + (q & 3)) << (q >> 2) per 64 samples on a 2^28 = 96 dB scale (Dexed). 6.6 ms at 63, 380 s at 0.
static inline double rate_secs(int q) { q = clampi(q, 0, 63); return pow(2.0, 26 - (q >> 2)) / (4 + (q & 3)) / SR; }
#include "fs1r_effects.h"

// FUN_0000C36C: the cutoff byte reaches VOP3-1 as a coefficient, 0xC0D + 0xA9 per step capped at
// 0x6000, so it is linear in the coefficient rather than in octaves. Reading that coefficient as a
// one-pole's a = 1 - e^(-2 pi f / fs) puts byte 0 at 755 Hz and byte 127 at 10.6 kHz. The formula is
// the firmware's; the reading is INFERRED and is what a recording would calibrate.
static inline double cut_hz(double c) {
    double a = cal::CUT_COEF0 + cal::CUT_COEF_STEP * clampi((int)c, 0, 127);
    return -log(1.0 - std::min(a, 0.999)) * cal::CUT_COEF_FS / (2 * PI);
}
// FUN_0000C3D0 sends TWO resonance coefficients per channel, not one. Table 0x374B24 is
// A = 1 - 2^(-raw/16) = 1 - r, and 0x374C24 is B = max(0, 0.5 - 2r^2), quadratic in the same damping and
// zeroed by the firmware for HPF and BEF. Two coefficients around a single shared cutoff coefficient is a
// ladder with feedback and passband compensation, which is also what FUN_0000CA44's per-type input scaler
// (0x40 vs 0x7F) and +-0x4000 tap mix want. Reading A alone as 1/Q is what forced RESO_PER_OCT to 32.
// INFERRED is what the two become: A scales the ladder's feedback and B is available as passband lift.
// The demo settled both scalings, and neither landed on the textbook value. See TODO.md, Tier 4.
static inline double reso_r(int r) { return pow(2.0, -clampi(r + 16, 0, 116) / 16.0); }  // fltReso = sysex byte - 16, so r+16 is the byte: A[byte] = 1 - 2^(-byte/16), measured on hardware (capture3's convhand)
static inline double reso_q(int r) { return cal::RESO_Q0 * pow(2.0, clampi(r + 16, 0, 116) / cal::RESO_PER_OCT); }
static inline double reso_fb(int r) { return cal::LADDER_K * (1.0 - reso_r(r)); }
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
    void go(int s) { stage = s; target = L[s]; k = 1.0 - exp(-1.0 / (rate_secs(clampi(R[s], 0, 63)) * 0.25 * TICK_HZ + 1)); }
    void release() { go(3); }
    inline double tick() {
        if (stage > 3) return cur;
        cur += (target - cur) * k;
        if (fabs(target - cur) < 1e-4) { cur = target; if (stage < 2) go(stage + 1); else stage = 9; }
        return cur;
    }
};

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
    int lfo2wave, lfo2speed, lfo2phase, lfo2sync;
    int fltType, fltReso, fltResoVel, fltCut, fltEgVel, fltLfo1, fltLfo2, fltKsDepth, fltKsPoint, fltInGain, fegDepth;
    int fltL[4], fltT[4], fltAtkVel, fltTscale;
    int noteshift, pegL[5], pegT[4], pegVel, pegRange, pegTscale;
    int fseqV, fseqU, alg, corr[8], fb;
    OpV v[8]; OpU u[8];
};
static void decode_voice(Voice& V) {
    const uint8_t* b = V.raw;
    memcpy(V.name, b, 10); V.name[10] = 0; V.cat = b[0x0E];
    V.lfo1wave = std::min<int>(b[0x10], 5); V.lfo1speed = b[0x11]; V.lfo1delay = b[0x12]; V.lfo1sync = b[0x13] & 1;
    V.pmd = b[0x15]; V.amd = b[0x16]; V.fmd = b[0x17];
    V.lfo2wave = std::min<int>(b[0x18], 5); V.lfo2speed = b[0x19]; V.lfo2phase = b[0x1C] & 3; V.lfo2sync = b[0x1D] & 1;
    V.fltType = std::min<int>(b[0x54], 5); V.fltReso = (b[0x55] & 0x7F) - 16; V.fltResoVel = (b[0x56] & 0xF) - 7;
    V.fltCut = b[0x57]; V.fltEgVel = (b[0x58] & 0xF) - 7; V.fltLfo1 = b[0x59]; V.fltLfo2 = b[0x5A];
    V.fltKsDepth = b[0x5B] - 64; V.fltKsPoint = b[0x5C]; V.fltInGain = (b[0x5D] & 0x1F) - 12; V.fegDepth = b[0x64] - 64;
    V.fltL[0] = b[0x66]; V.fltL[1] = b[0x67]; V.fltL[2] = b[0x68]; V.fltL[3] = b[0x65];   // L1 L2 L3, L4 is the start level
    for (int i = 0; i < 4; i++) V.fltT[i] = b[0x69 + i];
    V.fltAtkVel = b[0x6E] >> 3 & 7; V.fltTscale = b[0x6E] & 7;
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
    b[0x55] = 16; b[0x56] = b[0x58] = 7; b[0x57] = 127; b[0x5B] = b[0x5C] = 64; b[0x5D] = 12; b[0x64] = 64;
    b[0x65] = b[0x66] = b[0x67] = b[0x68] = 50;
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
// DX7 ACED (49 bytes, DX7II/DX7S/TX802 additions). The Data List: "ACED bulk data is not interpreted
// until its following VCED bulk data is received", so it is held here and applied after the conversion.
// Only nine of the 49 bytes carry anything: six per-operator amplitude mod senses, the pitch EG range,
// its velocity switch and its rate scaling.
static uint8_t g_aced[49];
static bool g_acedValid = false;
static void apply_aced(uint8_t* out) {
    if (!g_acedValid) return;
    for (int j = 0; j < 6; j++) {                 // ACED 6..11 are OP6..OP1, the same order as the VCED
        uint8_t* p = out + 112 + (2 + j) * 62;
        p[33] = (uint8_t)(((g_aced[6 + j] & 7) << 4) | (p[33] & 0x0F));
    }
    out[0x3B] = g_aced[0x0C] & 3;                 // pitch EG range
    out[0x27] = g_aced[0x0E] ? 7 : 0;             // pitch EG velocity switch
    out[0x3C] = g_aced[0x26] & 7;                 // pitch EG rate scaling
    g_acedValid = false;
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
    apply_aced(out);
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
struct Rom;                                  // defined with the loaders below
static void rom_voice(const Rom& R, int idx, Voice& V);
static int bank_voice_index(int bank, int prog);
static bool rom_ready(const Rom* R);
// Performance banks in the EPROM table of 384: the three preset banks, 128 each, in order A, B, C.
// The Data List's performance lists match them entry for entry - "Zap !" is Preset A 1, "Sweepy Voice"
// Preset B 1 and "UprightPiano" Preset C 1 - and the manual's own description of Preset C settles that
// last one on its own: it is the bank for the G50 guitar controller, "the maximum MIDI receive channel
// for these voices is 6, and the pitch bend range is -12 ... +12" (owner's manual page 21), which is
// exactly what entries 256-383 hold and nothing else does. INTERNAL is the user's own battery-backed
// bank, not ROM, so a bank select for it lands on Preset A, which is what the unit ships holding.
static int bank_perf_index(int lsb, int prog) {
    int base = lsb == 0x42 ? 128 : lsb == 0x43 ? 256 : 0;   // PrB, PrC, else Int and PrA
    return clampi(base + (prog & 0x7F), 0, 383);
}

struct Part {
    uint8_t p[52]; Voice voice;
    // controller state (MIDI)
    int bend = 0;            // (msb - 64) * 16, the firmware keeps the MSB only
    int expr = 254;          // DAT_010297fa
    // Controller sources in the firmware's own bit order (FUN_000191C0): KN1-4, MC1, MC2, PB, CAT,
    // PAT, FC, BC, MC3, MW, MC4. The knobs and MIDI controls are stored bipolar as (v - 64) * 2, the
    // physical controllers as the raw value, so the range is -128..127 either way.
    int src[14] = {};
    int held[32]; int nheld = 0; int lastPitch = -1;   // mono handling and portamento start
    int rpnM = 127, rpnL = 127; bool nrpnSel = false;   // RPN / NRPN selection state
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
        pt.p[0x28] = 50; pt.p[0x2E] = pt.p[0x2F] = 64;   // pan scaling +0, LFO2 rate / filter mod offsets 0
        pt.p[0x25] = 0; pt.p[0x26] = 0x40 + 2; pt.p[0x27] = 0x40 - 2; pt.p[0x2A] = 1; pt.p[0x2B] = 127; pt.p[0x2D] = 1; pt.p[0x2E] = pt.p[0x2F] = 64;
        init_default_voice(pt.voice);
    }
    memcpy(P.name, P.c, 12); P.name[12] = 0;
}

// ------------------------------------------------------------------------------------------ channel state
struct EG {                      // amplitude EG on the chip: hold, 4 segments. INFERRED shape/timing, DX7 style
    int stage = 5; double cur = -200, target = -200, rate = 0, holdLeft = 0; bool rising = false;
    int L[4] = {}, R[4] = {}; int hold = 0; int rs = 0;
    static double lvl_db(int a) { return a >= 63 ? -200.0 : -cal::EG_LEVEL_DB * a; }   // 6-bit attenuation (LEVTAB >> 1), 1.5 dB per step INFERRED
    void start(const int* lv, const int* rt, int h, int rateScale) {
        for (int i = 0; i < 4; i++) { L[i] = lv[i]; R[i] = rt[i]; } hold = h; rs = rateScale; cur = lvl_db(L[3]); stage = 0;
        int hr = egrate(h); if (hr < 0x3F) hr = std::min(hr + 4, 0x3E);   // FUN_00019414: +4 only below 0x3F
        // 0x3F is the firmware's "no hold", the one value it does not offset. Anything else holds for half a
        // traverse at that rate plus a fixed lag, both measured.
        holdLeft = hr < 0x3F ? (rate_secs(hr + rs) * cal::EG_HOLD_FRAC + cal::EG_HOLD_LAG) * SR : 0;
        if (holdLeft < 1) next(1);
    }
    void next(int s) {
        stage = s; if (s > 4) { target = -200; return; }
        target = lvl_db(L[s - 1]);
        double secs = rate_secs(R[s - 1] + rs);
        rising = target > cur;
        // A rising segment does not crawl up from silence: the chip is already at EG_ATTACK_FLOOR one
        // millisecond in, so the climb starts there, or at the target if that is lower still.
        if (rising && cur < cal::EG_ATTACK_FLOOR) cur = std::min(target, cal::EG_ATTACK_FLOOR);
        rate = rising ? 1.0 - exp(-1.0 / (secs * cal::EG_ATTACK_K * SR + 1)) : 96.0 / (secs * SR + 1);
    }
    void release() { if (stage < 4) next(4); }
    inline double tick() {
        if (stage == 0) { if (--holdLeft <= 0) next(1); return cur; }
        if (stage > 4) return cur;
        if (rising) { cur += (cal::EG_OVERSHOOT - cur) * rate; if (cur >= target) { cur = target; if (stage < 3) next(stage + 1); else if (stage == 4) stage = 5; } }
        else { cur -= rate; if (cur <= target) { cur = target; if (stage < 3) next(stage + 1); else if (stage == 4) stage = 5; } }
        return cur;
    }
    bool done() const { return stage == 5 || (stage == 4 && cur <= -120); }
};
struct FreqEG {                  // init -> attack level -> 0. The level curve is the firmware's, read back off the
                                 // voice images (FEGLVL, register 0x60/0x68); the range and the time curve are INFERRED.
    double cur = 0, target = 0, k = 0, kdec = 0; int stage = 2;
    // The sysex byte is not linear in the register: FEGLVL maps 0..100 onto 0..255 with 128 the centre, and
    // half the displayed depth is only a fifth of the register offset. 128 register steps = FEG_SEMIS.
    static double feg_semis(int v) { return (FEGLVL[clampi(v + 50, 0, 100)] - 128) * cal::FEG_SEMIS / 128.0; }
    void start(int init, int att, int attT, int decT) {
        cur = feg_semis(init); target = feg_semis(att); stage = (init == 0 && att == 0) ? 2 : 0;
        k = 1.0 - exp(-1.0 / (rate_secs(egrate(attT)) * cal::FEG_TIME_K * SR + 1)); kdec = 1.0 - exp(-1.0 / (rate_secs(egrate(decT)) * cal::FEG_TIME_K * SR + 1));
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
    double att = 0, fop = 0, wl7 = 1, uatt = 0, nf = 0, na = 1, nscale = 0; int bw = 0, ratio = 0;   // refresh_ctl
};
struct Chan {
    bool active = false; int part = 0, note = 0, vel = 0; bool held = false, sustained = false; uint32_t age = 0;
    // CPU side, computed at note-on
    int noteP = 60;            // note after all shifts (DAT_0103937a)
    int pitchNote = 0;         // NOTETAB + detune + tune (DAT_01028a84)
    int keyfact = 0;           // KEYFACT >> 2
    int levelOff[8], ulevelOff[8], freqWord[8], ufreqWord[8], frmtWord[8], bwReg[8], ubwReg[8];
    int egbias[8], uegbias[8]; // image 0x1C0/0x1C8
    int fbW[8] = {}, ufbW[8] = {};   // frequency bias words, DAT_010282bc / DAT_010283bc
    int egL[8][4], egR[8][4], egHold[8], uegL[8][4], uegR[8][4], uegHold[8];
    // pitch EG (software, FUN_00028c8e)
    int pegStage = 5, pegCur = 0, pegTarget = 0, pegRate = 255, velP = 128;
    int pegLvl[5], pegRt[5];
    // portamento (FUN_00029048)
    int portaCur = 0, portaTarget = 0, portaRate = 0; bool portaOn = false;
    // LFO1 (FUN_00028828)
    uint32_t lfoPhase = 0, lfoDelay = 0, lfoFade = 0; int lfoVal = 0; int lfoSH = 0;
    // LFO2 (filter only) and the pan / filter registers (0x22A-0x22F, 0x270)
    uint32_t lfo2Phase = 0; int lfo2Val = 0, lfo2SH = 0;
    int panBase = 63; double panL = 1, panR = 1;
    StepEG feg; VFilter flt; int fltType = 0; bool fltOn = false; double fltGain = 1, fltInGain = 1;
    int vcLvl[8][2] = {}, vcFreq[8][2] = {}, vcBw[8][2] = {};   // voice Formant/FM control offsets, [op][voiced=0]
    // registers refreshed each tick
    int regPitch = 0, regPM = 0, regFM = 0, regAM = 0, regLevel[8], regULevel[8], regC0 = 73;
    int fqWord[8] = {}, fquWord[8] = {}; double partV = 1, partU = 1;
    OpState op[8]; double fbBus = 0, fbPrev = 0;
    double f0 = 0, fbGain = 0; int ctlLeft = 0;                    // control-rate cache, see refresh_ctl
    bool fseqOp[8] = {}, fseqUOp[8] = {};
};

struct Synth {
    Perf perf; Chan ch[NCHAN]; uint32_t clock = 0; double gain = 1.0;   // the analogue volume pot, after the tap
    FxSection fx;
    const Rom* rom = nullptr;
    uint8_t sys[76] = {};                      // system parameters, sysex table 4
    int sysTune() const { return sys[0]; }
    int sysNoteShift() const { return sys[6]; }
    int velCurve() const { return sys[0x0E]; }
    int perfChannel() const { return sys[9]; }             // 0-15, 0x10 = all, 0x7F = off
    int devNumber() const { return sys[0x49]; }
    std::vector<std::vector<uint8_t>> outQ;                // sysex the engine sends back (dump / parameter replies)
    mutable std::mutex mtx;
    // Fseq playback
    Fseq fseq; bool fseqRun = false; double fseqAcc = 0, fseqPeriod = 0.01; int fseqStep = 0, fseqDir = 1; int fseqVel = 100; int fseqPart = -1;
    bool fseqHeld = false, fseqClock = false; double fseqDelay = 0, fseqClockAcc = 0;
    double tickAcc = 0;

    Synth() { init_perf(perf); fx.init(SR); init_system(); }
    void init_system() {
        memset(sys, 0, sizeof sys);
        sys[0] = 64; sys[6] = 64; sys[8] = 0; sys[9] = 0; sys[0x0E] = 0;
        sys[0x10] = sys[0x12] = sys[0x13] = sys[0x14] = sys[0x15] = 1;
        static const uint8_t cc[12] = {16, 17, 18, 19, 20, 21, 22, 13, 4, 2, 80, 81};   // KN1-4 MC1-4 FC BC Formant FM
        memcpy(sys + 0x16, cc, 12);
        sys[0x47] = 0; sys[0x48] = 4; sys[0x49] = 0; sys[0x4A] = 0;
    }
    static inline double sendlvl(int v) { return db2lin(-LEVEL_DB * SENDTAB[clampi(v, 0, 127)]); }

    // ---------------------------------------------------------------- per-part derived values
    // ---------------------------------------------------------------- controller sets (FUN_00014DDC)
    // Each of the eight sets sums the sources its 14-bit bitmap enables, then hands the sum and the
    // set's depth to a per-destination handler out of the table at flash 0x3DC04. There are three
    // scalers, and which one a destination uses is exactly the split the owner's manual describes:
    // destinations that "overwrite the edit buffer" edit a part byte, destinations that "directly
    // control the tone generator" return a signed offset.
    //   FUN_00016F9C  clamp((v' * d' * 2) >> 6, -128, 127)             destinations 1-16 and 34-47
    //   FUN_00016F58  clamp(base + ((v' * d' * 2) >> 7), 0, 127)       destinations 18 and 21-33
    //   FUN_00017032  clamp(base + adj((v' * d' * 2) >> 6), 0, 127)    destinations 17, 19, 20
    // with v' = v + (v > 0), d' = d + (d > 0), and adj taking one off a positive result.
    // The part switch at performance common 0x28 + n gates destinations 17-45 only; 1-16 (the insertion
    // parameters and its sends) and 46-47 (the Fseq) are performance-wide and ignore it.
    static int ctrl_bias(int v) { return v + (v > 0 ? 1 : 0); }
    static int scale_f9c(int v, int d) { return clampi((ctrl_bias(v) * ctrl_bias(d) * 2) >> 6, -128, 127); }
    static int scale_f58(int v, int d, int base) { return clampi(base + ((ctrl_bias(v) * ctrl_bias(d) * 2) >> 7), 0, 127); }
    static int scale_f032(int v, int d, int base) {
        int r = clampi((ctrl_bias(v) * ctrl_bias(d) * 2) >> 6, -128, 32767);
        if (r > 0) r = std::min(r - 1, 127);
        return clampi(base + r, 0, 127);
    }
    // The sum for one set: walk the bitmap from bit 0 up, clamping to a signed byte after each source.
    int ctrl_set_sum(int part, int set) const {
        int bits = perf.c[0x30 + 2 * set] << 7 | perf.c[0x31 + 2 * set], sum = 0;
        for (int b = 0; b < 14; b++) if (bits & (1 << b)) sum = clampi(sum + perf.part[part].src[b], -128, 127);
        return sum;
    }
    bool ctrl_set_active(int part, int set, int dest) const {
        int d = perf.c[0x40 + set] & 0x3F;
        if (!d || d >= 0x30 || d != dest) return false;
        if (dest >= 17 && dest <= 45 && !(perf.c[0x28 + set] & (1 << part))) return false;
        return true;
    }
    // A "direct" destination. Every one of these handlers stores a per-part byte rather than adding to
    // one (ctrlDest_36 writes DAT_01029748[part], ctrlDest_37 DAT_01029758, ctrlDest_39 DAT_0102979a),
    // and FUN_00014DDC walks the sets 0 to 7, so two sets on one destination do not compose: the last
    // one evaluated wins, exactly as for the destinations that edit a part byte.
    int ctrl_offset(int part, int dest) const {
        for (int s = 7; s >= 0; s--)
            if (ctrl_set_active(part, s, dest))
                return scale_f9c(ctrl_set_sum(part, s), perf.c[0x48 + s] - 64);
        return 0;
    }
    // A destination that edits a part byte: the firmware writes the byte, so the last set that selects
    // it wins rather than the offsets adding up.
    int ctrl_part(int part, int dest, int stored) const {
        bool wide = dest == 17 || dest == 19 || dest == 20;     // volume and the two sends use FUN_00017032
        for (int s = 7; s >= 0; s--)
            if (ctrl_set_active(part, s, dest)) {
                int v = ctrl_set_sum(part, s), d = perf.c[0x48 + s] - 64;
                return wide ? scale_f032(v, d, stored) : scale_f58(v, d, stored);
            }
        return clampi(stored, 0, 127);
    }
    // Destination 34 is its own shape: the depth is the bend range in semitones, not a scaler
    // (ctrlDest_34, and the manual: "if Vcn depth is set to +2 the maximum control value is +2
    // semitones"). The summed source drives it like a bend wheel.
    int ctrl_pitch_bias(int part) const {
        int w = 0;
        for (int s = 0; s < 8; s++) {
            if (!ctrl_set_active(part, s, 34)) continue;
            int v = ctrl_set_sum(part, s), d = clampi(perf.c[0x48 + s] - 64, -24, 24);
            int b = BENDTAB[std::abs(d)];
            if (d < 0) b = -b;
            w += (b * (v == 0x7F ? 0x80 : v)) >> 7;
        }
        return w;
    }
    // Destination 35 writes one per-part value that event 0x205 then scales by each operator's own EG
    // bias sense: ~((|scaled| + table[|depth|]) * 2), the table being flash 0x3DDC4 (ctrlDest_35).
    int ctrl_eg_bias(int part) const {
        static const short DEPTHTERM[32] = {127, 119, 115, 111, 107, 103, 99, 95, 91, 87, 83, 79, 75, 71, 67, 63,
                                            59, 55, 51, 47, 43, 39, 35, 31, 27, 23, 19, 15, 11, 7, 3, 0};
        // No set assigned to 35 means no bias at all. 255 would be full bias, which is what an assigned
        // set at full depth gives while its source sits at rest, so the two must not share a default:
        // EGBIAS[255] is 255, and a positive sense of 3 then buries the operator 35 dB down.
        int out = -1;
        for (int s = 0; s < 8; s++) {
            if (!ctrl_set_active(part, s, 35)) continue;
            int d = perf.c[0x48 + s] - 64;
            int r = (std::abs(scale_f9c(ctrl_set_sum(part, s), d)) + DEPTHTERM[std::min(31, std::abs(d))]) * 2;
            if (r > 0xFD) r = 0xFF;
            out = (~r) & 0xFF;                  // ctrlDest_35 stores DAT_01029738[part]: the last set wins
        }
        return out < 0 ? 0 : out;
    }
    int bend_word(const Part& pt) const {                // FUN_00020194 + pitch bias controller (dest 34)
        int b = pt.bend; int r = (b < 0 ? pt.p[0x27] : pt.p[0x26]) - 0x40; int v;
        if (r < 0) { v = BENDTAB[std::min(48, -r)]; if (b >= 0) v = -v; } else { v = BENDTAB[std::min(48, r)]; if (b < 0) v = -v; }
        int k = b >> 3; if (k == 0x7F) k = 0x80;
        int w = (v * k) >> 7;
        w += ctrl_pitch_bias((int)(&pt - perf.part));
        return w;
    }
    void part_levels(int part, int& vAtt, int& uAtt) const {   // FUN_00020044 -> registers 0x228/0x229 (+0x10 dropped here)
        const Part& pt = perf.part[part];
        int expr = clampi(pt.expr, 0, 254);
        int vol = ctrl_part(part, 17, pt.p[0x0B]);
        int u1 = ((vol + 1) * expr) >> 8; int iu = u1 + 1, iv = u1 + 1;
        int bal = ctrl_part(part, 31, pt.p[0x0A]);
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
        vel = clampi(((pt.p[0x0C] * VELCURVES[clampi(velCurve(), 0, 4) * 128 + vel]) >> 6) + (pt.p[0x0D] - 0x40) * 2, 1, 127);
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
        C.lfo2Phase = V.lfo2sync ? (uint32_t)(V.lfo2phase * 0x4000) : (uint32_t)(rand() & 0xFFFF);
        C.panBase = pt.p[0x0E] ? pt.p[0x0E] : (rand() % 128);       // part pan 0 = random per note; the rest is the table index
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
            s.eg.start(C.egL[o], C.egR[o], C.egHold[o], eg_ratescale(v.tscale, C.regC0));
            s.feg.start(v.fegInit, v.fegAtt, v.fegAttT, v.fegDecT);
            s.phase = v.keysync ? 0.0 : (double)rand() / RAND_MAX;
            s.ueg.start(C.uegL[o], C.uegR[o], C.uegHold[o], eg_ratescale(u.tscale, C.regC0));
            s.ufeg.start(u.fegInit, u.fegAtt, u.fegAttT, u.fegDecT);
            s.rng = 0x9E3779B9u * (o + 1) ^ clock; if (!s.rng) s.rng = 1;
        }
        start_filter(C, pt, vel);
    }
    // per-voice filter (voice common 0x54-0x6E, part 0x07/0x18/0x19/0x1F). Chip side, so INFERRED.
    void start_filter(Chan& C, const Part& pt, int vel) {
        const Voice& V = pt.voice;
        C.fltOn = (pt.p[7] & 1) != 0; C.fltType = V.fltType; C.flt.clear();
        C.fltInGain = db2lin(V.fltInGain); C.fltGain = C.fltInGain * cal::FLT_LOSS;
        double lv[4]; int rt[4];
        int ts = (V.fltTscale * C.keyfact) >> 5;
        for (int i = 0; i < 4; i++) { lv[i] = V.fltL[i] - 50; rt[i] = clampi(egrate(V.fltT[i]) + ts, 0, 63); }
        rt[0] = clampi(rt[0] + ((V.fltAtkVel * (vel - 64)) >> 5), 0, 63);   // attack time velocity
        C.feg.start(lv, rt);
    }
    // The Fseq retrigger test is the part's own held-note count, not the machine's: FUN_0001228c
    // increments DAT_010288dc[part] and calls the trigger only when that count reaches 1.
    bool anyOther(const Chan& C) const { for (auto& x : ch) if (&x != &C && x.active && x.held && x.part == C.part) return true; return false; }
    // The frame writer (FUN_000197b4) runs whenever the mode byte is 1 or 2 and the part matches; it is
    // not gated on the sequence still advancing, so a sequence that has played out holds its last frame
    // on the registers. The start delay is the one thing that does gate it: FUN_000125f8 clears the
    // track masks while DAT_010291ca is counting, so until the delay expires the operators are the
    // voice's own.
    bool fseq_on(int part) const {
        int m = perf.c[0x21] & 3;
        return fseq.valid && fseqPart == part && (m == 1 || m == 2) && fseqDelay <= 0;
    }

    // Everything the controller matrix feeds an operator, rebuilt on the tick rather than at note-on:
    // FUN_00014DDC re-runs the destination handler every time one of its sources moves, and
    // FUN_0001B334 (voiced) and FUN_0001D056 (unvoiced) then rebuild the whole per-part array, so a
    // held note follows the wheel. The frequency bias reaches the formant and fixed operators only,
    // since the ratio branch of FUN_0001E838 adds nothing, and the unvoiced one only in normal mode.
    void refresh_bias(Chan& C, const Part& pt) {
        const Voice& V = pt.voice; int part = C.part;
        int cf = ctrl_offset(part, 36), cv = ctrl_offset(part, 37), cu = ctrl_offset(part, 38), cb = ctrl_eg_bias(part);
        for (int o = 0; o < 8; o++) {
            const OpV& v = V.v[o]; const OpU& u = V.u[o];
            int s = v.fbias - 7;
            C.fbW[o] = (s && (v.form == 7 || v.fixed)) ? (int16_t)(cf * s * 0x10) >> 2 : 0;
            s = u.fbias - 7;
            C.ufbW[o] = (s && u.mode == 0) ? (int16_t)(cf * s * 0x10) >> 2 : 0;
            int bo = BWBIAS[v.bwbias & 0xF] ? clampi((cv * BWBIAS[v.bwbias & 0xF]) >> 3, -128, 127) : 0;
            C.bwReg[o] = clampi(v.bw + bo, 0, 99);
            int ubo = BWBIAS[u.bwbias & 0xF] ? clampi((cu * BWBIAS[u.bwbias & 0xF]) >> 3, -128, 127) : 0;
            C.ubwReg[o] = clampi(eb70(u.bw) + ubo, 0, 127);
            // EG bias attenuation (event 0x205): |sens| * EGBIAS[ctrl or 255-ctrl] >> 3
            auto bias = [&](int sn) { if (!sn) return 0; int idx = sn < 0 ? 255 - cb : cb; return std::min(255, (std::abs(sn) * EGBIAS[idx]) >> 3); };
            C.egbias[o] = bias(v.egbias); C.uegbias[o] = bias(u.egbias);
        }
    }

    // FUN_00010dc4: note shifts, note table, part detune, master tune
    void compute_pitch(Chan& C, const Part& pt, int note) {
        int n = clampi(note + (sysNoteShift() & 0x7F) - 0x40, 0, 127);
        n = clampi(n + (perf.c[0x12] & 0x3F) - 24, 0, 127);
        n = clampi(n + (pt.p[8] & 0x3F) - 24, 0, 127);
        n = clampi(n + (pt.voice.raw[0x1E] & 0x3F) - 24, 0, 127);
        C.noteP = n; C.keyfact = KEYFACT[n] >> 2;
        int pitch = NOTETAB[n];
        int d = pt.p[9] - 0x40;
        if (d) { int band = (n >> 3) & 0xF; int prod = std::abs(d) * (15 - band); if (!prod) prod = 1; pitch += (d < 0 ? -prod : prod) / 15; }
        pitch += (sysTune() & 0x7F) - 0x40;
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
            C.levelOff[o] = std::min(255, vel_att(v.amsb, vel) + ks);
            // unvoiced: 2*LEVTAB + velocity, then level key scaling
            {
                int a = std::min(255, LEVTAB[std::min(99, u.level)] * 2 + vel_att(u.amsb, vel));
                a += -(u.lks * 64 * (n - 60)) >> 8;
                C.ulevelOff[o] = clampi(a, 0, 255);
            }
            // frequency words. The bias the controller matrix adds to them is not here: it moves while
            // the note sounds, and refresh_bias rebuilds it every tick.
            if (v.form == 7 || v.fixed) C.freqWord[o] = 8 * (v.coarse * 128 + v.fine) + 0x28ED + keytrack(v.notescale, pm) + fvs_term(v.fmsb, vel);
            else C.freqWord[o] = 0x1243 + COARSE[v.coarse] + FINE[std::min(99, v.fine)];
            C.ufreqWord[o] = std::min(0x7F00, ((u.coarse & 0x1F) * 256 + u.fine * 2) * 4 + 0x28ED + keytrack(u.notescale, pm) + fvs_term(u.fmsb, vel));
            // formant transpose word (register 0x230): 0x1243 + TRANS + note dependent detune for frmt ops, else the raw byte 6
            if (v.form == 7) {
                int d = v.detune; int dd = d < 15 ? (~d) : d - 15; int idx = (dd & 0x1F) * 32 + band; int det = FRMDET[idx & 0x1FF]; if (idx & 0x200) det = -det;
                C.frmtWord[o] = 0x1243 + TRANS[clampi(v.transpose + 24, 0, 48)] + det;
            } else C.frmtWord[o] = v.bw;
            // EG registers: levels LEVTAB >> 1, rates ((99-T)*0xA4)>>8, part EG offsets on attack/decay/release (assumed T1/T2/T4)
            // FUN_00019414 walks the part's EG bytes 0x1A, 0x1B, 0x1B, 0x1C against voice times T1-T4,
            // so the decay offset reaches both T2 and T3, not T2 alone.
            int atk = ctrl_part(part, 24, pt.p[0x1A]), dec = ctrl_part(part, 25, pt.p[0x1B]), rel = ctrl_part(part, 26, pt.p[0x1C]);
            int offs[4] = {atk - 64, dec - 64, dec - 64, rel - 64};
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
        Lp[0] = clampi(Lp[0] + ctrl_part(C.part, 27, pt.p[0x20]) - 64, 0, 100);
        Lp[4] = clampi(Lp[4] + ctrl_part(C.part, 29, pt.p[0x22]) - 64, 0, 100);
        Tp[0] = 255; for (int i = 0; i < 4; i++) Tp[i + 1] = V.pegT[i];
        Tp[1] = clampi(Tp[1] + ctrl_part(C.part, 28, pt.p[0x21]) - 64, 0, 99);
        Tp[4] = clampi(Tp[4] + ctrl_part(C.part, 30, pt.p[0x23]) - 64, 0, 99);
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
    void fseq_keys() {                           // oneway plays out the rest once every key of the Fseq part is up
        if (fseqPart < 0) { fseqHeld = false; return; }
        for (auto& c : ch) if (c.active && c.part == fseqPart && (c.held || c.sustained)) { fseqHeld = true; return; }
        fseqHeld = false;
    }
    void release(Chan& C) {                      // FUN_000236e8: key off -> EG release, PEG stage 4 toward L4 at T4
        for (auto& s : C.op) { s.eg.release(); s.ueg.release(); }
        C.feg.release();
        C.pegStage = 4; C.pegTarget = C.pegLvl[4]; C.pegRate = C.pegRt[4];
        fseq_keys();
    }
    void set_sustain(int part, bool on) { Part& pt = perf.part[part]; pt.sustain = on; if (!on) for (auto& c : ch) if (c.active && c.part == part && c.sustained) { c.sustained = false; release(c); } }
    void all_off() { for (auto& c : ch) c.active = false; for (auto& p : perf.part) { p.nheld = 0; } fseqRun = false; fseqHeld = false; }
    void all_release() { for (auto& c : ch) if (c.active && (c.held || c.sustained)) { c.held = c.sustained = false; release(c); } for (auto& p : perf.part) p.nheld = 0; }

    // ---------------------------------------------------------------- Fseq (FUN_0000fffa / FUN_0001a59e)
    void fseq_loop(int& lo, int& hi, int& dir) const {     // loop points, and the direction they imply
        int ls = perf.c[0x1C] << 7 | perf.c[0x1D], le = perf.c[0x1E] << 7 | perf.c[0x1F];
        if (ls == le) { ls = fseq.loopStart; le = fseq.loopEnd; }
        dir = le >= ls ? 1 : -1;
        lo = clampi(std::min(ls, le), 0, fseq.endStep); hi = clampi(std::max(ls, le), 0, fseq.endStep);
    }
    void fseq_start(int vel) {
        int ratio = perf.c[0x18] << 7 | perf.c[0x19];
        fseqClock = ratio < 100;                           // below 10.0 % the word is a MIDI clock division 0..4
        if (!fseqClock) {
            int sens = perf.c[0x22] & 7;
            ratio = clampi(ratio, 100, 5000);
            if (sens && ratio > 100) { int x = ((ratio - 100) * sens * (127 - vel)) / 7 / 127; ratio = clampi(ratio - x, 100, 5000); }
            ratio = clampi(ratio + ((ctrl_offset(fseqPart, 46) * 0x1324) >> 7), 100, 5000);   // ctrlDest_46
            double counts = (double)VELW[clampi(fseq.speedAdj, 0, 127)] * 84.0 * 1000.0 / ratio + 2884.0;
            fseqPeriod = counts * 32.0 / CPU_HZ;           // CMT1 at clock/32
        }
        int lo, hi, dir; fseq_loop(lo, hi, dir);
        int off = clampi(perf.c[0x1A] << 7 | perf.c[0x1B], 0, fseq.endStep);
        fseqDir = dir;
        fseqStep = dir > 0 ? off : clampi(fseq.endStep - off, 0, fseq.endStep);
        fseqAcc = 0; fseqClockAcc = 0; fseqRun = true; fseqHeld = true; fseqVel = vel;
        fseqDelay = clampi(perf.c[0x26], 0, 99) / 99.0 * cal::FSEQ_DELAY_S;
    }
    // One frame forward. oneway loops the section between the loop points while a key is held and then
    // plays out the rest; round ping-pongs between them (owner's manual page 34).
    void fseq_step() {
        int lo, hi, dir; fseq_loop(lo, hi, dir); (void)dir;
        fseqStep += fseqDir;
        if ((perf.c[0x20] & 1) != 0) {
            if (fseqStep >= hi) { fseqStep = hi; fseqDir = -1; }
            else if (fseqStep <= lo) { fseqStep = lo; fseqDir = 1; }
        } else if (fseqHeld) {
            if (fseqDir > 0 && fseqStep > hi) fseqStep = lo;
            else if (fseqDir < 0 && fseqStep < lo) fseqStep = hi;
        } else {
            if (fseqStep >= fseq.endStep) { fseqStep = fseq.endStep; fseqRun = false; }
            else if (fseqStep <= 0) { fseqStep = 0; fseqRun = false; }
        }
    }
    void fseq_tick(double dt) {
        if (!fseqRun || !fseq.valid) return;
        if (fseqDelay > 0) { fseqDelay -= dt; if (fseqDelay > 0) return; }
        if ((perf.c[0x21] & 3) == 1) {                     // scratch: the controller is the transport
            int v = clampi(ctrl_offset(fseqPart, 47), -128, 127);       // ctrlDest_47, a signed byte
            fseqStep = clampi(((v + 128) * fseq.endStep) / 255, 0, fseq.endStep);
            return;
        }
        if (fseqClock) return;                             // MIDI clock drives it from midi_clock()
        fseqAcc += dt; if (fseqAcc < fseqPeriod) return;
        fseqAcc -= fseqPeriod;
        fseq_step();
    }
    // 24 ppqn in; the speed word 0..4 selects 1/4, 1/2, 1/1, 2/1, 4/1 frames per clock (INFERRED rate).
    void midi_clock() {
        if (!fseqRun || !fseqClock || !fseq.valid || fseqDelay > 0 || (perf.c[0x21] & 3) == 1) return;
        static const double rate[5] = {0.25, 0.5, 1.0, 2.0, 4.0};
        fseqClockAcc += rate[clampi(perf.c[0x18] << 7 | perf.c[0x19], 0, 4)];
        while (fseqClockAcc >= 1.0) { fseqClockAcc -= 1.0; fseq_step(); }
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
    // LFO2: the same stepper as LFO1, but the speed byte is 0..127 straight from the voice and the
    // output only reaches the filter cutoff.
    void lfo2_tick(Chan& C, const Part& pt) {
        const Voice& V = pt.voice;
        int sp = clampi(V.lfo2speed + pt.p[0x2E] - 64 + clampi(ctrl_offset(C.part, 45) * 2, -255, 255), 0, 255);
        uint32_t inc = sp == 0 ? 0xB : sp * (sp < 0xA0 ? 0xB : 0xB + ((sp - 0xA0) >> 2)); inc = (inc & 0xFFFF) << 1;
        uint32_t ph = C.lfo2Phase + inc; bool wrapped = ph > 0xFFFF; C.lfo2Phase = ph & 0xFFFF;
        int hi = C.lfo2Phase >> 8;
        switch (V.lfo2wave) {
        case 1: C.lfo2Val = (int8_t)(~hi); break;
        case 2: C.lfo2Val = (int8_t)hi; break;
        case 3: C.lfo2Val = (hi & 0x80) ? -128 : 127; break;
        case 4: { int idx = hi & 0x3F; if (hi & 0x40) idx ^= 0x3F; int v = SINE64[idx]; C.lfo2Val = (hi & 0x80) ? (int8_t)(~v) : v; break; }
        case 5: if (wrapped) C.lfo2SH = SHTAB[((C.lfo2SH & 0xFF) * 0xB3 + (rand() & 0xFF)) & 0x7FF]; C.lfo2Val = C.lfo2SH; break;
        default: { uint32_t t = (C.lfo2Phase << 1); if (t > 0xFFFF) t = ~t & 0xFFFF; C.lfo2Val = (int)((t >> 8) & 0xFF) - 0x80; }
        }
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
        voice_ctrl(C, pt); refresh_bias(C, pt);
        int fseqPitch = 0;
        if (fseq_on(part) && !(perf.c[0x23] & 1)) {
            const uint8_t* f = fseq.frame[fseqStep];
            fseqPitch = (f[0] * 256 + f[1] * 2) - (NOTETAB[fseq.noteAssign & 0x7F] + 0x1243) + (fseq.tuning - 63);
        }
        C.regPitch = C.portaCur + (C.pegCur >> 2) + bend_word(pt) + fseqPitch;
        C.regC0 = (C.pitchNote >> 8) + 10;
        int fade = C.lfoFade >> 8;
        int pmd = clampi(eb86(clampi(V.pmd + pt.p[0x16] - 64, 0, 99)) + ctrl_offset(part, 39) * 2, 0, 255);   // ctrlDest_39 doubles it
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
        // No formant tracking term: FUN_000127FC applies the whole key-to-Fseq difference to the channel
        // pitch (the fseqPitch above, whose constants it confirms exactly) and nothing shifts the frame's
        // formant frequencies. Formants staying put while the fundamental moves is the point of the format.
        for (int o = 0; o < 8; o++) {
            int l = C.levelOff[o], ul = C.ulevelOff[o];
            if (fseq_on(part)) {
                const uint8_t* f = fseq.frame[fseqStep]; int t = V.v[o].fseqtrk;
                if (C.fseqOp[o]) { l = f[0x12 + t] << 1; if (lvVel != 0x80) l = (~(((~l) & 0xFF) * lvVel >> 8)) & 0xFF; C.fqWord[o] = f[2 + t] * 256 + f[0xA + t] * 2 + C.fbW[o]; }
                if (C.fseqUOp[o]) { ul = f[0x2A + t] << 1; if (lvVel != 0x80) ul = (~(((~ul) & 0xFF) * lvVel >> 8)) & 0xFF; C.fquWord[o] = f[0x1A + t] * 256 + f[0x22 + t] * 2 + C.ufbW[o]; }
            }
            C.regLevel[o] = clampi(l + C.egbias[o] + C.vcLvl[o][0], 0, 255);
            C.regULevel[o] = clampi(ul + C.uegbias[o] + C.vcLvl[o][1], 0, 255);
        }
        int vAtt, uAtt; part_levels(part, vAtt, uAtt);
        C.partV = db2lin(-LEVEL_DB * vAtt); C.partU = db2lin(-LEVEL_DB * uAtt);
        refresh_pan(C, pt); refresh_filter(C, pt);
    }
    // Voice common 0x40-0x53: five Formant control destinations and five FM control destinations, each
    // [--ddvooo] where dd = off/out/freq/width, v picks the voiced or unvoiced operator and ooo the
    // operator. The paired depth byte scales the part's FORMANT (0x1D) or FM (0x1E) value.
    // INFERRED scaling: a full-depth knob moves the centre frequency about six semitones.
    void voice_ctrl(Chan& C, const Part& pt) {
        memset(C.vcLvl, 0, sizeof C.vcLvl); memset(C.vcFreq, 0, sizeof C.vcFreq); memset(C.vcBw, 0, sizeof C.vcBw);
        const uint8_t* b = pt.voice.raw;
        int src[2] = {ctrl_part(C.part, 32, pt.p[0x1D]) - 64, ctrl_part(C.part, 33, pt.p[0x1E]) - 64};
        for (int k = 0; k < 10; k++) {
            int d = b[k < 5 ? 0x40 + k : 0x4A + (k - 5)];
            int dep = (int)b[k < 5 ? 0x45 + k : 0x4F + (k - 5)] - 64;
            int dd = (d >> 4) & 3, v = (d >> 3) & 1, o = d & 7;
            if (!dd || !dep) continue;
            int amt = (src[k < 5 ? 0 : 1] * dep) >> 6;
            if (dd == 1) C.vcLvl[o][v] -= amt;            // "out": more control means less attenuation
            else if (dd == 2) C.vcFreq[o][v] += (int)(amt * cal::VCTRL_FREQ);
            else C.vcBw[o][v] += amt / 2;
        }
    }
    // registers 0x22A-0x22F: the pan index (part pan, pan scaling, pan LFO, performance pan, the Panpot
    // controller) read through the firmware's own pan tables as a 0.375 dB attenuation per side.
    // FUN_00025bc8 and FUN_0002c36c keep the pan as 0..255 and index the tables at pan >> 1, and the voice
    // image carries 2 * the part byte at +0x2E, so the table index is the part byte itself, not one below it.
    // The firmware keeps the whole sum in the 0..255 domain and halves it once with pan >> 1 (docs/
    // ymp706_registers.md, Pan). pan_index returns the part-pan-plus-scaling in the index domain, so the two
    // terms that follow — the pan LFO and the performance pan — are each summed at 0..255 and land halved.
    void refresh_pan(Chan& C, const Part& pt) {
        int base = pt.p[0x0E] ? ctrl_part(C.part, 18, pt.p[0x0E]) : C.panBase;             // Panpot edits the part byte
        int idx = pan_index(base, pt.p[0x28], C.noteP);
        idx += ((C.lfoVal * clampi(pt.p[0x29], 0, 99) * (int)(C.lfoFade >> 8)) >> 16) / 2; // pan LFO depth, faded in, LFO1
        if (perf.c[0x11]) idx += (perf.c[0x11] - 64) / 2;                              // performance pan
        idx = clampi(idx, 0, 127);
        C.panL = db2lin(-LEVEL_DB * PANL[idx]); C.panR = db2lin(-LEVEL_DB * PANR[idx]);
    }
    void refresh_filter(Chan& C, const Part& pt) {
        if (!C.fltOn) return;
        const Voice& V = pt.voice; int part = C.part;
        double cut = V.fltCut + (ctrl_part(part, 21, pt.p[0x18]) - 64);
        cut += (V.fltKsDepth * (C.noteP - clampi(V.fltKsPoint, 0, 127))) / 64.0;       // cutoff key scaling
        int egd = V.fegDepth + (ctrl_part(part, 23, pt.p[0x1F]) - 64) + ((V.fltEgVel * (C.vel - 64)) >> 4);
        cut += C.feg.cur * egd / 50.0;                                                 // filter EG, levels 0..100 around 50
        int fade = C.lfoFade >> 8;
        // Destinations 42 and 44 add to the LFO depths, not to the cutoff: FUN_0000DB3C takes the
        // controller's word, scales it by 99/127 and adds it to the voice's own depth before clamping.
        int lfo1d = clampi(V.fltLfo1 + ctrl_offset(part, 42) * 99 / 127, 0, 99);
        int lfo2d = clampi(V.fltLfo2 + pt.p[0x2F] - 64 + ctrl_offset(part, 44) * 99 / 127, 0, 99);
        cut += C.lfoVal * eb86(lfo1d) * fade / 4194304.0 * 64.0;                       // LFO1 filter mod
        cut += C.lfo2Val * eb86(lfo2d) / 16384.0 * 64.0;                               // LFO2 filter mod
        // FUN_0000DB3C: the part's resonance offset counts double, and the sum is clamped to the raw
        // 0..116 the chip takes (reso_q clamps it there).
        int reso = V.fltReso + (ctrl_part(part, 22, pt.p[0x19]) - 64) * 2 + ((V.fltResoVel * (C.vel - 64)) >> 4);
        C.flt.setup(C.fltType, cut_hz(cut), reso);
    }
    void tick() {
        double dt = 1.0 / TICK_HZ;
        fseq_tick(dt);
        for (auto& C : ch) {
            if (!C.active) continue;
            const Part& pt = perf.part[C.part];
            lfo_tick(C, pt); lfo2_tick(C, pt); peg_tick(C); porta_tick(C); C.feg.tick(); refresh_regs(C, pt);
        }
    }

    // ---------------------------------------------------------------- MIDI in (Data List sections 2 to 4)
    // One entry point for channel messages so the plugin layer can feed the engine the same way the
    // console does. Sysex goes through midi_sysex(); replies land in outQ.
    int forceChannel = -1;                       // console -c: every part listens on this channel
    double senseTimer = 0;                       // active sensing: mute if 0xFE stops arriving
    int bankMsb = 0x3F, perfBank = -1;
    // FUN_000191C0 fills the source table from the system control numbers, in this order. The four knob
    // slots are disabled when the system's knob receive switch is off.
    enum { SRC_KN1, SRC_KN2, SRC_KN3, SRC_KN4, SRC_MC1, SRC_MC2, SRC_PB, SRC_CAT,
           SRC_PAT, SRC_FC, SRC_BC, SRC_MC3, SRC_MW, SRC_MC4 };
    static const int SRC_RAW = 0x17C0;           // PB, CAT, PAT, FC, BC, MW keep the raw 0..127 value
    int ccSource(int cc) const {
        static const int sysOf[14] = {0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, -1, -1, -1, 0x1E, 0x1F, 0x1C, -2, 0x1D};
        for (int i = 0; i < 14; i++) {
            if (sysOf[i] == -2) { if (cc == 1) return i; continue; }
            if (sysOf[i] < 0) continue;
            if (i < 4 && !sys[0x14]) continue;   // knob receive switch off
            if (sys[sysOf[i]] == cc) return i;
        }
        return -1;
    }
    // FUN_00014AD0: (v - 64) * 2 for the knobs and MIDI controls, raised so full scale reaches 127;
    // the physical controllers are stored as they come.
    void set_source(Part& pt, int slot, int v) {
        pt.src[slot] = (SRC_RAW & (1 << slot)) ? v : ((v - 64) * 2 == 126 ? 127 : (v - 64) * 2);
    }
    bool part_listens(int p, int chn) const {
        int rc = perf.part[p].rcv();
        if (rc == 0x7F) return false;
        if (forceChannel >= 0) return chn == forceChannel;
        int pc = perfChannel();
        if (rc == 0x10) return pc == 0x10 || (pc != 0x7F && chn == pc);
        return rc == chn;
    }
    void load_perf_bank(int lsb, int prog);      // defined with the ROM loaders
    void midi_in(int st, int d1, int d2) {
        int chn = st & 0x0F; st &= 0xF0;
        if (st == 0xF0) {                        // system real time
            if (chn == 0x08) midi_clock();
            else if (chn == 0x0E) senseTimer = 0.5;
            else if (chn == 0x0A || chn == 0x0B) { fseqAcc = 0; fseqClockAcc = 0; }
            return;
        }
        int pc = perfChannel();
        bool onPerfCh = pc == 0x10 || (pc != 0x7F && chn == pc);
        if (st == 0xC0 && onPerfCh && sys[0x13] && perfBank >= 0x40) { load_perf_bank(perfBank, d1); return; }
        for (int p = 0; p < 4; p++) {
            if (!part_listens(p, chn)) continue;
            Part& pt = perf.part[p];
            switch (st) {
            case 0x90: if (d2) { note_on(p, d1, d2); fseq_keys(); break; }   // velocity 0 falls through
            case 0x80: note_off(p, d1); fseq_keys(); break;
            case 0xA0: pt.src[SRC_PAT] = d2; break;
            case 0xD0: pt.src[SRC_CAT] = d1; break;
            case 0xE0: pt.bend = (d2 - 64) * 16; pt.src[SRC_PB] = d2 - 64; break;
            case 0xC0: if (rom_ready(rom) && sys[0x13] && pt.p[1] && sys[8]) { pt.p[2] = (uint8_t)d1; rom_voice(*rom, bank_voice_index(pt.p[1], d1), pt.voice); } break;
            case 0xB0: control_change(p, d1, d2); break;
            }
        }
    }
    void control_change(int p, int cc, int v) {
        Part& pt = perf.part[p];
        int src = ccSource(cc);
        if (src >= 0) { set_source(pt, src, v); return; }
        if (cc == sys[0x20]) { pt.p[0x1D] = (uint8_t)v; return; }    // Formant knob control number
        if (cc == sys[0x21]) { pt.p[0x1E] = (uint8_t)v; return; }    // FM knob control number
        switch (cc) {
        case 0: if (sys[0x12]) bankMsb = v; break;
        case 32: if (sys[0x12] && bankMsb == 0x3F) { if (v <= 0x0B) pt.p[1] = (uint8_t)(v + 1); else if (v >= 0x40 && v <= 0x43) perfBank = v; } break;
        case 6: data_entry(p, v); break;
        case 7: pt.p[0x0B] = (uint8_t)v; break;
        case 11: pt.expr = std::max((int)pt.p[0x2C], std::min(254, v * 2)); break;
        case 64: set_sustain(p, v >= 64); break;
        case 65: pt.p[0x24] = (uint8_t)((pt.p[0x24] & 2) | (v >= 64)); break;
        case 5: pt.p[0x25] = (uint8_t)v; break;
        case 10: pt.p[0x0E] = (uint8_t)std::max(1, v); break;
        // The five sound controllers and the two sends, read out of the firmware's own per-CC handler
        // table at flash 0x3DA04: every entry that is not the ignore stub at 0x151C0 writes one part
        // parameter, and the handler's immediate is the address low it writes to. 71 -> 0x19, 72 ->
        // 0x1C, 73 -> 0x1A, 74 -> 0x18, 91 -> 0x13, 93 -> 0x12. The MIDI View screens in the EPROM
        // print the same pairs: RevSend "Bn 5B", VarSend "Bn 5D", Filter "Bn 4A".
        case 71: pt.p[0x19] = (uint8_t)v; break;    // filter resonance
        case 72: pt.p[0x1C] = (uint8_t)v; break;    // EG release time
        case 73: pt.p[0x1A] = (uint8_t)v; break;    // EG attack time
        case 74: pt.p[0x18] = (uint8_t)v; break;    // filter cutoff
        case 91: pt.p[0x13] = (uint8_t)v; break;    // reverb send
        case 93: pt.p[0x12] = (uint8_t)v; break;    // variation send, not 94
        case 98: pt.rpnL = v; pt.nrpnSel = true; break;
        case 99: pt.rpnM = v; pt.nrpnSel = true; break;
        case 100: pt.rpnL = v; pt.nrpnSel = false; break;
        case 101: pt.rpnM = v; pt.nrpnSel = false; break;
        case 120: all_off(); break;
        case 121: pt.bend = 0; pt.expr = 254; memset(pt.src, 0, sizeof pt.src); pt.sustain = false; pt.rpnM = pt.rpnL = 127; break;
        case 123: all_release(); break;
        case 126: pt.p[5] = 0; break;
        case 127: pt.p[5] = 1; break;
        default: break;
        }
    }
    void data_entry(int p, int v) {
        Part& pt = perf.part[p];
        if (pt.nrpnSel) {
            if (pt.rpnM != 1) return;
            switch (pt.rpnL) {                   // NRPN 01 xx, Data List section 2.2.2
            case 0x08: pt.p[0x15] = (uint8_t)v; break;   // LFO1 speed
            case 0x09: pt.p[0x16] = (uint8_t)v; break;   // LFO1 pitch mod
            case 0x0A: pt.p[0x17] = (uint8_t)v; break;   // LFO1 delay
            case 0x0B: pt.p[0x2E] = (uint8_t)v; break;   // LFO2 speed
            case 0x0C: pt.p[0x2F] = (uint8_t)v; break;   // LFO2 filter mod
            case 0x20: pt.p[0x18] = (uint8_t)v; break;   // filter cutoff
            case 0x21: pt.p[0x19] = (uint8_t)v; break;   // filter resonance
            case 0x63: pt.p[0x1A] = (uint8_t)v; break;   // EG attack
            case 0x64: pt.p[0x1B] = (uint8_t)v; break;   // EG decay
            case 0x66: pt.p[0x1C] = (uint8_t)v; break;   // EG release
            }
            return;
        }
        if (pt.rpnM != 0) return;
        switch (pt.rpnL) {                       // RPN 00 00-02
        case 0: { int r = clampi(v, 0, 24); pt.p[0x26] = (uint8_t)(0x40 + r); pt.p[0x27] = (uint8_t)(0x40 - r); break; }
        case 1: pt.p[9] = (uint8_t)clampi(v, 0x0F, 0x70); break;
        case 2: pt.p[8] = (uint8_t)clampi(v - 0x28, 0, 0x30); break;
        }
    }
    void sense_tick(double dt) { if (senseTimer > 0 && (senseTimer -= dt) <= 0) { senseTimer = 0; all_off(); } }

    // ---------------------------------------------------------------- sysex out (dump and parameter replies)
    void push_sysex(std::vector<uint8_t>& m) { if (outQ.size() < 64) outQ.push_back(m); }
    void push_bulk(int ah, int am, int al, const uint8_t* d, int n) {
        std::vector<uint8_t> m{0xF0, 0x43, (uint8_t)(devNumber() & 0x0F), 0x5E,
                               (uint8_t)(n >> 7 & 0x7F), (uint8_t)(n & 0x7F),
                               (uint8_t)ah, (uint8_t)am, (uint8_t)al};
        int sum = (n >> 7 & 0x7F) + (n & 0x7F) + ah + am + al;
        for (int i = 0; i < n; i++) { uint8_t b = d[i] & 0x7F; m.push_back(b); sum += b; }
        m.push_back((uint8_t)((-sum) & 0x7F)); m.push_back(0xF7);
        push_sysex(m);
    }
    void push_param(int ah, int am, int al, int val) {
        std::vector<uint8_t> m{0xF0, 0x43, (uint8_t)(0x10 | (devNumber() & 0x0F)), 0x5E,
                               (uint8_t)ah, (uint8_t)am, (uint8_t)al,
                               (uint8_t)(val >> 7 & 0x7F), (uint8_t)(val & 0x7F), 0xF7};
        push_sysex(m);
    }
    int read_param(int ah, int am, int al) const {
        if (ah == 0x00 && am == 0 && al < 76) return sys[al];
        if (ah == 0x10 && am == 0 && al < 80) return perf.c[al];
        if (ah == 0x10 && ((am == 0 && al >= 0x50) || am == 1)) { int i = am ? 48 + al : al - 0x50; return i < 112 ? perf.fx[i] : -1; }
        if (ah >= 0x30 && ah <= 0x33 && am == 0 && al < 52) return perf.part[ah - 0x30].p[al];
        if (ah >= 0x40 && ah <= 0x43 && am == 0 && al < 112) return perf.part[ah - 0x40].voice.raw[al];
        if (ah >= 0x60 && ah <= 0x63 && am < 8 && al < 62) return perf.part[ah - 0x60].voice.raw[112 + am * 62 + al];
        return -1;
    }
    void current_perf_bytes(uint8_t* out) const {          // 400 bytes, sysex layout
        memcpy(out, perf.c, 80); memcpy(out + 80, perf.fx, 112);
        for (int i = 0; i < 4; i++) memcpy(out + 192 + 52 * i, perf.part[i].p, 52);
    }
    void fseq_bytes(std::vector<uint8_t>& d) const {
        d.assign(32 + (size_t)fseq.nframes * 50, 0);
        memcpy(d.data(), fseq.name, 8);
        d[0x10] = (uint8_t)(fseq.loopStart >> 7); d[0x11] = (uint8_t)(fseq.loopStart & 0x7F);
        d[0x12] = (uint8_t)(fseq.loopEnd >> 7); d[0x13] = (uint8_t)(fseq.loopEnd & 0x7F);
        d[0x14] = (uint8_t)fseq.loopMode; d[0x15] = (uint8_t)fseq.speedAdj; d[0x16] = (uint8_t)fseq.velTempo;
        d[0x17] = (uint8_t)fseq.pitchMode; d[0x18] = (uint8_t)fseq.noteAssign; d[0x19] = (uint8_t)fseq.tuning;
        d[0x1A] = (uint8_t)fseq.delay; d[0x1B] = (uint8_t)clampi(fseq.nframes / 128 - 1, 0, 3);
        d[0x1E] = (uint8_t)(fseq.endStep >> 7); d[0x1F] = (uint8_t)(fseq.endStep & 0x7F);
        memcpy(d.data() + 32, fseq.frame, (size_t)fseq.nframes * 50);
    }
    void dump_request(int ah, int am, int al) {
        if (ah == 0x00) { push_bulk(0x00, 0, 0, sys, 76); return; }
        if (ah == 0x10 || ah == 0x11) { uint8_t d[400]; current_perf_bytes(d); push_bulk(ah, am, al, d, 400); return; }
        if (ah >= 0x40 && ah <= 0x43) { push_bulk(ah, 0, 0, perf.part[ah - 0x40].voice.raw, 608); return; }
        if (ah == 0x51) { push_bulk(0x51, am, al, perf.part[0].voice.raw, 608); return; }
        if ((ah & 0xF0) == 0x60 && (ah & 0x0F) <= 1 && fseq.valid) {   // 6b 00 nn, b = bank
            std::vector<uint8_t> d; fseq_bytes(d);
            push_bulk(ah, am, al, d.data(), (int)d.size());
        }
    }

    // ---------------------------------------------------------------- chip model (INFERRED beyond the register values)
    inline double op_sample(OpState& s, const OpV& v, double f0, double fop, int ratio, double pm) {
        if (v.form == 0) { s.phase += fop / SR; if (s.phase >= 1) s.phase -= 1; return fsin(s.phase + pm); }
        double fw, fc, wl;
        if (v.form == 7) { fw = f0; fc = fop; wl = s.wl7; }                                                          // formant: window at the fundamental (INFERRED bw curve)
        // all1/all2/odd1/odd2 are a group that starts at the operator's own frequency, so they are the same
        // windowed carrier the formant is, with the carrier at fop. MEASURED: the unit gives one partial on
        // all1 and all2 and partials 1 and 3 on odd1 and odd2, where the fixed quarter-period window at DC
        // this replaces gave a full harmonic series. Retriggering at twice fop is what leaves the odd
        // partials: a grain train at that spacing under a carrier at fop puts its lines at fop, 3 fop,
        // 5 fop and nowhere else.
        //
        // The window is FIXED, and the bandwidth has nothing to do with it. MEASURED on 2026-09-19 by
        // sweeping register 0x218 through all hundred values on each form in turn: the formant's window
        // opens and every one of the other six is flat to the byte, width and peak both. Voice byte 6 does
        // reach those forms, but as register 0x230, which the Data List calls the "freq. ratio of band
        // spectrum" and which the voice image confirms carries the raw byte for them and the formant
        // transpose word for the formant. What it does there is unmeasured: the sweep meant to settle it
        // wrote 0x218. INFERRED, FS1R.unlock/docs/unknowns.md experiment 9.
        // res1 and res2 put a three-partial group on harmonic ratio + 1 and are otherwise identical.
        // MEASURED on 2026-09-19 by sweeping register 0x230 through all hundred values on each: the peak
        // lands on partial ratio + 1 at every one of the hundred, on both forms, with the two partials
        // either side 6 dB down and everything else at the noise floor, and the peak level flat across
        // the whole range. The 1 + ratio * 31 / 99 this replaces topped out at 32 times the fundamental
        // where the unit reaches 100. docs/formant.md.
        // The window is one grain period for the odd and resonant forms and two for all1 and all2.
        // MEASURED off the sideband ratios, which a sin^2 window fixes exactly: a grain exactly one
        // period long makes the two partials either side of the peak 6.02 dB down and kills everything
        // beyond them, which is what the unit gives res1 and res2 at every one of a hundred settings.
        // all1 and all2 are a single partial with nothing within 88 dB, so their window is the longer
        // one the engine has slots for.
        else if (v.form == 5 || v.form == 6) { fw = fop; fc = fop * (ratio + 1); wl = 1.0; }
        else { fw = (v.form >= 3 ? 2 * fop : fop); fc = fop; wl = v.form < 3 ? 2.0 : 1.0; }
        wl = std::min(wl, 2.0);
        s.fphase += fw / SR;
        if (s.fphase >= 1) {
            s.fphase -= 1; WinGen& g = s.g[s.nextGen]; g.on = true; g.w = 0;
            // The formant restarts its carrier every grain, which is what holds its peak still while the
            // fundamental moves. The odd forms retrigger twice a period, so restarting there would put
            // half a cycle of alternation into the grain train and land the group on the even partials;
            // half a cycle back is the same carrier running on.
            g.c = (v.form == 3 || v.form == 4) && (s.halfCount & 1) ? 0.5 : 0.0;
            s.nextGen ^= 1; s.halfCount++;
        }
        double winc = fw / (wl * SR), y = 0;
        for (int k = 0; k < 2; k++) {
            WinGen& g = s.g[k]; if (!g.on) continue;
            g.w += winc; if (g.w >= 1) { g.on = false; continue; }
            y += fwin(v.form == 7, v.skirt, g.w) * fsin(g.c + pm); g.c += fc / SR;
        }
        if (v.form == 7) { if (cal::FRMT_NORM != 0.0) y *= pow(1.0 / wl, cal::FRMT_NORM); }
        else y *= cal::FORM_LEVEL * 2.0 / wl;    // every form but sine and frmt; a shorter grain carries less
        return y;
    }
    // Every CTL samples: the per-operator frequency and level maths. Its inputs only move on the 192 Hz
    // register tick and the voice parameters, and doing it per sample (a dozen pow() calls per operator)
    // was the whole CPU bill. The one input that moves every sample, an operator's own pitch EG, stays in
    // the sample loop while it runs: its integral is the phase. ponytail: 16 samples is 0.33 ms, finer
    // than the tick itself.
    static const int CTL = 16;
    void refresh_ctl(Chan& C) {
        const Part& pt = perf.part[C.part]; const Voice& V = pt.voice; const unsigned char* alg = FS1R_ALG[V.alg];
        bool fs = fseq_on(C.part);
        C.f0 = word_hz(C.regPitch + 0x1243 + C.regPM);                 // channel fundamental incl. LFO pitch mod (pms 7)
        C.fbGain = V.fb ? cal::FEEDBACK * pow(2.0, V.fb - 7) : 0.0;    // INFERRED feedback scale
        for (int o = 0; o < 8; o++) {
            OpState& s = C.op[o]; const OpV& v = V.v[o]; const OpU& u = V.u[o];
            s.att = C.regLevel[o] * LEVEL_DB + C.regAM * LEVEL_DB * v.ams / 7.0;   // INFERRED: ams scales the channel AM attenuation linearly
            if (alg[2 * o + 1] & 1) s.att += cal::CARRIER_DB * V.corr[o];         // carrier level correction (bits in the 0x200 word), 1.5 dB steps INFERRED
            int pmw = (int)(C.regPM * cal::PMS_FRAC[v.pms]) + C.vcFreq[o][0];
            double fop;
            if (fs && C.fseqOp[o]) fop = word_hz(C.fqWord[o] + pmw);
            else if (v.form == 7) fop = word_hz(C.freqWord[o] + C.fbW[o] + (C.frmtWord[o] - 0x1243) + pmw + (C.regFM * v.fms) / 7);
            else if (!v.fixed) fop = word_hz(C.freqWord[o] + C.regPitch + pmw);
            else fop = word_hz(C.freqWord[o] + C.fbW[o] + pmw + (C.regFM * v.fms) / 7);
            if (v.form != 7) fop *= pow(2.0, ((v.detune - 15) * cal::DETUNE_CENTS) / 1200.0);       // INFERRED: detune 2 cents per step on non-formant ops
            s.fop = fop;
            s.bw = clampi(C.bwReg[o] + C.vcBw[o][0], 0, 99);                 // register 0x218, the formant's
            s.ratio = v.form == 7 ? 0 : clampi(C.frmtWord[o], 0, 99);         // register 0x230, every other form's
            s.wl7 = std::min(cal::FRMT_WL_MAX, C.f0 / (cal::FRMT_BW_HZ0 * pow(2.0, s.bw / cal::FRMT_BW_DB)));
            // unvoiced (noise formant) operator
            s.uatt = C.regULevel[o] * LEVEL_DB + C.regAM * LEVEL_DB * u.ams / 7.0;
            double nf;
            if (fs && C.fseqUOp[o]) nf = word_hz(C.fquWord[o]);
            else if (u.mode == 2 && v.form == 7) nf = word_hz(C.freqWord[o] + (C.frmtWord[o] - 0x1243));
            // MEASURED: link-ff on a voiced partner that is not a formant operator reaches register 0x300 as
            // link-fo, not as itself. uv-linkff-sine's image reads mode 1 where the sysex asked for 2.
            else if (u.mode) nf = C.f0;
            else nf = word_hz(C.ufreqWord[o] + C.ufbW[o] + (C.regFM * u.fms) / 7 + C.vcFreq[o][1]);
            s.nf = nf * pow(2.0, u.transpose / 12.0);
            double fcut = cal::NOISE_BASE_HZ * pow(2.0, clampi(C.ubwReg[o] + C.vcBw[o][1], 0, 127) / 127.0 * cal::NOISE_OCT);  // INFERRED noise formant model, see docs
            s.na = 1.0 - exp(-2 * PI * fcut / SR);
            s.nscale = sqrt(1.0 + u.skirt) * 0.5 * sqrt(2.0 / cal::NOISE_BW_REF)
                     * pow(cal::NOISE_BW_REF / s.na, cal::NOISE_BW_POW);
        }
    }
    inline void render_chan(Chan& C, double& outL, double& outR) {
        if (C.ctlLeft-- <= 0) { C.ctlLeft = CTL - 1; refresh_ctl(C); }
        const Voice& V = perf.part[C.part].voice; const unsigned char* alg = FS1R_ALG[V.alg];
        double partV = C.partV, partU = C.partU;
        double Cb = 0, H = 0, S = 0, fbNew = 0, mix = 0;
        for (int o = 0; o < 8; o++) {
            OpState& s = C.op[o]; const OpV& v = V.v[o];
            unsigned char t0 = alg[2 * o], t1 = alg[2 * o + 1]; int F = (t0 >> 3) & 7;
            double in = F == 3 ? C.fbBus * C.fbGain : F == 4 ? Cb : F == 5 ? H : F == 6 ? S : 0.0;
            if (F == 6) S = 0;
            double egdb = s.eg.tick();
            double y = 0;
            if (egdb - s.att > -100) {
                double fop = s.fop;
                if (s.feg.stage < 2) fop *= pow(2.0, s.feg.tick() / 12.0);
                y = op_sample(s, v, C.f0, fop, s.ratio, in * FM_INDEX) * db2lin_fast(egdb - s.att);
            }
            Cb = y; if (t0 & 2) H = y; if (t1 & 4) S += y; if (t0 & 4) fbNew = y;
            if (t1 & 1) mix += y * partV;
            // unvoiced (noise formant) operator
            const OpU& u = V.u[o];
            double uegdb = s.ueg.tick();
            if (uegdb - s.uatt > -100) {
                double nf = s.nf;
                if (s.ufeg.stage < 2) nf *= pow(2.0, s.ufeg.tick() / 12.0);
                s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
                double nz = ((int32_t)s.rng) * (1.0 / 2147483648.0);
                int stages = 1 + u.skirt;
                for (int k = 0; k < stages; k++) { s.lp[k] += (nz - s.lp[k]) * s.na; nz = s.lp[k]; }
                nz = nz * s.nscale + u.res / 7.0;
                s.nphase += nf / SR; if (s.nphase >= 1) s.nphase -= 1;
                mix += nz * fsin(s.nphase) * db2lin_fast(uegdb - s.uatt) * partU;
            }
        }
        C.fbBus = (fbNew + C.fbPrev) * 0.5; C.fbPrev = fbNew;
        bool alive = false;
        for (auto& s : C.op) if (!s.eg.done() || !s.ueg.done()) { alive = true; break; }
        if (!alive) C.active = false;
        // The channel accumulator saturates before the filter loop, which is where the capture puts it:
        // eight carriers stacked in one channel clip flat, while the filter's own output on ingain-12 rides
        // 4 dB above that ceiling, so nothing downstream of CHOUT can be what clips.
        mix = std::clamp(mix, -cal::CHAN_CLIP, cal::CHAN_CLIP);
        if (C.fltOn) mix = C.flt.run(C.fltType, mix * C.fltGain);
        outL = mix * C.panL; outR = mix * C.panR;
    }
    // Part buses -> insertion / variation / reverb -> master EQ, the XG topology the FS1R uses.
    // ponytail: the performance's individual out (common 0x14) is folded into the main pair; the plugin
    // has one stereo output, so a second pair would be four channels nobody listens to yet.
    void render(float* outL, float* outR, int frames) {
        std::lock_guard<std::mutex> lk(mtx);
        fx.configure(perf.fx);
        double insLvl = sendlvl(perf.fx[0x63]), insRev = sendlvl(perf.fx[0x61]), insVar = sendlvl(perf.fx[0x62]);
        double varRev = sendlvl(perf.fx[0x5E]), varRet = sendlvl(perf.fx[0x5D]), revRet = sendlvl(perf.fx[0x5A]);
        int vp = clampi(perf.fx[0x5C] - 1, 0, 126), rp = clampi(perf.fx[0x59] - 1, 0, 126);
        double vpl = db2lin(-LEVEL_DB * PANL[vp]), vpr = db2lin(-LEVEL_DB * PANR[vp]);
        double rpl = db2lin(-LEVEL_DB * PANL[rp]), rpr = db2lin(-LEVEL_DB * PANR[rp]);
        double pvol = perf.c[0x10] / 127.0;
        double dry[4], varS[4], revS[4]; bool insSw[4];
        for (int p = 0; p < 4; p++) {
            const uint8_t* q = perf.part[p].p;
            insSw[p] = (q[0x14] & 1) != 0;
            dry[p] = sendlvl(q[0x11]);
            varS[p] = sendlvl(ctrl_part(p, 20, q[0x12])); revS[p] = sendlvl(ctrl_part(p, 19, q[0x13]));
        }
        sense_tick(frames / (double)SR);
        for (int i = 0; i < frames; i++) {
            tickAcc += TICK_HZ / SR; while (tickAcc >= 1) { tickAcc -= 1; tick(); }
            double pl[4] = {}, pr[4] = {};
            for (auto& c : ch) if (c.active) { double a, b; render_chan(c, a, b); pl[c.part] += a; pr[c.part] += b; }
            double dL = 0, dR = 0, iL = 0, iR = 0, vL = 0, vR = 0, rL = 0, rR = 0;
            for (int p = 0; p < 4; p++) {
                if (insSw[p]) { iL += pl[p]; iR += pr[p]; continue; }
                dL += pl[p] * dry[p]; dR += pr[p] * dry[p];
                vL += pl[p] * varS[p]; vR += pr[p] * varS[p];
                rL += pl[p] * revS[p]; rR += pr[p] * revS[p];
            }
            // An effect block handed parameters no preset would use can run away. Left alone, one bad
            // block poisons the master EQ and silences everything after it, so a block whose output
            // leaves the sane range is emptied and muted for that sample instead.
            auto sane = [](FxBlock& blk, double& a, double& b) {
                if (!(a > -1e6 && a < 1e6) || !(b > -1e6 && b < 1e6)) { blk.clearState(); a = b = 0.0; }
            };
            double oL, oR;
            fx.ins.process(iL, iR, oL, oR);
            sane(fx.ins, oL, oR);
            dL += oL * insLvl; dR += oR * insLvl;
            vL += oL * insVar; vR += oR * insVar;
            rL += oL * insRev; rR += oR * insRev;
            fx.var.process(vL, vR, oL, oR);
            sane(fx.var, oL, oR);
            rL += oL * varRev; rR += oR * varRev;
            double l = dL + oL * varRet * vpl, r = dR + oR * varRet * vpr;
            fx.rev.process(rL, rR, oL, oR);
            sane(fx.rev, oL, oR);
            l += oL * revRet * rpl; r += oR * revRet * rpr;
            if (!(l > -1e6 && l < 1e6) || !(r > -1e6 && r < 1e6)) {
                for (auto& q : fx.eq) q.reset();
                l = r = 0.0;
            }
            fx.master(l, r);
            // Down to the scale of the digital output board, which is what the capture measured, and then
            // the main DAC's own ceiling. gain is ours: it stands in for the analogue volume pot, which on
            // the hardware sits after the tap and so cannot be part of the measurement.
            l *= cal::OUT_GAIN * pvol; r *= cal::OUT_GAIN * pvol;
            // An effect block given parameters no preset would use can run away or go non-finite. The
            // engine is a plugin: whatever happens upstream, it must not hand the host a NaN.
            if (!(l > -1e6 && l < 1e6)) l = 0.0;
            if (!(r > -1e6 && r < 1e6)) r = 0.0;
            l = std::clamp(l, -1.0, 1.0) * gain; r = std::clamp(r, -1.0, 1.0) * gain;
            outL[i] = (float)l; outR[i] = (float)r;
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
// voices: 0-255 native at 0x5C280 (PrA, PrB), 256-1407 DX7 format at 0x30091 (PrC..PrK)
static void rom_voice(const Rom& R, int idx, Voice& V) {
    idx = clampi(idx, 0, 1407);
    if (idx < 256) memcpy(V.raw, R.d.data() + 0x5C280 + (size_t)idx * 608, 608);
    else { const uint8_t* r = R.d.data() + 0x30091 + (size_t)(idx - 256) * 155; uint8_t vced[155]; memcpy(vced, r + 10, 145); memcpy(vced + 145, r, 10); convert_dx7(vced, V.raw); }
    decode_voice(V);
}
// Part bank byte: 0 = off, 1 = Int, 2..12 = PrA..PrK. PRESET A and B are the FS1R's own 128 voices
// each, PRESET C through K the nine banks of DX-series voices (owner's manual page 21), which is the
// order the two ROM blocks are in: 256 native then 1152 DX7. The performances prove it - "FundaBass"
// asks for bank 2 program 41 and native voice 41 is "FundaBass" - and across all 384 of them this
// reading puts 439 parts on FS1R voices against 48 the other way round, and matches the performance's
// own category 71% of the time against 10%. Int is the user bank, which ships holding Init voices, so
// nothing is loaded for it.
static int bank_voice_index(int bank, int prog) {
    prog &= 0x7F;
    if (bank == 2 || bank == 3) return (bank - 2) * 128 + prog;        // PrA, PrB: native
    if (bank >= 4 && bank <= 12) return 256 + (bank - 4) * 128 + prog; // PrC..PrK: DX7
    return prog;
}
// performances: one table of 384 400-byte entries at 0xA000, the preset banks A, B and C in order
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
    idx = clampi(idx, 0, 383); size_t off = 0xA000 + (size_t)idx * 400;
    std::lock_guard<std::mutex> lk(S.mtx);
    perf_from_bytes(S, &R, R.d.data() + off);
    return true;
}
static bool rom_ready(const Rom* R) { return R && R->ok; }
void Synth::load_perf_bank(int lsb, int prog) {          // called with mtx already held
    if (!rom_ready(rom)) return;
    perf_from_bytes(*this, rom, rom->d.data() + 0xA000 + (size_t)bank_perf_index(lsb, prog) * 400);
}
static bool rom_fseq(Synth& S, const Rom& R, int n) {   // preset Fseq 1..90
    n = clampi(n, 1, 90); size_t off = n <= 10 ? 0x100A00 + (size_t)(n - 1) * 25632 : 0x83000 + (size_t)(n - 11) * 6432;
    std::lock_guard<std::mutex> lk(S.mtx); S.fseq.from_bytes(R.d.data() + off, R.d.data() + off + 32, n <= 10 ? 512 : 128);
    if (S.fseqPart < 0) S.fseqPart = 0;
    return true;
}
// FS1R and DX7 bulk dumps. The caller holds S.mtx.
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
                int pi = ah <= 0x43 ? ah - 0x40 : part;
                memcpy(S.perf.part[pi].voice.raw, p, 608); decode_voice(S.perf.part[pi].voice); return true;
            }
            if (bc == 400 && (ah == 0x10 || ah == 0x11)) {
                if (found++ != pick) continue;
                perf_from_bytes(S, R, p); return true;
            }
            // "FSeq Bulk does not interpret Byte Count" (Data List 3.2.1), and it cannot: a 512 frame
            // dump is 25632 bytes and the count is 14 bits. The header's own frame count says how long
            // the dump is, which is also how the length is known when stepping over one.
            if (ah == 0x70 && i + 9 + 32 <= len) {
                int frames = 128 * ((p[0x1B] & 3) + 1);
                size_t n = 32 + (size_t)frames * 50;
                if (i + 9 + n + 2 > len) break;
                if (found++ != pick) { i += 10 + n; continue; }
                S.fseq.from_bytes(p, p + 32, frames); if (S.fseqPart < 0) S.fseqPart = 0; return true;
            }
        } else if (d[i + 3] == 0x05 && d[i + 4] == 0x00 && d[i + 5] == 0x31) {
            if (i + 6 + 49 + 2 > len) break;
            memcpy(g_aced, d + i + 6, 49); g_acedValid = true;       // held for the next VCED
            i += 6 + 49 + 1;
        } else if (d[i + 3] == 0x00 && d[i + 4] == 0x01 && d[i + 5] == 0x1B) {
            if (i + 6 + 155 + 2 > len) break;
            if (found++ != pick) continue;
            convert_dx7(d + i + 6, S.perf.part[part].voice.raw); decode_voice(S.perf.part[part].voice); return true;
        }
    }
    return false;
}
// Every address in the parameter tables reaches the engine as a single parameter change, not just
// through a bulk dump: system (table 4), performance common and the 112 effect bytes (table 1),
// part (table 1), voice common and the 62 bytes per operator (table 2).
// Addresses that hold a 14-bit value across this byte and the next. The Data List's "FS1R receives a
// parameter constructed of 2 bytes (i.e. Fseq Speed Ratio) via an Address High" covers these; every
// other address is a single 7-bit byte and the high half of the value is ignored.
static bool param_is_wide(int ah, int am, int al) {
    if (ah == 0x10 && am == 0) return al == 0x18 || al == 0x1A || al == 0x1C || al == 0x1E ||
                                      (al >= 0x30 && al <= 0x3E && !(al & 1));
    if (ah == 0x10 && am == 0 && al >= 0x50) return !((al - 0x50) & 1) && al <= 0x5F;
    if (ah == 0x10 && am == 1) return al <= 0x27 && !(al & 1);
    if (ah == 0x70 && am == 0) return al == 0x10 || al == 0x12 || al == 0x1E;
    return false;
}
static bool write_param(Synth& S, int ah, int am, int al, int val) {
    uint8_t v = (uint8_t)(val & 0x7F);
    if (ah == 0x00 && am == 0 && al < 76) { if (al != 0x46) S.sys[al] = v; return true; }
    if (ah == 0x10 && am == 0 && al < 80) {
        S.perf.c[al] = v;
        if (al == 0x15 || al == 0x16 || al == 0x17) {
            S.fseqPart = (S.perf.c[0x15] & 7) ? (S.perf.c[0x15] & 7) - 1 : -1;
            if (rom_ready(S.rom) && (S.perf.c[0x16] & 1)) {
                int n = S.perf.c[0x17];
                size_t off = n < 10 ? 0x100A00 + (size_t)n * 25632 : 0x83000 + (size_t)(n - 10) * 6432;
                S.fseq.from_bytes(S.rom->d.data() + off, S.rom->d.data() + off + 32, n < 10 ? 512 : 128);
            }
        }
        return true;
    }
    if (ah == 0x10 && ((am == 0 && al >= 0x50) || am == 1)) { int i = am ? 48 + al : al - 0x50; if (i >= 112) return false; S.perf.fx[i] = v; return true; }
    if (ah >= 0x30 && ah <= 0x33 && am == 0 && al < 52) { S.perf.part[ah - 0x30].p[al] = v; return true; }
    if (ah >= 0x40 && ah <= 0x43 && am == 0 && al < 112) { Voice& V = S.perf.part[ah - 0x40].voice; V.raw[al] = v; decode_voice(V); return true; }
    if (ah >= 0x60 && ah <= 0x63 && am < 8 && al < 62) { Voice& V = S.perf.part[ah - 0x60].voice; V.raw[112 + am * 62 + al] = v; decode_voice(V); return true; }
    if (ah == 0x70 && am == 0 && al < 32) {                       // Fseq header
        uint8_t h[32] = {}; std::vector<uint8_t> cur; S.fseq_bytes(cur);
        if (cur.size() >= 32) memcpy(h, cur.data(), 32);
        h[al] = v;
        if (S.fseq.valid) S.fseq.from_bytes(h, (const uint8_t*)S.fseq.frame, S.fseq.nframes);
        return true;
    }
    return false;
}
// F0 43 1n 5E ah am al vh vl F7 change, F0 43 3n 5E ah am al F7 parameter request,
// F0 43 2n 5E ah am al F7 dump request. The caller holds S.mtx.
static bool apply_param_change_locked(Synth& S, const uint8_t* d, size_t len) {
    if (len < 8 || d[0] != 0xF0 || d[1] != 0x43 || d[3] != 0x5E) return false;
    int kind = d[2] & 0xF0, dev = d[2] & 0x0F;
    if (S.devNumber() != 0x10 && S.devNumber() != dev) return false;
    int ah = d[4], am = d[5], al = d[6];
    if (kind == 0x30) { int v = S.read_param(ah, am, al); if (v >= 0) S.push_param(ah, am, al, v); return v >= 0; }
    if (kind == 0x20) { S.dump_request(ah, am, al); return true; }
    if (kind != 0x10 || len < 10) return false;
    int val = d[7] << 7 | d[8];
    if (param_is_wide(ah, am, al)) return write_param(S, ah, am, al, val >> 7) & write_param(S, ah, am, al + 1, val);
    return write_param(S, ah, am, al, val);
}
static bool apply_param_change(Synth& S, const uint8_t* d, size_t len) {
    std::lock_guard<std::mutex> lk(S.mtx);
    return apply_param_change_locked(S, d, len);
}

// ------------------------------------------------------------------------------------------ self test
// fs1r_emu -selftest: every sysex path the plugin will use. Parameter change in, parameter request out,
// bulk dump out and straight back in, and the whole 400/608 byte state surviving the round trip.
static int g_fails = 0;
static void ck(const char* what, bool ok) { if (!ok) { printf("  FAIL %s\n", what); g_fails++; } }
static int selftest(Synth& S) {
    // 1. a parameter change reaches the engine at every address class
    struct { int ah, am, al, v; const char* name; } pc[] = {
        {0x00, 0, 0x0E, 3, "system velocity curve"},
        {0x10, 0, 0x11, 100, "performance pan"},
        {0x10, 0, 0x58, 5, "reverb type"},
        {0x10, 1, 0x2F, 9, "insertion type"},
        {0x31, 0, 0x0E, 20, "part 2 pan"},
        {0x42, 0, 0x54, 2, "part 3 voice filter type"},
        {0x63, 5, 0x16, 77, "part 4 op 6 level"},
        {0x60, 1, 0x05, 5 << 3 | 1, "part 1 op 2 skirt"},   // what captures/sweep.py skirt sweeps
    };
    for (auto& t : pc) {
        uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)t.ah, (uint8_t)t.am, (uint8_t)t.al, 0, (uint8_t)t.v, 0xF7};
        ck(t.name, apply_param_change_locked(S, m, 10));
        ck(t.name, S.read_param(t.ah, t.am, t.al) == t.v);
    }
    ck("voice decode followed the change", S.perf.part[2].voice.fltType == 2);
    ck("op level decode followed the change", S.perf.part[3].voice.v[5].level == 77);
    // The ladder's zero-delay solve and its (1 + k) normalisation: a DC input has to come back out at
    // unity from every tap, at every feedback the resonance table can reach. The mean over the second
    // half of the run averages the ring away, and a loop that has gone unstable blows this up.
    for (int raw : {0, 32, 64, 116}) {
        for (int tap = 2; tap <= 4; tap++) {
            Ladder L; L.set(1000.0, reso_fb(raw - 16), 0.0);
            double acc = 0; int n = 0;
            for (int i = 0; i < 96000; i++) { double y = L.run(1.0, tap); if (i >= 48000) { acc += y; n++; } }
            ck("ladder unity passband", fabs(acc / n - 1.0) < 0.05);
        }
    }

    // The chip's EG key rate scaling, against the 24 rates 02_envelope_3 measured. Key codes 85, 93
    // and 101 are notes 36, 60 and 84; the rows are the rate offset at time scaling 0 to 7, and the
    // truncation toward zero is what separates the middle row from a flooring divide. docs/aeg.md.
    {
        const int kc[3] = {85, 93, 101};
        const int want[3][8] = {{0, -1, -2, -3, -5, -6, -7, -8},
                                {0, 0, 0, 0, -1, -1, -1, -1},
                                {0, 0, 1, 1, 2, 2, 3, 3}};
        for (int n = 0; n < 3; n++)
            for (int t = 0; t < 8; t++) ck("EG rate scaling", eg_ratescale(t, kc[n]) == want[n][t]);
        // and 10_envelope2's ten key codes, one note per octave from 12 to 120, at time scaling 7 and 3
        const int kc2[10] = {77, 81, 85, 89, 93, 97, 101, 105, 109, 113};
        const int w7[10] = {-11, -11, -8, -4, -1, 1, 3, 7, 10, 11};
        const int w3[10] = {-4, -4, -3, -1, 0, 0, 1, 3, 4, 4};
        for (int n = 0; n < 10; n++) {
            ck("EG rate scaling, tscale 7", eg_ratescale(7, kc2[n]) == w7[n]);
            ck("EG rate scaling, tscale 3", eg_ratescale(3, kc2[n]) == w3[n]);
        }
        ck("EG rate scaling saturates low", eg_ratescale(7, 60) == -11);
        ck("EG rate scaling saturates high", eg_ratescale(7, 127) == 11);
    }
    // The formant window against the bandwidth byte, 04_formant_1's own staircase: flat to 40, opening
    // from 44, halving every eight steps after that.
    {
        // The window is a time, so its length in periods scales with the fundamental and its width in Hz
        // does not. Both are checked here because getting the units wrong is what cost two fitted knees.
        auto hz = [](double bw) { return cal::FRMT_BW_HZ0 * pow(2.0, bw / cal::FRMT_BW_DB); };
        auto wl = [&](double bw, double f0) { return std::min(cal::FRMT_WL_MAX, f0 / hz(bw)); };
        ck("formant bandwidth doubles every eight", fabs(hz(56) / hz(48) - 2.0) < 1e-9);
        ck("formant bandwidth is the same in Hz at any note", fabs(hz(64) - hz(64)) < 1e-12);
        ck("the window is four times as many periods an octave down",
           fabs(wl(72, 65.41) * 4.0 - wl(72, 261.64)) < 1e-6);
        ck("the window clamps at two periods", wl(0, 261.64) == cal::FRMT_WL_MAX);
    }
    // The skirt multiplies the window exponent, it does not step it. A sin^(2n) window one grain period
    // long puts its lines on the binomial row C(2n, n-k), which is what res2 gives the unit: 6.02 dB down
    // either side at skirt 0, 3.52 and 15.56 at skirt 1. Checking the table against those rows pins the
    // law and the doubling at once, and the sine table's own interpolation with them.
    {
        auto line = [&](int s, int k) {          // the k-th Fourier coefficient of g_win[0][s]
            double re = 0;
            for (int i = 0; i < 1024; i++) re += g_win[0][s][i] * cos(2 * PI * k * i / 1024.0);
            return re;
        };
        auto row_db = [&](int s, int k) { return 20 * log10(fabs(line(s, k) / line(s, 0))); };
        ck("skirt 0 is sin^2: one period either side 6.02 dB down", fabs(row_db(0, 1) + 6.0206) < 0.02);
        ck("skirt 0 is sin^2: nothing two periods out", row_db(0, 2) < -60);
        ck("skirt 1 is sin^4, not sin^4 by a different route", fabs(row_db(1, 1) + 3.5218) < 0.02
                                                            && fabs(row_db(1, 2) + 15.563) < 0.05);
        ck("skirt 2 is sin^8", fabs(row_db(2, 1) + 1.9382) < 0.02 && fabs(row_db(2, 3) + 18.837) < 0.1);
        ck("the exponent doubles rather than stepping", fabs(row_db(3, 1) + 1.0216) < 0.02);
        ck("the formant's exponent goes by sqrt(2)",                    // 2 * sqrt(2)^2 = 4 = skirt 1's
           fabs(row_db(0, 1) - 20 * log10(fabs(line(0, 1) / line(0, 0)))) < 1e-12
           && fabs(cal::WIN_SKIRT * cal::WIN_SKIRT_FRMT * cal::WIN_SKIRT_FRMT - 4.0) < 1e-6);
    }
    // Register 0xC0 against the note. docs/ymp706_registers.md used to gloss it as "note/3 + 10", which is
    // out by 63 and is what made the old rate scaling look like dead code when it was merely wrong.
    {
        const int note[5] = {0, 36, 60, 84, 127};
        const int want[5] = {73, 85, 93, 101, 116};
        for (int i = 0; i < 5; i++) ck("key code register", (NOTETAB[note[i]] >> 8) + 10 == want[i]);
    }
    // Pan key scaling at its extreme byte, the ten notes 10_envelope2 measured: hard right at 12, centre
    // at 60, hard left from 108 up.
    {
        const int note[10] = {12, 24, 36, 48, 60, 72, 84, 96, 108, 120};
        const int want[10] = {128, 112, 96, 80, 64, 48, 32, 16, 0, -16};
        for (int i = 0; i < 10; i++) ck("pan key scaling", pan_index(64, 0, note[i]) == want[i]);
        ck("pan key scaling centred", pan_index(64, 50, 12) == 64 && pan_index(64, 50, 120) == 64);
    }
    // A rise from silence starts at the attack floor and gets to its target, and the hold is half a
    // traverse plus the lag. Both are measured; an EG that crawls up from -200 dB is the old bug.
    {
        int L[4] = {0, 0, 0, 63}, R[4] = {32, 0, 0, 0};        // L1 full, L4 silent, attack at rate 32
        EG e; e.start(L, R, 0, 0);
        ck("no hold at register 0x3F", e.holdLeft == 0 && e.stage == 1);
        ck("attack starts at the floor", fabs(e.cur - cal::EG_ATTACK_FLOOR) < 1e-9);
        for (int i = 0; i < (int)(rate_secs(32) * SR); i++) e.tick();
        ck("attack reaches its target", e.cur > -0.5);
        // A rise to a target part way up takes the same route: the chip aims at the top of the scale and
        // stops at the target, so it gets there in a fraction of the time an approach aimed at the target
        // itself would need. Level 70 is 21 dB down; the unit is there inside 80 ms at rate 32.
        int M[4] = {LEVTAB[70] >> 1, 0, 0, 63};
        EG m; m.start(M, R, 0, 0);
        int n = 0;
        while (m.stage == 1 && n < (int)(0.5 * SR)) { m.tick(); n++; }
        ck("a rise to a mid target aims at the top", n > (int)(0.04 * SR) && n < (int)(0.10 * SR));
        int H[4] = {63, 63, 63, 63};
        EG h; h.start(L, H, 40, 0);                             // hold 40 -> register 41
        double want = (rate_secs(41) * cal::EG_HOLD_FRAC + cal::EG_HOLD_LAG) * SR;
        ck("hold is half a traverse plus the lag", fabs(h.holdLeft - want) < 1.0);
    }

    // 2. a parameter request comes back as a parameter change with the same value
    { uint8_t m[8] = {0xF0, 0x43, 0x30, 0x5E, 0x10, 0, 0x11, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("parameter request replied", S.outQ.size() == 1);
      if (S.outQ.size() == 1) { auto& r = S.outQ[0];
          ck("reply is a parameter change", r.size() == 10 && r[2] == 0x10 && r[4] == 0x10 && r[6] == 0x11);
          ck("reply carries the value", (r[7] << 7 | r[8]) == 100); } }

    // 3. bulk dumps: request, check the checksum, feed it back, state unchanged
    uint8_t before[400]; S.current_perf_bytes(before);
    { uint8_t m[8] = {0xF0, 0x43, 0x20, 0x5E, 0x10, 0, 0, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("performance dump replied", S.outQ.size() == 1);
      if (S.outQ.size() == 1) {
          auto r = S.outQ[0];
          int n = r[4] << 7 | r[5];
          ck("byte count 400", n == 400);
          int sum = 0; for (size_t i = 4; i + 1 < r.size(); i++) sum += r[i];
          ck("checksum", (sum & 0x7F) == 0);
          for (int i = 0; i < 4; i++) S.perf.part[i].p[0x0E] = 1;          // scribble, then reload
          ck("bulk reloaded", load_sysex(S, nullptr, r.data(), r.size(), 0, 0));
          uint8_t after[400]; S.current_perf_bytes(after);
          ck("performance survived the round trip", memcmp(before, after, 400) == 0);
      } }
    { uint8_t m[8] = {0xF0, 0x43, 0x20, 0x5E, 0x40, 0, 0, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("voice dump replied", S.outQ.size() == 1 && (S.outQ[0][4] << 7 | S.outQ[0][5]) == 608); }
    { uint8_t m[8] = {0xF0, 0x43, 0x20, 0x5E, 0x00, 0, 0, 0xF7};
      S.outQ.clear(); apply_param_change_locked(S, m, 8);
      ck("system dump replied", S.outQ.size() == 1 && (S.outQ[0][4] << 7 | S.outQ[0][5]) == 76); }

    // 4. RPN / NRPN and bank select
    S.perf.part[0].p[4] = 0; S.forceChannel = 0;
    S.midi_in(0xB0, 101, 0); S.midi_in(0xB0, 100, 0); S.midi_in(0xB0, 6, 12);
    ck("RPN bend range", S.perf.part[0].p[0x26] == 0x4C && S.perf.part[0].p[0x27] == 0x34);
    S.midi_in(0xB0, 100, 2); S.midi_in(0xB0, 6, 0x28 + 12);
    ck("RPN note shift", S.perf.part[0].p[8] == 12);
    S.midi_in(0xB0, 99, 1); S.midi_in(0xB0, 98, 0x20); S.midi_in(0xB0, 6, 90);
    ck("NRPN filter cutoff", S.perf.part[0].p[0x18] == 90);
    S.midi_in(0xB0, 0, 0x3F); S.midi_in(0xB0, 32, 3);
    ck("bank select", S.perf.part[0].p[1] == 4);
    S.midi_in(0xB0, 126, 0); ck("mono mode", S.perf.part[0].p[5] == 0);
    S.midi_in(0xB0, 127, 0); ck("poly mode", S.perf.part[0].p[5] == 1);

    // 5. the controller matrix (FUN_00016F9C / F58 / 17032 and the source order in FUN_000191C0)
    ck("scaler f9c centre", Synth::scale_f9c(0, 0) == 0);
    ck("scaler f9c full", Synth::scale_f9c(127, 63) == 127);          // (128 * 64 * 2) >> 6 saturates
    ck("scaler f9c half", Synth::scale_f9c(64, 16) == 34);            // (65 * 17 * 2) >> 6
    ck("scaler f9c negative", Synth::scale_f9c(-128, 63) == -128);
    ck("scaler f9c inverted", Synth::scale_f9c(64, -16) == -33);      // a negative depth takes no bias
    ck("scaler f58 adds to the byte", Synth::scale_f58(64, 16, 64) == 64 + 17);
    ck("scaler f58 clamps low", Synth::scale_f58(-128, 63, 10) == 0);
    ck("scaler f58 clamps high", Synth::scale_f58(127, 63, 100) == 127);
    ck("scaler 17032 is f58 doubled less one", Synth::scale_f032(64, 16, 0) == 33);
    {
        Synth& T = S;
        memset(T.perf.c + 0x28, 0, 0x28);
        T.perf.c[0x28] = 0x0F;                                        // set 1 on for every part
        T.perf.c[0x30] = 0; T.perf.c[0x31] = 1 << Synth::SRC_KN1;     // source KN1
        T.perf.c[0x40] = 37;                                          // destination: voiced band width
        T.perf.c[0x48] = 64 + 32;                                     // depth +32
        memset(T.perf.part[0].src, 0, sizeof T.perf.part[0].src);
        ck("no source, no offset", T.ctrl_offset(0, 37) == 0);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 127);
        ck("KN1 is bipolar", T.perf.part[0].src[Synth::SRC_KN1] == 127);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 64);
        ck("KN1 centre is zero", T.perf.part[0].src[Synth::SRC_KN1] == 0);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 0);
        ck("KN1 bottom is -128", T.perf.part[0].src[Synth::SRC_KN1] == -128);
        T.set_source(T.perf.part[0], Synth::SRC_MW, 100);
        ck("the wheel keeps its raw value", T.perf.part[0].src[Synth::SRC_MW] == 100);
        T.set_source(T.perf.part[0], Synth::SRC_KN1, 127);
        ck("offset follows the source", T.ctrl_offset(0, 37) == Synth::scale_f9c(127, 32));
        ck("another destination stays clear", T.ctrl_offset(0, 38) == 0);
        T.perf.c[0x28] = 0x0E;                                        // part 1 switched out of the set
        ck("the part switch gates it", T.ctrl_offset(0, 37) == 0);
        T.perf.c[0x40] = 46;                                          // Fseq speed is performance-wide
        ck("a global destination ignores the part switch", T.ctrl_offset(0, 46) != 0);
        T.perf.c[0x28] = 0x0F; T.perf.c[0x40] = 21;                   // filter cutoff edits the part byte
        ck("a part-byte destination lands on the byte",
           T.ctrl_part(0, 21, 64) == Synth::scale_f58(127, 32, 64));
        // Two sets on one destination do not compose: each handler stores a per-part byte, so the
        // highest-numbered one wins (FUN_00014DDC walks 0 to 7, ctrlDest_37 writes DAT_01029758).
        T.perf.c[0x40] = 37; T.perf.c[0x29] = 0x0F;
        T.perf.c[0x32] = 0; T.perf.c[0x33] = 1 << Synth::SRC_KN1;
        T.perf.c[0x41] = 37; T.perf.c[0x49] = 64 + 16;                // set 2, same destination, depth +16
        ck("the higher set wins, it does not sum", T.ctrl_offset(0, 37) == Synth::scale_f9c(127, 16));
        // Frequency bias reaches formant and fixed operators only: the ratio branch of FUN_0001E838
        // adds nothing, which is what detuned the demo's ratio operators under a moving controller.
        T.perf.c[0x41] = 36; T.perf.c[0x49] = 64 + 16;                // set 2 -> frequency bias
        T.perf.c[0x40] = 0;
        Voice& V = T.perf.part[0].voice;
        V.v[0].form = 7; V.v[0].fbias = 7 + 4; V.v[1].form = 0; V.v[1].fixed = 0; V.v[1].fbias = 7 + 4;
        T.refresh_bias(T.ch[0], T.perf.part[0]);
        int want = (int16_t)(Synth::scale_f9c(127, 16) * 4 * 0x10) >> 2;
        ck("the formant operator takes the frequency bias", T.ch[0].fbW[0] == want && want != 0);
        ck("the ratio operator does not", T.ch[0].fbW[1] == 0);
        memset(T.perf.c + 0x28, 0, 0x28);
        init_default_voice(V);
    }

    // 6. notes still sound and stop
    S.midi_in(0x90, 60, 100);
    int live = 0; for (auto& c : S.ch) if (c.active) live++;
    ck("note on allocated a channel", live > 0);
    std::vector<float> bl(4096), br(4096);
    S.render(bl.data(), br.data(), 4096);
    double pk = 0; for (auto v : bl) pk = std::max(pk, (double)fabs(v));
    ck("note produced audio", pk > 0.0001);
    S.midi_in(0x80, 60, 0); S.midi_in(0xB0, 120, 0);
    live = 0; for (auto& c : S.ch) if (c.active) live++;
    ck("all sound off", live == 0);

    printf(g_fails ? "selftest: %d FAILURES\n" : "selftest: ok\n", g_fails);
    return g_fails ? 1 : 0;
}

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
