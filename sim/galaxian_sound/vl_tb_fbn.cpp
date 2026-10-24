// vl_tb_fbn.cpp - Verilator testbench for rtl/galaxian_snd_fbn.vhd (converted
// to Verilog with "ghdl --synth", see vl_fbn.sh). Same stimulus and timing as
// tb_galaxian_snd_fbn.vhd and fbn_fix.cpp, same output format.
//
// usage: vl_fbn <writes.txt> <out.txt> <msec> [mooncr]
// Watchdog: fails if no new sample strobe for 1000 clocks.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "VGALAXIAN_SND_FBN.h"

double sc_time_stamp() { return 0; }

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: vl_fbn <writes.txt> <out.txt> <msec>\n"); return 1; }
	FILE *wf = fopen(argv[1], "r"); if (!wf) { perror(argv[1]); return 1; }
	FILE *of = fopen(argv[2], "w"); if (!of) { perror(argv[2]); return 1; }
	int64_t msec = atoll(argv[3]);
	VGALAXIAN_SND_FBN *d = new VGALAXIAN_SND_FBN;
	d->I_MOONCR = argc > 4 ? atoi(argv[4]) : 0;
	d->I_CLK_12M = 0; d->I_RESET = 1; d->I_BD = 0; d->I_PITCH_WE = 0; d->I_FS = 0; d->I_HIT_WR = 0;
	d->I_FIRE = 0; d->I_VOL1 = 0; d->I_VOL2 = 0; d->I_DAC = 0; d->I_VSYNC = 0;
	d->eval();
	double t; char k; int a, v;
	bool have = fscanf(wf, "%lf %c %d %d", &t, &k, &a, &v) == 4;
	int idle = 0;
	for (int64_t n = 1; n <= msec * 12000; n++) {
		if (n % 250 == 0) fprintf(of, "%d\n", (int16_t)d->O_AUDIO);
		d->I_CLK_12M = 1; d->eval();
		d->I_CLK_12M = 0; d->eval();
		if (!d->I_RESET) {
			idle = d->O_SAMPLE ? 0 : idle + 1;
			if (idle >= 1000) { fprintf(stderr, "watchdog: sample sequencer stopped at clock %lld\n", (long long)n); return 2; }
		}
		// inputs for the next edge
		d->I_RESET = n < 4;
		d->I_PITCH_WE = 0; d->I_HIT_WR = 0;
		d->I_VSYNC = (n % 198000) >= 100 && (n % 198000) < 1100;
		while (have && (int64_t)(t * 12.0e6 + 0.5) <= n) {
			if (k == 'L') d->I_DAC = (d->I_DAC & ~(1 << a)) | ((v & 1) << a);
			else if (k == 'P') { d->I_BD = v; d->I_PITCH_WE = 1; }
			else switch (a) {
				case 0: case 1: case 2: d->I_FS = (d->I_FS & ~(1 << a)) | ((v & 1) << a); break;
				case 3: if (v & 1) d->I_HIT_WR = 1; break;
				case 5: d->I_FIRE = v & 1; break;
				case 6: d->I_VOL1 = v & 1; break;
				case 7: d->I_VOL2 = v & 1; break;
			}
			have = fscanf(wf, "%lf %c %d %d", &t, &k, &a, &v) == 4;
		}
		d->eval();
	}
	fclose(of); fclose(wf);
	delete d;
	return 0;
}
