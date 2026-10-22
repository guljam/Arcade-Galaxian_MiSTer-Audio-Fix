// fbn_ref.cpp - FBNeo's Galaxian sound (src/burn/drv/galaxian/gal_sound.cpp,
// GAL_SOUND_HARDWARE_TYPE_GALAXIAN path) ported as-is to a standalone program,
// plus the generator of the constants package for rtl/galaxian_snd_fbn.vhd.
//
// FBNeo's code is a port of the pre-discrete MAME sound (MAME 0.36b11 -
// 0.133u1, src/mame/audio/galaxian.c) with a filtered explosion added in 2025.
// The render functions below keep FBNeo's float/integer operations in the
// same order. FBNeo runs the Galaxian sound with one sample per CPU slice
// (nInterleave = nBurnSoundLen), so every function here renders one sample.
//
// usage: fbn_ref ref <writes.txt> <out.raw> <seconds>      int16 mono, 48 kHz
//        fbn_ref aligned <writes.txt> <out.raw> <seconds>  same, RTL testbench timing
//        fbn_ref aligned <writes.txt> <out.raw> <seconds>  same, RTL testbench timing
//        fbn_ref pkg > ../../rtl/galaxian_snd_fbn_pkg.vhd

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

typedef int16_t INT16; typedef int32_t INT32; typedef uint32_t UINT32; typedef uint8_t UINT8;

static const INT32 nBurnSoundRate = 48000;
static const INT32 FRAME_LEN = 792;   // 48000 / 60.606 Hz, = FBNeo's nBurnSoundLen

#define BURN_SND_CLIP(A) ((A) < -0x8000 ? -0x8000 : (A) > 0x7fff ? 0x7fff : (A))

// ---- FBNeo src/burn/snd/biquad.h (FILT_LOWPASS / FILT_LOWSHELF only) ----
enum { FILT_LOWPASS = 1, FILT_LOWSHELF = 2 };
struct BIQ {
	double a0, a1, a2, b1, b2, q, z1, z2, frequency, samplerate, output;
	float filter(float input) {
		output = input * a0 + z1;
		z1 = input * a1 + z2 - b1 * output;
		z2 = input * a2 - b2 * output;
		return (float)output;
	}
	void init(INT32 type, INT32 sample_rate, INT32 freqhz, double q_, double gain) {
		samplerate = sample_rate; frequency = freqhz; q = q_;
		z1 = 0.0; z2 = 0.0; output = 0.0;
		double k = tan(3.14159265358979323846 * frequency / samplerate);
		double norm = 1 / (1 + k / q + k * k);
		double v = pow(10, fabs(gain) / 20);
		if (type == FILT_LOWPASS) {
			a0 = k * k * norm; a1 = 2 * a0; a2 = a0;
			b1 = 2 * (k * k - 1) * norm; b2 = (1 - k / q + k * k) * norm;
		} else {   // FILT_LOWSHELF, gain >= 0
			norm = 1 / (1 + sqrt(2.0) * k + k * k);
			a0 = (1 + sqrt(2*v) * k + v * k * k) * norm;
			a1 = 2 * (v * k * k - 1) * norm;
			a2 = (1 - sqrt(2*v) * k + v * k * k) * norm;
			b1 = 2 * (k * k - 1) * norm;
			b2 = (1 - sqrt(2.0) * k + k * k) * norm;
		}
	}
};

// ---- gal_sound.cpp constants ----
#define XTAL				18432000
#define RNG_RATE			(XTAL / 3)
#define NOISE_RATE			(XTAL / 3 / 192 / 2 / 2)
#define NOISE_LENGTH			(NOISE_RATE * 4)
#define NOISE_AMPLITUDE			(70 * 256)
#define SHOOT_VOLUME			0.50f
#define SHOOT_SEC			2
#define R41__ 				100000
#define R44__ 				10000
#define R45__ 				22000
#define R46__ 				10000
#define R47__ 				2200
#define R48__ 				2200
#define C25__ 				0.000001
#define C27__ 				0.00000001
#define C28__ 				0.000047
#define C29__ 				0.00000001
#define IC8L3_L 			0.2
#define IC8L3_H 			4.5
#define NOISE_L 			0.2
#define NOISE_H 			4.5
#define SHOOT_KEYON_TIME 		0.1
#define NE555_FM_ADJUST_RATE 		0.80
#define TOOTHSAW_LENGTH 		16
#define TOOTHSAW_AMPLITUDE		(64*256)
#define V(r0,r1) 			2 * TOOTHSAW_AMPLITUDE * (r0) / (r0 + r1) - TOOTHSAW_AMPLITUDE
#define STEPS 				16
#define LFO_VOLUME 			0.06f
#define MINFREQ 			(139 - 139 / 3)
#define MAXFREQ 			(139 + 139 / 3)

