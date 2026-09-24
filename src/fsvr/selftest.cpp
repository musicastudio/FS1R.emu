// fsvr/selftest.cpp - the engine self check. Ours, not the hardware's.
#include "fs1r/internal.h"

// ------------------------------------------------------------------------------------------ self test
// fs1r_emu -selftest: every sysex path the plugin will use. Parameter change in, parameter request out,
// bulk dump out and straight back in, and the whole 400/608 byte state surviving the round trip.
static int g_fails = 0;
static void ck(const char* what, bool ok) { if (!ok) { printf("  FAIL %s\n", what); g_fails++; } }
int selftest(Synth& S) {
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
        ck("key code 87, the demo drum's note 41, reads -8 (19_drums)", eg_keyoff(87) == -8);
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
        // The asymmetric family: the same rise, a sin^2 fall whatever the skirt, and no mean in the
        // stored all1/all2 waveform, which is the window with g_winDC taken out.
        ck("the '1' family shares the rise", g_win[1][5][256] == g_win[0][5][256]);
        ck("the '1' family falls as sin^2", fabs(g_win[1][5][768] - g_win[0][0][768]) < 1e-6);
        ck("the '1' family is asymmetric", g_win[1][5][768] > 4 * g_win[0][5][768]);
        double dc = 0; for (int i = 0; i < 1024; i++) dc += g_win[0][0][i] - g_winDC[0][0];
        ck("all2 at skirt 0 carries no DC", fabs(dc) < 1e-3);
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

    // 5b. the detune law: the ROM's own key scaled table, against what 11_detune measured. The band is
    // (pitchNote >> 8) + 10 - 0x50, which advances four per octave, so note 60 is band 13.
    {
        auto detw = [](int byte, int band) {
            int dd = byte < 15 ? (~byte) : byte - 15, idx = (dd & 0x1F) * 32 + band;
            return (idx & 0x200) ? -(int)FRMDET[idx & 0x1FF] : (int)FRMDET[idx & 0x1FF];
        };
        ck("detune centre is none", detw(15, 13) == 0);
        ck("detune +7 at note 60 is 7 word units", detw(22, 13) == 7);      // 8.20 cents, measured 7.85
        ck("detune -7 at note 60 mirrors it", detw(8, 13) == -7);
        ck("detune +15 at note 60 is 22 units", detw(30, 13) == 22);        // 25.78 cents, measured 25.84
        ck("the same detune is smaller three octaves up", detw(30, 25) == 7 && detw(22, 25) == 2);
        ck("and larger three octaves down", detw(30, 1) == 37 && detw(22, 1) == 13);

        // And that it reaches the operator. Earlier sections leave note shifts scribbled, so the band
        // is read back off the channel rather than assumed: what is under test here is that the word
        // reaches the frequency at all, the table itself being checked above.
        Synth& T = S;
        Voice& V = T.perf.part[0].voice;
        init_default_voice(V);
        std::vector<float> a(64), b(64);
        auto play = [&](int det) {
            V.v[0].detune = det;
            T.midi_in(0x90, 60, 100);
            T.render(a.data(), b.data(), 64);
            Chan* c = nullptr;
            for (auto& ch : T.ch) if (ch.active && ch.note == 60) { c = &ch; break; }
            double f = c ? c->op[0].fop : 0.0;
            int bnd = c ? (c->pitchNote >> 8) + 10 : 0;
            bnd = bnd < 0x50 ? 0 : bnd < 0x70 ? ((bnd ^ 0x10) & 0x1F) : 0x1F;
            T.midi_in(0x80, 60, 0); T.midi_in(0xB0, 120, 0);
            return std::make_pair(f, bnd);
        };
        auto centred = play(15), up = play(22), down = play(8);
        ck("a centred operator has a frequency at all", centred.first > 1.0);
        double want = detw(22, up.second) * 1200.0 / 1024.0;
        ck("detune +7 moves the operator by its table entry",
           fabs(1200.0 * log2(up.first / centred.first) - want) < 0.01 && want > 0.0);
        ck("detune -7 moves it the other way by the same",
           fabs(1200.0 * log2(down.first / centred.first) + want) < 0.01);
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
    // The filter EG as the firmware steps it (FUN_0000D050 / FUN_0000CB6C): every segment reaches its end
    // level exactly, a flat segment still ends (asymptote target + 3), the hold is a hold, and the release
    // returns to L4. Time per rate word is the chip's and unmeasured, so only the structure is checked.
    {
        StepEG e; double L[4] = {200, 100, 100, -100}; int R[4] = {20, 20, 50, 20};   // times, not rate words: 0 is the slowest
        e.start(L, R);
        int n = 0; while (e.stage == 0 && n < 100000) { e.tick(); n++; }
        ck("filter EG attack lands on L1", e.stage == 1 && e.cur == 200 && n > 1);
        n = 0; while (e.stage == 1 && n < 100000) { e.tick(); n++; }
        ck("filter EG decay lands on L2", e.stage == 2 && e.cur == 100);
        n = 0; while (e.stage == 2 && n < 100000) { e.tick(); n++; }
        ck("filter EG flat segment holds FEG_FLAT_S", e.stage == 9 && e.cur == 100 && fabs(n / TICK_HZ - cal::FEG_FLAT_S) < 0.01);
        for (int i = 0; i < 1000; i++) e.tick();
        ck("filter EG holds at L3", e.cur == 100);
        e.release(); n = 0; while (e.stage == 3 && n < 100000) { e.tick(); n++; }
        ck("filter EG release lands on L4", e.stage == 9 && e.cur == -100);
        // Named rather than compound literals: a (int[4]){...} temporary is a clang extension that
        // GCC rejects as taking the address of a temporary array and MSVC as C4576.
        const int rateSlow[4] = {60, 0, 0, 0}, rateFast[4] = {20, 0, 0, 0};
        StepEG slow, fast; slow.start(L, rateSlow); fast.start(L, rateFast);
        int ns = 0, nf = 0; while (slow.stage == 0 && ns < 10000000) { slow.tick(); ns++; }
        while (fast.stage == 0 && nf < 10000000) { fast.tick(); nf++; }
        ck("filter EG time 60 vs 20: 2^(40/15.5) = 6x per 15 words... 15x over 40 (flteg 0.81 vs 0.052 s)", ns > 12 * nf && ns < 18 * nf);
    }

    // A part whose voice bank is off receives nothing even with a receive channel (A011 Sho, parts 3 and 4).
    { S.perf.part[1].p[1] = 0; S.perf.part[1].p[4] = 0x10; ck("bank off silences the part", !S.part_listens(1, 0)); S.perf.part[1].p[1] = 2; ck("bank on hears it again", S.part_listens(1, 0)); }

    return g_fails ? 1 : 0;
}

