// galsnd_fix.cpp - bit exact C++ model of rtl/galaxian_snd.vhd, clock by clock.
//
// It reads its constants from rtl/galaxian_snd_pkg.vhd and reproduces the
// testbench (tb_galaxian_snd.vhd) timing exactly, so its output file must be
// identical to the GHDL output. GHDL is slow on the wide multiplies, so long
// runs are done with this model and GHDL only proves equivalence.
//
// usage: galsnd_fix <pkg.vhd> <writes.txt> <out.txt> <msec> [mooncr] [audio.raw]
//   out.txt  : same format as the testbench output
//   audio.raw: optional int16 stream at 750 kHz (every step)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

typedef int64_t i64;
static std::map<std::string, i64> C;
static std::map<std::string, std::vector<i64>> T;

static i64 sext(i64 v, int bits) { v &= (i64(1) << bits) - 1; if (v >> (bits - 1)) v -= i64(1) << bits; return v; }

static void load_pkg(const char *fn) {
	FILE *f = fopen(fn, "r"); if (!f) { perror(fn); exit(1); }
	char line[4096];
	while (fgets(line, sizeof line, f)) {
		char name[128];
		char *p = strstr(line, "constant ");
		if (!p) continue;
		if (sscanf(p, "constant %127s", name) != 1) continue;
		char *eq = strstr(p, ":=");
		if (!eq) continue;
		eq += 2;
		while (*eq == ' ') eq++;
		if (*eq == 'x') { C[name] = sext(strtoll(eq + 2, nullptr, 16), 40); }
		else if (*eq == '(') {
			std::vector<i64> v; char *q = eq + 1;
			while (*q && *q != ')') { v.push_back(strtoll(q, &q, 10)); while (*q == ',' || *q == ' ') q++; }
			T[name] = v;
		} else C[name] = strtoll(eq, nullptr, 10);
	}
	fclose(f);
}
static i64 c(const char *n) { auto it = C.find(n); if (it == C.end()) { fprintf(stderr, "missing %s\n", n); exit(1); } return it->second; }
static const std::vector<i64> &t(const char *n) { auto it = T.find(n); if (it == T.end()) { fprintf(stderr, "missing %s\n", n); exit(1); } return it->second; }

static i64 mulc(i64 x, i64 k, i64 s) {
	i64 xs = sext(x >> 9, 27);
	i64 p = xs * k;
	p += i64(1) << (s - 10);
	return sext(p >> (s - 9), 40);
}

struct In { int rst = 1, pwe = 0, bd = 0, fs[3] = {0, 0, 0}, hit = 0, fire = 0, vol1 = 0, vol2 = 0, dac = 0, mooncr = 0; };

struct Dut {
	// constants
	i64 ZERO = 0, V_025, V_5, LFO_TH, LFO_SPAN, CV_GAIN_K, CV_GAIN_S, CV_OFS;
	i64 VK[4][3];   // KC, KD, S for VCO1..3, FVCO
	i64 VCO_HIGH, BG_K, BG_S, HIT_K, HIT_S, HIT_U, BP_C1_K, BP_C1_S, BP_C2_K, BP_C2_S, BP_B0G_K, BP_B0G_S;
	i64 BP_VREF, BP_YMAX, BP_YMIN, F173_K, F173_S, V_4, F_NOISE_CV, F_CV_G_K, F_CV_G_S, F182_K, F182_S;
	i64 C26G_K, C26G_S, C26M_K, C26M_S, AMP_K, AMP_S, AMP_DC_GAL, AMP_DC_MC, MW_BG_S, MW_HIT_S, MW_FIRE_S;
	std::vector<i64> LFO_SLOPE, PW_A, PW_C, PW_D, MW_BG_K, MW_HIT_K, MW_FIRE_K;
	i64 SAT_HI = i64(32767) << 17, SAT_LO = i64(-32768) << 17;