static INT16 *GalNoiseWave, *GalShootWave;
static UINT32 GalShootLength, GalShootRate;
static UINT8 GalLastPort2;
static UINT8 GalShootEnable;
static UINT8 GalNoiseEnable;
static INT32 GalNoiseVolume, GalNoiseHold;
static double GalShootWavePos, GalNoiseWavePos;
static INT32 GalPitch, GalVol, GalCounter, GalCountDown;
static double GalLfoWavePos[3];
static INT32 GalLfoVolume[3];
static double GalLfoFreq, GalLfoFreqFrameVar;
static INT32 GalLfoBit[4];
static double GalGain = 4.00;
static INT16 GalToneWave[4][TOOTHSAW_LENGTH];
static BIQ biquad_explode, biquad_explode2;
static UINT32 nCurrentFrame;

static const INT16 GalBackgroundWave[32] = {
   0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000,
   0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000,
  -0x4000,-0x4000,-0x4000,-0x4000,-0x4000,-0x4000,-0x4000,-0x4000,
  -0x4000,-0x4000,-0x4000,-0x4000,-0x4000,-0x4000,-0x4000,-0x4000,
};

static void GalSoundInit()
{
	INT32 CountDown, Generator, Bit1, Bit2;
	GalNoiseWave = (INT16*)calloc(NOISE_LENGTH + 1, sizeof(INT16));
	GalShootRate = 22050;
	GalShootLength = SHOOT_SEC * GalShootRate;
	GalShootWave = (INT16*)calloc(GalShootLength + 1, sizeof(INT16));

	Generator = 0;
	CountDown = NOISE_RATE / 2;
	for (INT32 i = 0; i < NOISE_LENGTH; i++) {
		CountDown -= RNG_RATE;
		while (CountDown < 0) {
			Generator <<= 1;
			Bit1 = (~Generator >> 17) & 1;
			Bit2 = (Generator >> 5) & 1;
			if (Bit1 ^ Bit2) Generator |= 1;
			CountDown += NOISE_RATE;
		}
		GalNoiseWave[i] = ((Generator >> 17) & 1) ? NOISE_AMPLITUDE : -NOISE_AMPLITUDE;
	}

	double v  = 5.0;
	double vK = (GalShootRate) ? exp(-1 / (R41__ * C25__) / GalShootRate) : 0;
	double IC8L3 = IC8L3_L;
	INT32 IC8Lcnt = (INT32)(SHOOT_KEYON_TIME * GalShootRate);
	double c28v = IC8L3_H - (IC8L3_H - (NOISE_H + NOISE_L) / 2) / (R46__ + R47__ + R48__) * R47__;
	double c28K = (GalShootRate) ? exp(-1 / (22000 * 0.000047 ) / GalShootRate) : 0;
	double c29v  = IC8L3_H - (IC8L3_H - (NOISE_H + NOISE_L) / 2) / (R46__ + R47__ + R48__) * (R47__ + R48__);
	double c29K1 = (GalShootRate) ? exp(-1 / (22000  * 0.00000001 ) / GalShootRate) : 0;
	double c29K2 = (GalShootRate) ? exp(-1 / (100000 * 0.00000001 ) / GalShootRate) : 0;
	double ne555cnt = 0;
	double ne555step = (GalShootRate) ? ((1.44 / ((R44__ + R45__ * 2) * C27__)) / GalShootRate) : 0;
	double ne555duty = (double)(R44__ + R45__) / (R44__ + R45__ * 2);
	double ne555sr;
	double ncnt  = 0.0;
	double nstep = (GalShootRate) ? ((double)NOISE_RATE / GalShootRate) : 0;
	double noise_sh2;

	for (UINT32 i = 0; i < GalShootLength; i++) {
		noise_sh2 = GalNoiseWave[(INT32)ncnt % NOISE_LENGTH] == NOISE_AMPLITUDE ? NOISE_H : NOISE_L;
		ncnt += nstep;
		ne555sr = c29v * NE555_FM_ADJUST_RATE / (5.0 * 2 / 3);
		ne555cnt += ne555step;
		if (ne555cnt >= ne555sr) ne555cnt -= ne555sr;
		if (ne555cnt < ne555sr * ne555duty) {
			GalShootWave[i] = (INT16)(v / 5 * 0x7fff);
			if(IC8L3 == IC8L3_H) v *= vK;
		} else {
			GalShootWave[i] = 0;
		}
		c28v += (IC8L3 - c28v) - (IC8L3 - c28v) * c28K;
		c28v += (c29v - c28v) - (c29v - c28v) * c28K;
		c29v += (c28v - c29v) - (c28v - c29v) * c29K1;
		c29v += (noise_sh2 - c29v) - (noise_sh2 - c29v) * c29K2;
		if(IC8L3 == IC8L3_L && --IC8Lcnt == 0) IC8L3 = IC8L3_H;
	}

	memset(GalToneWave, 0, sizeof(GalToneWave));
	for (UINT32 i = 0; i < TOOTHSAW_LENGTH; i++ ) {
		double r0a = 1.0 / 1e12, r1a = 1.0 / 1e12;
		double r0b = 1.0 / 1e12, r1b = 1.0 / 1e12;
		if (i & 1) { r1a += 1.0 / 33000; r1b += 1.0 / 33000; } else { r0a += 1.0 / 33000; r0b += 1.0 / 33000; }
		if (i & 4) { r1a += 1.0 / 22000; r1b += 1.0 / 22000; } else { r0a += 1.0 / 22000; r0b += 1.0 / 22000; }
		GalToneWave[0][i] = (INT16)(V(1.0 / r0a, 1.0 / r1a));
		if (i & 4) { r1a += 1.0 / 10000; } else { r0a += 1.0 / 10000; }
		GalToneWave[1][i] = (INT16)(V(1.0 / r0a, 1.0 / r1a));
		if (i & 8) { r1b += 1.0 / 15000; } else { r0b += 1.0 / 15000; }
		GalToneWave[2][i] = (INT16)(V(1.0 / r0b, 1.0 / r1b));
		if (i & 4) { r0b += 1.0 / 10000; } else { r1b += 1.0 / 10000; }
		GalToneWave[3][i] = (INT16)(V(1.0 / r0b, 1.0 / r1b));
	}

	// GalSoundReset
	GalLastPort2 = 0; GalShootEnable = 0; GalNoiseEnable = 0; GalNoiseVolume = 0; GalNoiseHold = 0;
	GalShootWavePos = 0; GalNoiseWavePos = 0;
	GalLfoWavePos[0] = GalLfoWavePos[1] = GalLfoWavePos[2] = 0;
	GalPitch = 0xff; GalVol = 0; GalCounter = 0; GalCountDown = 0;
	GalLfoVolume[0] = GalLfoVolume[1] = GalLfoVolume[2] = 0;
	GalLfoFreq = MAXFREQ; GalLfoFreqFrameVar = 0;
	GalLfoBit[0] = GalLfoBit[1] = GalLfoBit[2] = GalLfoBit[3] = 0;

	biquad_explode.init(FILT_LOWSHELF, nBurnSoundRate, 150.00, 1.00, 10.0);
	biquad_explode2.init(FILT_LOWPASS, nBurnSoundRate, 550.00, 1.00, 10.0);
}

