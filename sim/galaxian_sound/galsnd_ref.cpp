// galsnd_ref.cpp - double precision reference model of the Galaxian sound board.
//
// This is a direct port of MAME's galaxian_discrete netlist
// (src/mame/galaxian/galaxian_a.cpp) and the discrete node code it uses
// (src/devices/sound/disc_*.hxx). It is used as the golden model for the
// FPGA implementation in rtl/galaxian_snd.vhd.
//
// usage: galsnd_ref <writes.txt> <out.raw> [sample_rate] [seconds]
//   writes.txt : "<time_sec> <L|S|P> <offset> <data>" (see mame_log.lua)
//   out.raw    : float32 mono, volts at the speaker node (MAME: 1.0V = full scale)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const double PI = 3.14159265358979323846;
static double rk(double k) { return k * 1e3; }
static double cu(double u) { return u * 1e-6; }

// ---------------------------------------------------------------- values
static const double TTL_OUT = 4.0;
static const double R15 = rk(100), R16 = rk(220), R17 = rk(470), R18 = rk(1000), R19 = rk(330);
static const double R20 = rk(15), R21 = rk(100), R22 = rk(100), R23 = rk(470), R24 = rk(10);
static const double R25 = rk(100), R26 = rk(330), R27 = rk(10), R28 = rk(100), R29 = rk(220);
static const double R30 = rk(10), R31 = rk(47), R32 = rk(47), R33 = rk(10), R34 = rk(5.1);
static const double R35 = rk(150), R36 = rk(22), R37 = rk(470), R38 = rk(33), R39 = rk(22);
static const double R40 = rk(2.2) * 0.6, R41 = rk(100), R43 = rk(2.2), R44 = rk(10), R45 = rk(22);
static const double R46 = rk(10), R47 = rk(2.2), R48 = rk(2.2), R49 = rk(10);
static const double R50 = rk(22), R51 = rk(33), R52 = rk(15), R91 = rk(10), MCRST_R34 = rk(15);
static const double C15 = cu(1), C17 = cu(0.01), C18 = cu(0.01), C19 = cu(0.01), C20 = cu(0.1);
static const double C21 = cu(2.2), C22 = cu(0.01), C23 = cu(0.01), C25 = cu(1), C26 = cu(0.01);
static const double C27 = cu(0.01), C28 = cu(47), C46 = cu(0.1);
static const double MASTER = 18432000.0;
static const double SOUND_CLOCK = MASTER / 6 / 2;     // 1.536 MHz
static const double RNG_RATE = MASTER / 3 * 2;        // 12.288 MHz

struct Ctx { double fs, dt; };
static double rc_charge_exp(const Ctx &c, double rc) { return 1.0 - exp(-c.dt / rc); }

// ---------------------------------------------------------------- 555 astable (DSD_555_ASTBL)
struct Astable {
	double r1, r2, c, v_pos, v_charge, v_out_high;
	int out_energy;   // 1 = DISC_555_OUT_ENERGY, 0 = DISC_555_OUT_SQW
	double t_rc_charge, t_rc_discharge, exp_charge, exp_discharge;
	double cap = 0, out = 0; int ff = 1;
	void init(const Ctx &cx) {
		t_rc_charge = (r1 + r2) * c; t_rc_discharge = r2 * c;
		exp_charge = rc_charge_exp(cx, t_rc_charge); exp_discharge = rc_charge_exp(cx, t_rc_discharge);
	}
	double step(const Ctx &cx, int enable, double ctrlv) {
		if (!enable) { out = 0; ff = 1; cap = 0; return out; }
		int count_f = 0, count_r = 0, flip_flop = ff, update_exponent = 0;
		double x_time = 0, v_cap = cap, v_cap_next = 0, exponent, dt;
		if (ctrlv < .25) return out;
		double threshold = ctrlv, trigger = ctrlv / 2.0;
		if (v_cap >= threshold) { flip_flop = 0; count_f++; }
		else if (v_cap <= trigger) { flip_flop = 1; count_r++; }
		dt = cx.dt;
		do {
			if (flip_flop) {
				exponent = update_exponent ? 1.0 - exp(-dt / t_rc_charge) : exp_charge;
				v_cap_next = v_cap + ((v_charge - v_cap) * exponent);
				dt = 0;
				if (v_cap_next >= threshold) {
					dt = t_rc_charge * log(1.0 / (1.0 - ((v_cap_next - threshold) / (v_charge - v_cap))));
					x_time = dt; v_cap_next = threshold; flip_flop = 0; count_f++; update_exponent = 1;
				}
			} else {
				exponent = update_exponent ? 1.0 - exp(-dt / t_rc_discharge) : exp_discharge;
				v_cap_next = v_cap - (v_cap * exponent);
				dt = 0;
				if (v_cap_next <= trigger) {
					if (v_cap_next < trigger)
						dt = t_rc_discharge * log(1.0 / (1.0 - ((trigger - v_cap_next) / v_cap)));
					x_time = dt; v_cap_next = trigger; flip_flop = 1; count_r++; update_exponent = 1;
				}
			}
			v_cap = v_cap_next;
		} while (dt);
		cap = v_cap;
		x_time = x_time / cx.dt;
		if (out_energy) {
			if (x_time == 0) x_time = 1.0;
			out = v_out_high * (flip_flop ? x_time : (1.0 - x_time));
		} else {
			if (count_f + count_r >= 2) out = ff ? 0 : v_out_high;
			else out = flip_flop * v_out_high;
		}
		ff = flip_flop;
		return out;
	}
};