	// registers
	int step_cnt = 0, pitch = 255, clk_acc = 0, note_c1 = 255, note_c2 = 0;
	int acc_a = 0, acc_c = 0, acc_d = 0, cnt_a = 0, cnt_c = 0, cnt_d = 0;
	int rng_acc = 0, v2_acc = 0, lfsr = 1 << 17, noise = 0;
	i64 lfo_cap = 0, cv = 0, vco_cap[3] = {0, 0, 0}; int vco_ff[3] = {1, 1, 1};
	i64 bg = 0, hit_cap = 0, bp_x1 = 0, bp_x2 = 0, bp_y1 = 0, bp_y2 = 0, hit_out, f173 = 0, fvco_cap = 0;
	int fvco_ff = 1;
	i64 f182_cap = 0, fire_out = 0, c26_cap = 0, amp_cap; int audio = 0;

	void init() {
		V_025 = c("V_025"); V_5 = c("V_5"); LFO_TH = c("LFO_TH"); LFO_SPAN = c("LFO_SPAN");
		CV_GAIN_K = c("CV_GAIN_K"); CV_GAIN_S = c("CV_GAIN_S"); CV_OFS = c("CV_OFS");
		const char *vn[4] = {"VCO1", "VCO2", "VCO3", "FVCO"};
		for (int i = 0; i < 4; i++) {
			std::string b = vn[i];
			VK[i][0] = c((b + "_KC").c_str()); VK[i][1] = c((b + "_KD").c_str()); VK[i][2] = c((b + "_S").c_str());
		}
		VCO_HIGH = c("VCO_HIGH_X1_5"); BG_K = c("BG_K_K"); BG_S = c("BG_K_S");
		HIT_K = c("HIT_K_K"); HIT_S = c("HIT_K_S"); HIT_U = c("HIT_U");
		BP_C1_K = c("BP_C1_K"); BP_C1_S = c("BP_C1_S"); BP_C2_K = c("BP_C2_K"); BP_C2_S = c("BP_C2_S");
		BP_B0G_K = c("BP_B0G_K"); BP_B0G_S = c("BP_B0G_S");
		BP_VREF = c("BP_VREF"); BP_YMAX = c("BP_YMAX"); BP_YMIN = c("BP_YMIN");
		F173_K = c("F173_K_K"); F173_S = c("F173_K_S"); V_4 = c("V_4"); F_NOISE_CV = c("F_NOISE_CV");
		F_CV_G_K = c("F_CV_G_K"); F_CV_G_S = c("F_CV_G_S"); F182_K = c("F182_K_K"); F182_S = c("F182_K_S");
		C26G_K = c("C26_K_GAL_K"); C26G_S = c("C26_K_GAL_S"); C26M_K = c("C26_K_MC_K"); C26M_S = c("C26_K_MC_S");
		AMP_K = c("AMP_K_K"); AMP_S = c("AMP_K_S"); AMP_DC_GAL = c("AMP_DC_GAL"); AMP_DC_MC = c("AMP_DC_MC");
		MW_BG_S = c("MW_BG_S"); MW_HIT_S = c("MW_HIT_S"); MW_FIRE_S = c("MW_FIRE_S");
		LFO_SLOPE = t("LFO_SLOPE"); PW_A = t("PW_A"); PW_C = t("PW_C"); PW_D = t("PW_D");
		MW_BG_K = t("MW_BG_K"); MW_HIT_K = t("MW_HIT_K"); MW_FIRE_K = t("MW_FIRE_K");
		hit_out = BP_VREF; amp_cap = AMP_DC_GAL;
	}

	void astable(int en, i64 cvv, int i, i64 &v, int &ff) {
		if (!en) { v = 0; ff = 1; return; }
		if (cvv < V_025) return;
		i64 th = cvv, tr = cvv >> 1, a, k;
		if (v >= th) ff = 0; else if (v <= tr) ff = 1;
		if (ff) { a = V_5 - v; k = VK[i][0]; } else { a = -v; k = VK[i][1]; }
		i64 vn = v + mulc(a, k, VK[i][2]);
		if (ff) { if (vn >= th) { vn = th; ff = 0; } }
		else { if (vn <= tr) { vn = tr; ff = 1; } }
		v = vn;
	}
	void rcdisc5(int en, i64 u, i64 k, i64 s, i64 &cap, i64 &o) {
		if (en) { if (u < cap) cap = cap + mulc(u - cap, k, s); else cap = u; o = cap; }
		else { if (u > cap) cap = u; o = 0; }
	}