// FBN_EXACT: keep the noise / fire wave positions on exact multiples of
// 1/6 and 1/320 like the RTL. FBNeo adds the steps in double, so floor()
// sometimes picks the previous wave sample (6.5% of noise samples).
static bool exact_pos = false;

// ---- render, one mono sample each (FBNeo writes L and R the same) ----
static void GalRenderShootSample(INT16 *s)
{
	double Addr = GalShootWavePos;
	double Step = (double)GalShootRate / nBurnSoundRate;
	INT16 Sample = (INT16)(GalShootWave[(INT32)Addr] * SHOOT_VOLUME);
	Sample >>= 4;
	INT32 n = BURN_SND_CLIP((INT32)(Sample * GalGain));
	*s = BURN_SND_CLIP(*s + n);
	Addr += Step;
	if (exact_pos) Addr = std::round(Addr * 320) / 320;
	GalShootWavePos = Addr;
	if (GalShootWavePos > GalShootLength) { GalShootWavePos = 0; GalShootEnable = 0; }
}

static void GalRenderNoiseSample(INT16 *s)
{
	double Addr = GalNoiseWavePos;
	double Step = (double)NOISE_RATE / nBurnSoundRate;
	INT16 Sample = (INT16)(GalNoiseWave[(INT32)Addr] * ((double)GalNoiseVolume / 100));
	Sample /= 10;
	Sample = biquad_explode2.filter(biquad_explode.filter(Sample) + Sample);
	INT32 n = BURN_SND_CLIP((INT32)(Sample * GalGain));
	*s = BURN_SND_CLIP(*s + n);
	Addr += Step;
	if (exact_pos) Addr = std::round(Addr * 6) / 6;
	GalNoiseWavePos = Addr;
	if (GalNoiseWavePos > NOISE_LENGTH) GalNoiseWavePos = 0;
}