// ---------------------------------------------------------------- the board
struct Board {
	Ctx cx;
	int mooncrst = 0;   // 1 = mooncrst_discrete final mix
	// latches
	int bg_dac = 0, fs[3] = {0, 0, 0}, hit = 0, fire = 0, vol1 = 0, vol2 = 0, pitch = 0;

	// NODE_150 LFSR noise
	double lfsr_t_clock, lfsr_t_left = 0; int lfsr_reg = 0, lfsr_out = 0;
	// NODE_151 squarewfix (2V)
	double sq_step, sq_t_left, sq_t_on, sq_t_off; int sq_ff = 1;
	// NODE_152 DFF
	int dff_last_clk = 0, dff_q = 0;
	// NODE_100 DAC
	double dac_v[16];
	// NODE_105 555 CC
	double cc_cap = 0; int cc_ff = 1;
	// NODE_115..117
	Astable vco[3];
	// NODE_120 mixer cF
	double bg_exp, bg_vcap = 0;
	// NODE_132 note
	double note_t_clock, note_t_left = 0; int note_c1 = 0, note_c2 = 0;
	// NODE_155 rcdisc5, NODE_157 bandpass
	double hit_exp, hit_vcap = 0;
	double bp_a1, bp_a2, bp_b0, bp_b1, bp_b2, bp_x1 = 0, bp_x2 = 0, bp_y1 = 0, bp_y2 = 0;
	double bp_rtotal, bp_vref, bp_vp, bp_vn;
	// fire
	double f173_exp, f173 = 0; Astable fvco; double f182_exp, f182_vcap = 0;
	// final mixer
	double mix_exp_c26, mix_vcap_c26 = 0, mix_exp_amp, mix_vcap_amp = 0;

	// debug taps
	double t_cv = 0, t_bg = 0, t_pre = 0, t_hit = 0, t_fire = 0, t_lfo = 0;