	void clock(const In &in) {
		if (in.rst) {
			*this = Dut(*this).reset_copy(in.mooncr);
			return;
		}
		// snapshot of registers (signal semantics)
		Dut o = *this;
		// ---------------- digital
		step_cnt = (o.step_cnt + 1) & 15;
		if (in.pwe) pitch = in.bd;
		if (o.clk_acc >= 125 - 16) {
			clk_acc = o.clk_acc - (125 - 16);
			if (o.pitch != 255) {
				if (o.note_c1 == 255) { note_c1 = o.pitch; note_c2 = (o.note_c2 + 1) & 15; }
				else note_c1 = o.note_c1 + 1;
			}
		} else clk_acc = o.clk_acc + 16;
		int qa = o.note_c2 & 1, qc = (o.note_c2 >> 2) & 1, qd = (o.note_c2 >> 3) & 1;
		if (o.step_cnt == 15) {
			cnt_a = (o.acc_a + qa) & 31; cnt_c = (o.acc_c + qc) & 31; cnt_d = (o.acc_d + qd) & 31;
			acc_a = acc_c = acc_d = 0;
		} else { acc_a = (o.acc_a + qa) & 31; acc_c = (o.acc_c + qc) & 31; acc_d = (o.acc_d + qd) & 31; }
		if (o.rng_acc >= 12500 - 128) {
			rng_acc = o.rng_acc - (12500 - 128);
			int l = o.lfsr; int fb = (l >> 17) & 1;
			l = (l & ~0x1ffff) | (((l << 1) | fb) & 0x1ffff);
			int nb = !(((l >> 4) & 1) ^ ((l >> 16) & 1));
			l = (l & 0x1ffff) | (nb << 17);
			lfsr = l;
		} else rng_acc = o.rng_acc + 128;
		if (o.v2_acc >= 50000 - 33) { v2_acc = o.v2_acc - (50000 - 33); noise = (o.lfsr >> 17) & 1; }
		else v2_acc = o.v2_acc + 33;

		// ---------------- analog
		if (o.step_cnt != 0) return;
		i64 lfo = o.lfo_cap + LFO_SLOPE[in.dac];
		if (lfo >= LFO_TH) lfo -= LFO_SPAN;
		lfo_cap = lfo;
		i64 cvv = mulc(o.lfo_cap, CV_GAIN_K, CV_GAIN_S) + CV_OFS;
		if (cvv < 0) cvv = 0; else if (cvv > V_5) cvv = V_5;
		cv = cvv;
		for (int i = 0; i < 3; i++) { i64 v = o.vco_cap[i]; int ff = o.vco_ff[i]; astable(in.fs[i], o.cv, i, v, ff); vco_cap[i] = v; vco_ff[i] = ff; }
		i64 tgt = 0;
		for (int i = 0; i < 3; i++) if (in.fs[i] && o.vco_ff[i]) tgt += VCO_HIGH;
		bg = o.bg + mulc(tgt - o.bg, BG_K, BG_S);
		// hit
		i64 u = in.hit ? HIT_U : 0, hc = o.hit_cap, n155;
		rcdisc5(o.noise, u, HIT_K, HIT_S, hc, n155);
		hit_cap = hc;
		i64 dx = n155 - o.bp_x2;
		i64 y = sext(o.bp_y1 << 1, 40) - o.bp_y2 + mulc(o.bp_y1, BP_C1_K, BP_C1_S) + mulc(o.bp_y2, BP_C2_K, BP_C2_S) + mulc(dx, BP_B0G_K, BP_B0G_S);
		if (y > BP_YMAX) y = BP_YMAX; else if (y < BP_YMIN) y = BP_YMIN;
		bp_x2 = o.bp_x1; bp_x1 = n155; bp_y2 = o.bp_y1; bp_y1 = y;
		hit_out = y + BP_VREF;
		// fire
		tgt = in.fire ? 0 : V_4;
		f173 = o.f173 + mulc(tgt - o.f173, F173_K, F173_S);
		i64 fc = mulc(o.f173, F_CV_G_K, F_CV_G_S);
		if (o.noise) fc += F_NOISE_CV;
		{ i64 v = o.fvco_cap; int ff = o.fvco_ff; astable(1, fc, 3, v, ff); fvco_cap = v; fvco_ff = ff; }
		u = in.fire ? HIT_U : 0; hc = o.f182_cap; i64 n182;
		rcdisc5(o.fvco_ff, u, F182_K, F182_S, hc, n182);
		f182_cap = hc; fire_out = n182;
		// mixer
		int idx = (in.vol1 ? 1 : 0) + (in.vol2 ? 2 : 0) + (in.mooncr ? 4 : 0);
		i64 c26 = in.mooncr ? o.c26_cap + mulc(o.fire_out - o.c26_cap, C26M_K, C26M_S)
		                    : o.c26_cap + mulc(o.fire_out - o.c26_cap, C26G_K, C26G_S);
		c26_cap = c26;
		i64 vf = o.fire_out - c26;
		i64 mix = o.cnt_a * PW_A[idx] + o.cnt_c * PW_C[idx] + o.cnt_d * PW_D[idx]
		        + mulc(o.bg, MW_BG_K[idx], MW_BG_S) + mulc(o.hit_out, MW_HIT_K[idx], MW_HIT_S) + mulc(vf, MW_FIRE_K[idx], MW_FIRE_S);
		i64 amp = o.amp_cap + mulc(mix - o.amp_cap, AMP_K, AMP_S);
		amp_cap = amp;
		i64 oo = mix - amp;
		if (oo >= SAT_HI) audio = 32767; else if (oo < SAT_LO) audio = -32768; else audio = (int16_t)((oo >> 17) & 0xffff);
	}