static void GalRenderToneWave(INT16 *s)
{
	INT16 *w = GalToneWave[GalVol];
	if (GalPitch != 0xff) {
		INT32 mix = 0;
		for (INT32 j = 0; j < STEPS; j++) {
			if (GalCountDown >= (256 * nBurnSoundRate / 96000)) {
				GalCounter = (GalCounter + 1) % TOOTHSAW_LENGTH;
				GalCountDown = GalPitch * nBurnSoundRate / 96000;
			}
			GalCountDown++;
			mix += w[GalCounter % TOOTHSAW_LENGTH];
		}
		INT16 Sample = mix / STEPS;
		Sample >>= 4;
		*s = BURN_SND_CLIP((INT32)(Sample * GalGain));
	}
}

static void GalRenderLfoWaveSample(INT32 nLfoWave, INT16 *s)
{
	INT32 nshHZ = nLfoWave == 0 ? 470 : nLfoWave == 1 ? 330 : 220;
	double Addr = GalLfoWavePos[nLfoWave];
	double Step = (double)(sizeof(GalBackgroundWave) * GalLfoFreq * (100 + 2 * nshHZ) / (100 + 2 * 470)) / nBurnSoundRate;
	INT16 Sample = (INT16)(GalBackgroundWave[(INT32)Addr & 31] * (GalLfoVolume[nLfoWave] ? LFO_VOLUME : 0));
	Sample >>= 4;
	INT32 n = BURN_SND_CLIP((INT32)(Sample * GalGain));
	*s = BURN_SND_CLIP(*s + n);
	Addr += Step;
	GalLfoWavePos[nLfoWave] = Addr;
	if (GalLfoWavePos[nLfoWave] > 32) GalLfoWavePos[nLfoWave] = 0;
}

static int solo = -1;   // debug: 0 tone, 1 noise, 2 fire, 3 swarm (env FBN_SOLO)

static INT16 GalRenderSoundSample()
{
	INT16 s = 0, x;
	GalRenderToneWave(&s);
	if (solo >= 0 && solo != 0) s = 0;
	x = s; GalRenderNoiseSample(&s); if (solo >= 0 && solo != 1) s = x;
	x = s; if (GalShootEnable) GalRenderShootSample(&s); if (solo >= 0 && solo != 2) s = x;
	x = s;
	GalRenderLfoWaveSample(0, &s);
	GalRenderLfoWaveSample(1, &s);
	GalRenderLfoWaveSample(2, &s);
	if (solo >= 0 && solo != 3) s = x;
	return s;
}

