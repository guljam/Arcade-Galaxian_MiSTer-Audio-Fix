// fbn_fix.cpp - bit exact C++ model of rtl/galaxian_snd_fbn.vhd together with
// its testbench tb_galaxian_snd_fbn.vhd, clock edge by clock edge.
//
// Reads the constants from rtl/galaxian_snd_fbn_pkg.vhd. The output must be
// identical to the testbench output; long runs are done with this model.
//
// usage: fbn_fix <pkg.vhd> <writes.txt> <out.txt> <msec> [audio.raw]
//   out.txt  : one line per sample (every 250 clocks), the audio value
//   audio.raw: optional int16 stream, 48 kHz

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

static void load_pkg(const char *fn)
{
	FILE *f = fopen(fn, "r"); if (!f) { perror(fn); exit(1); }
	std::string all; char buf[65536]; size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
	fclose(f);
	size_t pos = 0;
	while ((pos = all.find("constant ", pos)) != std::string::npos) {
		pos += 9;
		size_t e = all.find_first_of(" :", pos);
		std::string name = all.substr(pos, e - pos);
		size_t eq = all.find(":=", pos); if (eq == std::string::npos) break;
		size_t semi = all.find(";\n", eq);
		std::string v = all.substr(eq + 2, semi - eq - 2);
		pos = semi;
		size_t i = v.find_first_not_of(" \t\n");
		if (v[i] != '(') { C[name] = strtoll(v.c_str() + i, nullptr, 10); continue; }
		std::vector<i64> arr;
		const char *p = v.c_str() + i + 1;
		while (*p) {
			while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t' || *p == '\r') p++;
			if (*p == ')' || !*p) break;
			if (*p == 'x') { arr.push_back(strtoll(p + 2, (char **)&p, 16)); p++; }
			else if (!strncmp(p, "to_unsigned(", 12)) { arr.push_back(strtoll(p + 12, (char **)&p, 10)); p = strchr(p, ')') + 1; }
			else arr.push_back(strtoll(p, (char **)&p, 10));
		}
		T[name] = arr;
	}
}
static i64 c(const char *n) { auto it = C.find(n); if (it == C.end()) { fprintf(stderr, "missing %s\n", n); exit(1); } return it->second; }
static const std::vector<i64> &t(const char *n) { auto it = T.find(n); if (it == T.end()) { fprintf(stderr, "missing %s\n", n); exit(1); } return it->second; }

static i64 sext(i64 v, int bits) { v &= (i64(1) << bits) - 1; if (v >> (bits - 1)) v -= i64(1) << bits; return v; }
static int clip16(i64 x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : (int)x; }
static unsigned lfsr_shift(unsigned g) { unsigned n = (g << 1) & 0x3ffff; n |= (((n >> 17) & 1) ^ 1) ^ ((n >> 5) & 1); return n; }

static int solo = -1;   // debug: 0 tone, 1 noise, 2 fire, 3 swarm (env FBN_SOLO)

struct In { int rst = 1, bd = 0, pwe = 0, fs = 0, hit_wr = 0, fire = 0, vol1 = 0, vol2 = 0, dac = 0, vs = 0; };

struct Reg {
	int div = 0, st = 31;
	int hit_wr_l = 0, vs_l = 0, hit_pend = 0, frame_pend = 0, pitch_pend = 0, pitch_new = 255;
	int fs = 0, vol = 0, fire_l = 0;
	int pitch = 255, t_cnt = 0, t_cd = 0, t_mix = 0;
	int n_vol = 0, n_hold = 0, f3 = 0, n_idx = 0, n_frac = 0; unsigned n_gen = 0; int n_shift = 0;
	i64 x = 0, y = 0, s = 0, tA = 0, tB = 0, ls_z1 = 0, ls_z2 = 0, lp_z1 = 0, lp_z2 = 0;
	int n_out = 0; i64 mp = 0;
	int sh_en = 0, sh_idx = 0, sh_frac = 0, sh_q = 0;
	i64 lfo_f = 0; i64 lfo_pos[3] = {0, 0, 0}, lfo_step[3] = {0, 0, 0};
	int audio = 0;
};

struct Dut {
	std::vector<i64> TONE, NAMP, FVAR, KSTEP, ROM;
	i64 G0, LS[5], LP[5], LMIN, LMAX, SLEN;
	Reg r;

	void init() {
		TONE = t("TONE_WAVE"); NAMP = t("NOISE_AMP"); FVAR = t("LFO_FRAME_VAR"); KSTEP = t("LFO_STEP_K"); ROM = t("SHOOT_ROM");
		G0 = c("NOISE_G0"); LMIN = c("LFO_MIN"); LMAX = c("LFO_MAX"); SLEN = c("SHOOT_LEN");
		const char *ln[5] = {"LS_A0", "LS_A1", "LS_A2", "LS_B1", "LS_B2"}, *pn[5] = {"LP_A0", "LP_A1", "LP_A2", "LP_B1", "LP_B2"};
		for (int i = 0; i < 5; i++) { LS[i] = c(ln[i]); LP[i] = c(pn[i]); }
		r.n_gen = (unsigned)G0; r.lfo_f = LMAX << 32;
	}