	Dut reset_copy(int mooncr) {
		Dut d; // constants copied below
		d.V_025 = V_025; d.V_5 = V_5; d.LFO_TH = LFO_TH; d.LFO_SPAN = LFO_SPAN; d.CV_GAIN_K = CV_GAIN_K; d.CV_GAIN_S = CV_GAIN_S; d.CV_OFS = CV_OFS;
		memcpy(d.VK, VK, sizeof VK);
		d.VCO_HIGH = VCO_HIGH; d.BG_K = BG_K; d.BG_S = BG_S; d.HIT_K = HIT_K; d.HIT_S = HIT_S; d.HIT_U = HIT_U;
		d.BP_C1_K = BP_C1_K; d.BP_C1_S = BP_C1_S; d.BP_C2_K = BP_C2_K; d.BP_C2_S = BP_C2_S; d.BP_B0G_K = BP_B0G_K; d.BP_B0G_S = BP_B0G_S;
		d.BP_VREF = BP_VREF; d.BP_YMAX = BP_YMAX; d.BP_YMIN = BP_YMIN; d.F173_K = F173_K; d.F173_S = F173_S; d.V_4 = V_4;
		d.F_NOISE_CV = F_NOISE_CV; d.F_CV_G_K = F_CV_G_K; d.F_CV_G_S = F_CV_G_S; d.F182_K = F182_K; d.F182_S = F182_S;
		d.C26G_K = C26G_K; d.C26G_S = C26G_S; d.C26M_K = C26M_K; d.C26M_S = C26M_S; d.AMP_K = AMP_K; d.AMP_S = AMP_S;
		d.AMP_DC_GAL = AMP_DC_GAL; d.AMP_DC_MC = AMP_DC_MC; d.MW_BG_S = MW_BG_S; d.MW_HIT_S = MW_HIT_S; d.MW_FIRE_S = MW_FIRE_S;
		d.LFO_SLOPE = LFO_SLOPE; d.PW_A = PW_A; d.PW_C = PW_C; d.PW_D = PW_D; d.MW_BG_K = MW_BG_K; d.MW_HIT_K = MW_HIT_K; d.MW_FIRE_K = MW_FIRE_K;
		d.hit_out = BP_VREF; d.amp_cap = mooncr ? AMP_DC_MC : AMP_DC_GAL;
		return d;
	}
};