// ---- write handlers ----
static void GalaxianSoundWrite(UINT32 Offset, UINT8 d)
{
	d &= 0x01;
	switch (Offset & 0x07) {
		case 0x00: case 0x01: case 0x02: GalLfoVolume[Offset] = d; return;
		case 0x03:
			if ((d & 1) && GalNoiseHold) { GalNoiseHold = 20; GalNoiseVolume = 100; return; }
			GalNoiseEnable = d & 1;
			if (GalNoiseEnable) { GalNoiseHold = 20; GalNoiseVolume = 100; GalNoiseWavePos = 0; }
			return;
		case 0x04: return;
		case 0x05:
			if (d & 1 && !(GalLastPort2 & 1)) { GalShootEnable = 1; GalShootWavePos = 0; }
			GalLastPort2 = d;
			return;
		case 0x06: case 0x07:
			GalVol = (GalVol & ~(1 << (Offset & 0x01))) | ((d & 1) << (Offset & 0x01));
			return;
	}
}

static double LfoFrameVar(const INT32 *bit)
{
	double r0, r1, rx = 100000.0;
	r0 = 1.0 / 330000;
	r1 = 1.0 / 1e12;
	if (bit[0]) r1 += 1.0 / 1000000; else r0 += 1.0 / 1000000;
	if (bit[1]) r1 += 1.0 / 470000;  else r0 += 1.0 / 470000;
	if (bit[2]) r1 += 1.0 / 220000;  else r0 += 1.0 / 220000;
	if (bit[3]) r1 += 1.0 / 100000;  else r0 += 1.0 / 100000;
	r0 = 1.0 / r0;
	r1 = 1.0 / r1;
	rx = rx + 2000000.0 * r0 / (r0 + r1);
	return (1000000 / (639 * rx / (MAXFREQ - MINFREQ))) * STEPS;
}

static void GalaxianLfoFreqWrite(UINT32 Offset, UINT8 d)
{
	if ((d & 0x01) == GalLfoBit[Offset]) return;
	GalLfoBit[Offset] = d & 0x01;
	GalLfoFreqFrameVar = LfoFrameVar(GalLfoBit);
}

static void GalaxianSoundUpdateTimers()
{
	if (GalNoiseHold) GalNoiseHold--;
	if ((nCurrentFrame % 0x3) == 0) {
		if (!GalNoiseHold && GalNoiseVolume > 0) {
			GalNoiseVolume -= (GalNoiseVolume / 10) + 1;
			if (GalNoiseVolume <= 0) { GalNoiseVolume = 0; GalNoiseEnable = 0; }
		}
	}
	if (GalLfoFreq > MINFREQ) GalLfoFreq -= GalLfoFreqFrameVar;
	else GalLfoFreq = MAXFREQ;
}

// ---- constants package for the RTL ----
static long long fx(double v, int frac) { return llround(v * std::ldexp(1.0, frac)); }