	void init(double fs_) {
		cx.fs = fs_; cx.dt = 1.0 / fs_;
		// LFSR
		lfsr_t_clock = 1.0 / (RNG_RATE / 100);
		lfsr_reg = 0;
		{ int fb0 = (lfsr_reg >> 4) & 1, fb1 = (lfsr_reg >> 16) & 1; int fb = fb0 ^ fb1 ^ 1; lfsr_reg |= fb << 17; lfsr_out = 0; }
		// squarewfix: freq 60*264/2, duty 50, phase 0
		sq_step = cx.dt; sq_t_off = 1.0 / (60 * 264 / 2); sq_t_left = 0; sq_t_on = sq_t_off * 0.5; sq_t_off -= sq_t_on;
		sq_t_left = -sq_t_left;
		while (sq_t_left <= 0) { sq_ff = sq_ff ? 0 : 1; sq_t_left += sq_ff ? sq_t_on : sq_t_off; }
		// DAC R1 ladder (galaxian_bck_dac)
		{
			double r[4] = {R18, R17, R16, R15};
			double rt = 0; for (int b = 0; b < 4; b++) rt += 1.0 / r[b];
			rt += 1.0 / R20; rt += 1.0 / R19; rt = 1.0 / rt;
			for (int i = 0; i < 16; i++) {
				double it = 4.4 / R20;
				for (int b = 0; b < 4; b++) if ((i >> b) & 1) it += TTL_OUT / r[b];
				dac_v[i] = it * rt;
			}
		}
		// 555 VCOs
		const double vr1[3] = {R22, R25, R28}, vr2[3] = {R23, R26, R29}, vc[3] = {C17, C18, C19};
		for (int i = 0; i < 3; i++) {
			vco[i].r1 = vr1[i]; vco[i].r2 = vr2[i]; vco[i].c = vc[i];
			vco[i].v_pos = 5; vco[i].v_charge = 5; vco[i].v_out_high = 5.0 - 0.5; vco[i].out_energy = 1;
			vco[i].init(cx);
		}
		bg_exp = rc_charge_exp(cx, (1.0 / (1.0 / R24 + 1.0 / R27 + 1.0 / R30)) * C20);
		// note
		note_t_clock = 1.0 / SOUND_CLOCK; note_c1 = pitch; note_c2 = 0;
		// hit
		hit_exp = rc_charge_exp(cx, (R35 + R36) * C21);
		{
			bp_vref = 5.0 * R39 / (R38 + R39); bp_vp = 5 - 1.5; bp_vn = 0;
			double rt = 1.0 / (1.0 / R35 + 1.0 / R36); bp_rtotal = rt;
			double rF = R37, c1 = C22, c2 = C23;
			double fc = 1.0 / (2 * PI * sqrt(rt * rF * c1 * c2));
			double d = (c1 + c2) / sqrt(rF / rt * c1 * c2);
			double gain = -rF / rt * c2 / (c1 + c2);
			double two_over_T = 2 * cx.fs, tot2 = two_over_T * two_over_T;
			double wc = cx.fs * 2.0 * tan(PI * fc / cx.fs), wc2 = wc * wc;
			double den = tot2 + d * wc * two_over_T + wc2;
			bp_a1 = 2.0 * (-tot2 + wc2) / den;
			bp_a2 = (tot2 - d * wc * two_over_T + wc2) / den;
			bp_b0 = d * wc * two_over_T / den; bp_b1 = 0; bp_b2 = -bp_b0;
			bp_b0 *= gain; bp_b1 *= gain; bp_b2 *= gain;
		}
		// fire
		f173_exp = rc_charge_exp(cx, R47 * C28);
		fvco.r1 = R44; fvco.r2 = R45; fvco.c = C27; fvco.v_pos = 5; fvco.v_charge = 5; fvco.v_out_high = 1.0;
		fvco.out_energy = 0; fvco.init(cx);
		f182_exp = rc_charge_exp(cx, R41 * C25);
		// final mixer
		mix_exp_c26 = rc_charge_exp(cx, (mooncrst ? R43 : 1.0 / (1.0 / R43 + 1.0 / R91)) * C26);
		mix_exp_amp = rc_charge_exp(cx, rk(100) * C46);
	}

	void write(char kind, int a, int d) {
		if (kind == 'L') { bg_dac = (bg_dac & ~(1 << a)) | ((d & 1) << a); }
		else if (kind == 'P') { pitch = d & 0xff; }
		else {
			d &= 1;
			switch (a) {
			case 0: case 1: case 2: fs[a] = d; break;
			case 3: hit = d; break;
			case 5: fire = d; break;
			case 6: vol1 = d; break;
			case 7: vol2 = d; break;
			}
		}
	}