	// one rising edge: read r (old values), build n (new values)
	void edge(const In &in) {
		const Reg &o = r;
		Reg n = r;
		n.sh_q = (int)ROM[o.sh_idx];
		if (in.rst) {
			n.div = 0; n.st = 31; n.hit_wr_l = 0; n.vs_l = 0; n.hit_pend = 0; n.frame_pend = 0; n.pitch_pend = 0; n.pitch_new = 255;
			n.fs = 0; n.vol = 0; n.fire_l = 0; n.pitch = 255; n.t_cnt = 0; n.t_cd = 0; n.t_mix = 0;
			n.n_vol = 0; n.n_hold = 0; n.f3 = 0; n.n_idx = 0; n.n_frac = 0; n.n_gen = (unsigned)G0; n.n_shift = 0;
			n.ls_z1 = n.ls_z2 = n.lp_z1 = n.lp_z2 = 0; n.n_out = 0; n.sh_en = 0; n.sh_idx = 0; n.sh_frac = 0;
			n.lfo_f = LMAX << 32; for (int i = 0; i < 3; i++) n.lfo_pos[i] = 0; n.audio = 0;
			r = n; return;
		}
		n.hit_wr_l = in.hit_wr; n.vs_l = in.vs;
		bool hit_ev = in.hit_wr && !o.hit_wr_l, vs_ev = in.vs && !o.vs_l;
		if (hit_ev) n.hit_pend = 1;
		if (vs_ev) n.frame_pend = 1;
		if (in.pwe) { n.pitch_pend = 1; n.pitch_new = in.bd; }
		if (o.n_shift) { unsigned g = o.n_gen; for (int i = 0; i < 8; i++) g = lfsr_shift(g); n.n_gen = g; n.n_shift = o.n_shift - 1; }
		n.div = o.div == 249 ? 0 : o.div + 1;
		if (o.div == 0) n.st = 0; else if (o.st != 31) n.st = o.st + 1;

		bool use_m = false; i64 ma = 0, mb = 0;
		auto sh = [](i64 v, int k) { return v >> k; };   // arithmetic
		switch (o.st) {
		case 0: {
			if (o.frame_pend && !vs_ev) n.frame_pend = 0;
			int v = o.n_vol, b = o.n_hold;
			if (o.frame_pend) {
				if (b) b--;
				if (o.f3 == 0 && b == 0 && v > 0) { v -= v / 10 + 1; if (v < 0) v = 0; }
				n.f3 = o.f3 == 2 ? 0 : o.f3 + 1;
				if (o.lfo_f > (LMIN << 32)) n.lfo_f = o.lfo_f - FVAR[in.dac]; else n.lfo_f = LMAX << 32;
			}
			if (o.hit_pend) {
				if (!hit_ev) n.hit_pend = 0;
				if (b == 0) { n.n_idx = 0; n.n_frac = 0; n.n_gen = (unsigned)G0; n.n_shift = 0; }
				b = 20; v = 100;
			}
			n.n_vol = v; n.n_hold = b;
			n.fire_l = in.fire;
			if (in.fire && !o.fire_l) { n.sh_en = 1; n.sh_idx = 0; n.sh_frac = 0; }
			if (o.pitch_pend) { if (!in.pwe) n.pitch_pend = 0; n.pitch = o.pitch_new; }
			n.fs = in.fs; n.vol = in.vol2 * 2 + in.vol1; n.t_mix = 0;
			break;
		}
		case 1:
			n.x = ((o.n_gen >> 17) & 1) ? (NAMP[o.n_vol] << 20) : -(NAMP[o.n_vol] << 20);
			break;
		case 2: case 3: case 4:
			use_m = true; ma = sext(o.lfo_f >> 5, 36); mb = KSTEP[o.st - 2]; break;
		case 5: use_m = true; ma = o.x; mb = LS[0]; break;
		case 6: n.y = sh(o.mp, 25) + o.ls_z1; use_m = true; ma = o.x; mb = LS[1]; break;
		case 7: n.tA = sh(o.mp, 25) + o.ls_z2; use_m = true; ma = sext(o.y, 36); mb = LS[3]; break;
		case 8: n.tA = o.tA - sh(o.mp, 25); use_m = true; ma = o.x; mb = LS[2]; break;
		case 9: n.tB = sh(o.mp, 25); use_m = true; ma = sext(o.y, 36); mb = LS[4]; break;
		case 10: n.ls_z2 = o.tB - sh(o.mp, 25); n.ls_z1 = o.tA; n.s = o.y + o.x; break;
		case 11: use_m = true; ma = sext(o.s, 36); mb = LP[0]; break;
		case 12: n.y = sh(o.mp, 25) + o.lp_z1; use_m = true; ma = sext(o.s, 36); mb = LP[1]; break;
		case 13: n.tA = sh(o.mp, 25) + o.lp_z2; use_m = true; ma = sext(o.y, 36); mb = LP[3]; break;
		case 14: n.tA = o.tA - sh(o.mp, 25); use_m = true; ma = sext(o.s, 36); mb = LP[2]; break;
		case 15: n.tB = sh(o.mp, 25); use_m = true; ma = sext(o.y, 36); mb = LP[4]; break;
		case 16:
			n.lp_z2 = o.tB - sh(o.mp, 25); n.lp_z1 = o.tA;
			n.n_out = o.y < 0 ? -(int)((-o.y) >> 20) : (int)(o.y >> 20);
			break;
		case 17: {
			int b = 0;
			if (o.pitch != 255 && (solo < 0 || solo == 0)) { int ts = o.t_mix / 16; ts >>= 4; b = clip16(ts * 4); }
			if (solo < 0 || solo == 1) b = clip16(b + clip16((i64)o.n_out * 4));
			if (o.sh_en) {
				if (solo < 0 || solo == 2) b = clip16(b + o.sh_q * 4);
				int nf = o.sh_frac + 147, ni = o.sh_idx;
				if (nf >= 320) { nf -= 320; ni++; }
				if (ni > SLEN || (ni == SLEN && nf > 0)) { n.sh_en = 0; n.sh_idx = 0; n.sh_frac = 0; }
				else { n.sh_idx = ni; n.sh_frac = nf; }
			}
			for (int i = 0; i < 3; i++) {
				if (((o.fs >> i) & 1) && (solo < 0 || solo == 3)) b = clip16(b + (((o.lfo_pos[i] >> 36) & 1) ? -248 : 244));
				i64 p = o.lfo_pos[i] + o.lfo_step[i];
				n.lfo_pos[i] = p > (i64(32) << 32) ? 0 : p;
			}
			n.audio = b;
			int nf = o.n_frac + 1, ni = o.n_idx;
			if (nf == 6) { nf = 0; ni++; }
			if (ni > 32000 || (ni == 32000 && nf > 0)) { n.n_idx = 0; n.n_frac = 0; n.n_gen = (unsigned)G0; n.n_shift = 0; }
			else { n.n_idx = ni; n.n_frac = nf; if (nf == 0) n.n_shift = 96; }
			break;
		}
		default: break;
		}
		if (o.st >= 3 && o.st <= 5) n.lfo_step[o.st - 3] = (o.mp >> 30) & ((i64(1) << 38) - 1);
		if (o.st >= 1 && o.st <= 16 && o.pitch != 255) {
			int cd = o.t_cd, cnt = o.t_cnt, mix = o.t_mix;
			if (cd >= 128) { cnt = (cnt + 1) & 15; cd = o.pitch >> 1; }
			cd++;
			mix += (int)TONE[o.vol * 16 + cnt];
			n.t_cd = cd; n.t_cnt = cnt; n.t_mix = mix;
		}
		if (use_m) n.mp = ma * mb;
		r = n;
	}
};