static void emit_pkg()
{
	GalSoundInit();
	printf("-- Generated by sim/galaxian_sound/fbn_ref.cpp (\"fbn_ref pkg\"), do not edit.\n");
	printf("-- Constants of FBNeo's Galaxian sound for rtl/galaxian_snd_fbn.vhd.\n");
	printf("library ieee;\n\tuse ieee.std_logic_1164.all;\n\tuse ieee.numeric_std.all;\n\n");
	printf("package galaxian_snd_fbn_pkg is\n");
	printf("\ttype int_array is array(natural range <>) of integer;\n\n");

	printf("\t-- tone wave tables [VOL][counter] (GalToneWave)\n");
	printf("\tconstant TONE_WAVE : int_array(0 to 63) := (");
	for (int v = 0; v < 4; v++) for (int i = 0; i < 16; i++) printf("%s%d", v || i ? ", " : "", GalToneWave[v][i]);
	printf(");\n\n");

	printf("\t-- noise sample magnitude per volume: (INT16)(17920 * vol/100) / 10\n");
	printf("\tconstant NOISE_AMP : int_array(0 to 100) := (");
	for (int v = 0; v <= 100; v++) { INT16 a = (INT16)(NOISE_AMPLITUDE * ((double)v / 100)); a /= 10; printf("%s%d", v ? ", " : "", a); }
	printf(");\n\n");

	// LFSR state after the first 768 shifts from 0 (state of noise sample 0)
	INT32 g = 0;
	for (int i = 0; i < 768; i++) { g <<= 1; if (((~g >> 17) & 1) ^ ((g >> 5) & 1)) g |= 1; }
	printf("\t-- noise generator state for sample 0 (768 shifts from 0)\n");
	printf("\tconstant NOISE_G0 : integer := %d;\n\n", g & 0x3ffff);

	// biquads, signal Q20, coefficients Q25
	const char *bn[2] = {"LS", "LP"};
	BIQ *bq[2] = {&biquad_explode, &biquad_explode2};
	printf("\t-- explosion biquads, coefficients Q25 (signals are Q20)\n");
	for (int k = 0; k < 2; k++)
		printf("\tconstant %s_A0 : integer := %lld;\n\tconstant %s_A1 : integer := %lld;\n\tconstant %s_A2 : integer := %lld;\n"
		       "\tconstant %s_B1 : integer := %lld;\n\tconstant %s_B2 : integer := %lld;\n",
		       bn[k], fx(bq[k]->a0, 25), bn[k], fx(bq[k]->a1, 25), bn[k], fx(bq[k]->a2, 25),
		       bn[k], fx(bq[k]->b1, 25), bn[k], fx(bq[k]->b2, 25));
	printf("\n");

	// LFO: frequency Q32 (in a 42 bit signed), per frame decrement per DAC value, step factors Q35
	printf("\t-- swarm LFO: frequency in Hz Q32, decrement per frame for each DAC value (bit 0 = 1M)\n");
	printf("\tconstant LFO_MIN : integer := %d;\n\tconstant LFO_MAX : integer := %d;\n", MINFREQ, MAXFREQ);
	printf("\ttype u44_array is array(natural range <>) of unsigned(43 downto 0);\n");
	printf("\tconstant LFO_FRAME_VAR : u44_array(0 to 15) := (\n");
	for (int d = 0; d < 16; d++) {
		INT32 b[4] = {d & 1, (d >> 1) & 1, (d >> 2) & 1, (d >> 3) & 1};
		printf("\t\tx\"%011llx\"%s\n", (unsigned long long)fx(LfoFrameVar(b), 32), d < 15 ? "," : "");
	}
	printf("\t);\n");
	printf("\t-- wave position step per sample = frequency * 64 * (100 + 2 * R) / 1040 / 48000, Q35\n");
	const int nsh[3] = {470, 330, 220};
	printf("\tconstant LFO_STEP_K : int_array(0 to 2) := (");
	for (int n = 0; n < 3; n++) printf("%s%lld", n ? ", " : "", fx(64.0 * (100 + 2 * nsh[n]) / (100 + 2 * 470) / nBurnSoundRate, 35));
	printf(");\n\n");

	printf("\t-- fire: (INT16)(GalShootWave[i] * 0.5f) >> 4, 22050 Hz, %u samples\n", GalShootLength + 1);
	printf("\tconstant SHOOT_LEN : integer := %u;\n", GalShootLength);
	printf("\ttype shoot_rom_t is array(0 to %u) of unsigned(9 downto 0);\n", GalShootLength);
	printf("\tconstant SHOOT_ROM : shoot_rom_t := (");
	for (UINT32 i = 0; i <= GalShootLength; i++) {
		INT16 s = (INT16)(GalShootWave[i] * SHOOT_VOLUME); s >>= 4;
		printf("%s%sto_unsigned(%d,10)", i ? "," : "", i % 8 ? " " : "\n\t\t", s);
	}
	printf("\n\t);\nend package;\n");
}