	double step() {
		// ---------------- NODE_150 LFSR
		{
			double cycles = (lfsr_t_left + cx.dt) / lfsr_t_clock; int inc = (int)cycles;
			lfsr_t_left = (cycles - inc) * lfsr_t_clock;
			int fb = 0;
			for (int k = 0; k < inc; k++) {
				fb = (lfsr_reg >> 17) & 1;
				lfsr_reg = ((lfsr_reg << 1) & 0x1ffff) | fb;
				int fb0 = (lfsr_reg >> 4) & 1, fb1 = (lfsr_reg >> 16) & 1;
				fb = fb0 ^ fb1 ^ 1;
				lfsr_reg = (lfsr_reg & 0x1ffff) | (fb << 17);
			}
			if (inc > 0) lfsr_out = fb;
		}
		// ---------------- NODE_151 squarewfix
		sq_t_left -= sq_step;
		while (sq_t_left <= 0) { sq_ff = sq_ff ? 0 : 1; sq_t_left += sq_ff ? sq_t_on : sq_t_off; }
		int n151 = sq_ff;
		// ---------------- NODE_152 DFF
		if (!dff_last_clk && n151) dff_q = lfsr_out;
		dff_last_clk = n151;
		int noise = dff_q;

		// ---------------- background: NODE_100 DAC, NODE_105 555 CC
		double vin = dac_v[bg_dac];
		{
			const double th = 5.0 * 2 / 3, tr = 5.0 / 3, C = C15;
			double vlim = vin + 0.7;
			double i = (5.0 - vlim) / R21; if (i < 0) i = 0;
			double dt = cx.dt, v = cc_cap, vn;
			int ff = cc_ff;
			do {
				if (ff) {
					vn = v + i * dt / C; if (vn > vlim) vn = vlim; dt = 0;
					if (vn >= th) { dt = C * (vn - th) / i; vn = th; ff = 0; }
				} else { vn = tr; ff = 1; }  // immediate discharge, dt unchanged
				v = vn;
			} while (dt);
			cc_cap = v; cc_ff = ff;
		}
		t_lfo = cc_cap;
		// NODE_110 multadd, NODE_111 clamp
		double cv = cc_cap * (R33 / (1.0 / (1.0 / R31 + 1.0 / R32 + 1.0 / R33))) + (-5.0 * R33 / R31);
		if (cv < 0) cv = 0; if (cv > 5) cv = 5;
		t_cv = cv;
		// NODE_115..117
		double vo[3];
		for (int k = 0; k < 3; k++) vo[k] = vco[k].step(cx, fs[k], cv);
		// NODE_120 mixer with cF
		{
			double v = (vo[0] + vo[1] + vo[2]) / 3.0;
			bg_vcap += (v - bg_vcap) * bg_exp;
		}
		t_bg = bg_vcap;

		// ---------------- NODE_132 note, NODE_133 bits
		{
			double cycles = (note_t_left + cx.dt) / note_t_clock; int inc = (int)cycles;
			note_t_left = (cycles - inc) * note_t_clock;
			if (pitch != 255)
				for (int k = 0; k < inc; k++) {
					note_c1++;
					if (note_c1 > 255) { note_c1 = pitch; note_c2++; if (note_c2 > 15) note_c2 = 0; }
				}
		}
		double qa = (note_c2 & 1) ? TTL_OUT : 0, qc = (note_c2 & 4) ? TTL_OUT : 0, qd = (note_c2 & 8) ? TTL_OUT : 0;

		// ---------------- HIT: NODE_155 rcdisc5, NODE_157 bandpass
		double n155;
		{
			double u = hit * TTL_OUT - 0.7; if (u < 0) u = 0;
			double diff = u - hit_vcap;
			if (noise) { if (diff < 0) diff *= hit_exp; hit_vcap += diff; n155 = hit_vcap; }
			else { if (diff > 0) hit_vcap = u; n155 = 0; }
		}
		double n157;
		{
			double i = (n155 - bp_vref) / R35 + (0.0 - bp_vref) / R36;
			double v = i * bp_rtotal;
			double vo_ = -bp_a1 * bp_y1 - bp_a2 * bp_y2 + bp_b0 * v + bp_b1 * bp_x1 + bp_b2 * bp_x2 + bp_vref;
			bp_x2 = bp_x1; bp_x1 = v; bp_y2 = bp_y1;
			if (vo_ > bp_vp) vo_ = bp_vp; if (vo_ < bp_vn) vo_ = bp_vn;
			bp_y1 = vo_ - bp_vref;
			n157 = vo_;
		}
		t_hit = n157;

		// ---------------- FIRE
		double n171 = TTL_OUT * fire, n172 = TTL_OUT * (fire ? 0 : 1);
		f173 += (n172 - f173) * f173_exp;
		double n178 = (1.0 / (1.0 / R46 + 1.0 / R48)) * (noise * TTL_OUT / R46 + f173 / R48);
		double n181 = fvco.step(cx, 1, n178);
		double n182;
		{
			double u = n171 - 0.7; if (u < 0) u = 0;
			double diff = u - f182_vcap;
			if (n181 != 0) { if (diff < 0) diff *= f182_exp; f182_vcap += diff; n182 = f182_vcap; }
			else { if (diff > 0) f182_vcap = u; n182 = 0; }
		}
		t_fire = n182;

		// ---------------- NODE_279 pre mixer (resistor network, VOL1/VOL2 switch resistors in/out)
		double n279;
		{
			double g = 1.0 / R51 + 1.0 / R50 + 1.0 / R34;
			double i = qa / R51 + qc / R50 + t_bg / R34;
			if (vol1) { g += 1.0 / R49; i += qc / R49; }
			if (vol2) { g += 1.0 / R52; i += qd / R52; }
			n279 = i / g;
		}
		t_pre = n279;
		// ---------------- NODE_280 final mixer
		double out;
		if (mooncrst) {
			// mooncrst_mixer_desc: one resistor network, no R91
			double g = 1.0 / R51 + 1.0 / R50 + 1.0 / MCRST_R34 + 1.0 / R40 + 1.0 / R43;
			mix_vcap_c26 += (n182 - mix_vcap_c26) * mix_exp_c26;
			double vf = n182 - mix_vcap_c26;
			double i = qa / R51 + qc / R50 + t_bg / MCRST_R34 + n157 / R40 + vf / R43;
			if (vol1) { g += 1.0 / R49; i += qc / R49; }
			if (vol2) { g += 1.0 / R52; i += qd / R52; }
			double v = i / g;
			mix_vcap_amp += (v - mix_vcap_amp) * mix_exp_amp;
			out = v - mix_vcap_amp;
		} else {
			double g = 1.0 / R34 + 1.0 / R40 + 1.0 / R43 + 1.0 / R91;
			mix_vcap_c26 += (n182 - mix_vcap_c26) * mix_exp_c26;
			double vf = n182 - mix_vcap_c26;
			double v = (n279 / R34 + n157 / R40 + vf / R43) / g;
			mix_vcap_amp += (v - mix_vcap_amp) * mix_exp_amp;
			out = v - mix_vcap_amp;
		}
		return out;
	}
};