int main(int argc, char **argv)
{
	if (argc < 5) { fprintf(stderr, "usage: fbn_fix <pkg.vhd> <writes.txt> <out.txt> <msec> [audio.raw]\n"); return 1; }
	if (getenv("FBN_SOLO")) solo = atoi(getenv("FBN_SOLO"));
	load_pkg(argv[1]);
	Dut d; d.init();
	FILE *wf = fopen(argv[2], "r"); if (!wf) { perror(argv[2]); return 1; }
	FILE *of = fopen(argv[3], "w"); if (!of) { perror(argv[3]); return 1; }
	FILE *rf = argc > 5 ? fopen(argv[5], "wb") : nullptr;
	i64 msec = atoll(argv[4]);
	double t; char k; int a, v;
	bool have = fscanf(wf, "%lf %c %d %d", &t, &k, &a, &v) == 4;
	In in;
	// tb_galaxian_snd_fbn: edge counter n, inputs assigned at edge n are seen at edge n+1
	for (i64 n = 1; n <= msec * 12000; n++) {
		if (n % 250 == 0) {
			fprintf(of, "%d\n", d.r.audio);
			if (rf) { int16_t s = (int16_t)d.r.audio; fwrite(&s, 2, 1, rf); }
		}
		d.edge(in);
		in.rst = n < 4;
		in.pwe = 0; in.hit_wr = 0;
		in.vs = (n % 198000) >= 100 && (n % 198000) < 1100;
		while (have && (i64)(t * 12.0e6 + 0.5) <= n) {
			if (k == 'L') in.dac = (in.dac & ~(1 << a)) | ((v & 1) << a);
			else if (k == 'P') { in.bd = v; in.pwe = 1; }
			else switch (a) {
				case 0: case 1: case 2: in.fs = (in.fs & ~(1 << a)) | ((v & 1) << a); break;
				case 3: if (v & 1) in.hit_wr = 1; break;
				case 5: in.fire = v & 1; break;
				case 6: in.vol1 = v & 1; break;
				case 7: in.vol2 = v & 1; break;
			}
			have = fscanf(wf, "%lf %c %d %d", &t, &k, &a, &v) == 4;
		}
	}
	fclose(of); if (rf) fclose(rf); fclose(wf);
	return 0;
}