// ---- driver: replay a MAME sound register log ----
static void run_ref(const char *wfn, const char *ofn, double sec)
{
	GalSoundInit();
	FILE *f = fopen(wfn, "r"); if (!f) { perror(wfn); exit(1); }
	FILE *o = fopen(ofn, "wb"); if (!o) { perror(ofn); exit(1); }
	double t; char k; int a, d;
	bool have = fscanf(f, "%lf %c %d %d", &t, &k, &a, &d) == 4;
	long long n = (long long)(sec * nBurnSoundRate);
	for (long long i = 0; i < n; i++) {
		if (i % FRAME_LEN == 0) { GalaxianSoundUpdateTimers(); nCurrentFrame++; }
		while (have && t * nBurnSoundRate < i + 1) {
			if (k == 'L') GalaxianLfoFreqWrite(a, d);
			else if (k == 'S') GalaxianSoundWrite(a, d);
			else GalPitch = d;
			have = fscanf(f, "%lf %c %d %d", &t, &k, &a, &d) == 4;
		}
		INT16 s = GalRenderSoundSample();
		fwrite(&s, 2, 1, o);
	}
	fclose(o); fclose(f);
}

// Same, but with the timing of tb_galaxian_snd_fbn.vhd / the RTL: a write
// logged at time t is applied by the testbench at clock edge w = round(t*12e6)
// and the RTL takes it at the start of the first sample whose start edge
// (6 + 250 j) is after w (level inputs) or after w + 1 (hit / pitch strobes).
// Frame ticks are seen at edge 101 + 198000 m. Within a sample: level writes,
// frame tick (uses the current DAC), hit / pitch, render. Output sample k is
// the RTL's sample k - 1. The LFO decrement starts from the DAC value 0 (the
// testbench's initial value) instead of FBNeo's 0 before the first write.
static void run_aligned(const char *wfn, const char *ofn, double sec)
{
	GalSoundInit();
	GalLfoFreqFrameVar = LfoFrameVar(GalLfoBit);
	FILE *f = fopen(wfn, "r"); if (!f) { perror(wfn); exit(1); }
	FILE *o = fopen(ofn, "wb"); if (!o) { perror(ofn); exit(1); }
	double t; char k; int a, d;
	bool have = fscanf(f, "%lf %c %d %d", &t, &k, &a, &d) == 4;
	long long n = (long long)(sec * nBurnSoundRate);
	INT16 s = 0;
	fwrite(&s, 2, 1, o);
	long long next_frame_edge = 101;
	struct W { char k; int a, d; };
	std::vector<W> late;   // strobes waiting for the next sample
	for (long long j = 0; j + 1 < n; j++) {
		long long st0 = 6 + 250 * j;
		std::vector<W> strobes;
		for (auto &w : late) strobes.push_back(w);
		late.clear();
		while (have) {
			long long w = (long long)(t * 12.0e6 + 0.5); if (w < 1) w = 1;
			if (w + 1 > st0) break;
			if (k == 'L') GalaxianLfoFreqWrite(a, d);
			else if (k == 'P' || (k == 'S' && a == 3)) {
				if (k == 'S' && !(d & 1)) GalaxianSoundWrite(a, d);
				else if (w + 1 < st0) strobes.push_back({k, a, d}); else late.push_back({k, a, d});
			}
			else GalaxianSoundWrite(a, d);
			have = fscanf(f, "%lf %c %d %d", &t, &k, &a, &d) == 4;
		}
		if (next_frame_edge < st0) { GalaxianSoundUpdateTimers(); nCurrentFrame++; next_frame_edge += 198000; }
		for (auto &w : strobes) { if (w.k == 'P') GalPitch = w.d; else GalaxianSoundWrite(w.a, w.d); }
		s = GalRenderSoundSample();
		fwrite(&s, 2, 1, o);
	}
	fclose(o); fclose(f);
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "pkg")) { emit_pkg(); return 0; }
	if (getenv("FBN_SOLO")) solo = atoi(getenv("FBN_SOLO"));
	exact_pos = getenv("FBN_EXACT") != nullptr;
	if (argc >= 5 && !strcmp(argv[1], "ref")) { run_ref(argv[2], argv[3], atof(argv[4])); return 0; }
	if (argc >= 5 && !strcmp(argv[1], "aligned")) { run_aligned(argv[2], argv[3], atof(argv[4])); return 0; }
	fprintf(stderr, "usage: fbn_ref ref|aligned <writes.txt> <out.raw> <seconds> | fbn_ref pkg\n");
	return 1;
}