int main(int argc, char **argv) {
	if (argc < 5) { fprintf(stderr, "usage: %s pkg.vhd writes.txt out.txt msec [mooncr] [audio.raw]\n", argv[0]); return 1; }
	load_pkg(argv[1]);
	struct W { double t; char k; int a, d; };
	std::vector<W> ws; { FILE *fi = fopen(argv[2], "r"); if (!fi) { perror(argv[2]); return 1; } W w; char kb[4];
		while (fscanf(fi, "%lf %3s %d %d", &w.t, kb, &w.a, &w.d) == 4) { w.k = kb[0]; ws.push_back(w); } fclose(fi); }
	long msec = atol(argv[4]);
	int mooncr = argc > 5 ? atoi(argv[5]) : 0;
	FILE *fo = fopen(argv[3], "w");
	FILE *fa = argc > 6 ? fopen(argv[6], "wb") : nullptr;
	Dut d; d.init(); d.amp_cap = mooncr ? d.AMP_DC_MC : d.AMP_DC_GAL;
	In in; in.mooncr = mooncr;
	// testbench stimulus process emulation
	size_t wi = 0; long stim_edge_ready = 1; // process is at edge 1 when it starts reading writes
	int pend_pwe_clear = 0;
	long acc = 0; int n = 0;
	long end_cycle = msec * 12000;
	for (long e = 0;; e++) {
		// mon process (reads values visible at this edge)
		if (!in.rst && d.step_cnt == 1) {
			acc += d.audio; n++;
			if (fa) { int16_t s = (int16_t)d.audio; fwrite(&s, 2, 1, fa); }
			if (n == 16) {
				fprintf(fo, "%ld %lld %lld %lld %lld %lld\n", acc / 16, (long long)(d.lfo_cap >> 12), (long long)(d.cv >> 12),
				        (long long)(d.bg >> 12), (long long)(d.hit_out >> 12), (long long)(d.fire_out >> 12));
				acc = 0; n = 0;
			}
		}
		if (e >= end_cycle) break;
		// DUT edge
		d.clock(in);
		// stimulus process wakes at this edge and assigns for the next one
		In nx = in;
		if (e == 1) nx.rst = 0;
		if (pend_pwe_clear) { nx.pwe = 0; pend_pwe_clear = 0; }
		if (e >= stim_edge_ready && !(nx.pwe)) {
			while (wi < ws.size()) {
				const W &w = ws[wi];
				if (w.t * 1000.0 >= (double)msec) { wi = ws.size(); break; }
				long cyc = (long)(w.t * 12.0e6 + 0.5);
				if (e < cyc) break;
				wi++;
				if (w.k == 'L') nx.dac = (nx.dac & ~(1 << w.a)) | ((w.d & 1) << w.a);
				else if (w.k == 'P') { nx.bd = w.d & 0xff; nx.pwe = 1; pend_pwe_clear = 1; break; }
				else switch (w.a) {
					case 0: case 1: case 2: nx.fs[w.a] = w.d & 1; break;
					case 3: nx.hit = w.d & 1; break;
					case 5: nx.fire = w.d & 1; break;
					case 6: nx.vol1 = w.d & 1; break;
					case 7: nx.vol2 = w.d & 1; break;
				}
			}
		}
		in = nx;
	}
	fclose(fo); if (fa) fclose(fa);
	return 0;
}