int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: %s writes.txt out.raw [fs] [seconds] [taps.raw|-] [mooncrst]\n", argv[0]); return 1; }
	double fs = argc > 3 ? atof(argv[3]) : 48000.0;
	double secs = argc > 4 ? atof(argv[4]) : 60.0;
	FILE *fi = fopen(argv[1], "r"); if (!fi) { perror(argv[1]); return 1; }
	struct W { double t; char k; int a, d; };
	std::vector<W> ws; W w;
	char kb[4];
	while (fscanf(fi, "%lf %3s %d %d", &w.t, kb, &w.a, &w.d) == 4) { w.k = kb[0]; ws.push_back(w); }
	fclose(fi);
	FILE *fo = fopen(argv[2], "wb");
	FILE *ft = (argc > 5 && strcmp(argv[5], "-")) ? fopen(argv[5], "wb") : nullptr;
	Board b; b.mooncrst = argc > 6 ? atoi(argv[6]) : 0; b.init(fs);
	size_t wi = 0; long n = (long)(secs * fs);
	std::vector<float> buf; buf.reserve(1 << 16);
	for (long s = 0; s < n; s++) {
		double t = s / fs;
		while (wi < ws.size() && ws[wi].t <= t) { b.write(ws[wi].k, ws[wi].a, ws[wi].d); wi++; }
		float o = (float)b.step();
		fwrite(&o, 4, 1, fo);
		if (ft) { float tp[6] = {(float)b.t_lfo, (float)b.t_cv, (float)b.t_bg, (float)b.t_pre, (float)b.t_hit, (float)b.t_fire}; fwrite(tp, 4, 6, ft); }
	}
	fclose(fo); if (ft) fclose(ft);
	return 0;
}
