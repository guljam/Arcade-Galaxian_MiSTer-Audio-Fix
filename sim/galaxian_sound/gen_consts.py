#!/usr/bin/env python3
"""Generate fixed-point constants for rtl/galaxian_snd.vhd.

All analog voltages in the VHDL are signed 40-bit numbers with 32 fraction
bits (1.0 V = 2**32).  Coefficients are stored as (k_int, S) pairs where
k_int = round(k * 2**S) fits a signed 27-bit DSP operand; the VHDL computes
x * k as ((x >> 9) * k_int) >> (S - 9).

Component values and formulas mirror MAME's galaxian_a.cpp (galaxian_discrete
and mooncrst_discrete).  Run this script and paste its output into the
constants section of rtl/galaxian_snd.vhd when anything here changes.
"""
import math

FCLK = 12_000_000.0
FS = FCLK / 16            # engine step rate (750 kHz)
DT = 1.0 / FS
F = 32                    # fraction bits of voltages

def k_(x): return x * 1e3
def u_(x): return x * 1e-6

R15, R16, R17, R18, R19 = k_(100), k_(220), k_(470), k_(1000), k_(330)
R20, R21, R22, R23 = k_(15), k_(100), k_(100), k_(470)
R25, R26, R28, R29 = k_(100), k_(330), k_(100), k_(220)
R24 = R27 = R30 = k_(10)
R31, R32, R33, R34, MCRST_R34 = k_(47), k_(47), k_(10), k_(5.1), k_(15)
R35, R36, R37, R38, R39 = k_(150), k_(22), k_(470), k_(33), k_(22)
R40, R41, R43, R44, R45 = k_(2.2) * 0.6, k_(100), k_(2.2), k_(10), k_(22)
R46, R47, R48, R49 = k_(10), k_(2.2), k_(2.2), k_(10)
R50, R51, R52, R91 = k_(22), k_(33), k_(15), k_(10)
C15, C17, C18, C19, C20 = u_(1), u_(0.01), u_(0.01), u_(0.01), u_(0.1)
C21, C22, C23, C25, C26 = u_(2.2), u_(0.01), u_(0.01), u_(1), u_(0.01)
C27, C28, C46 = u_(0.01), u_(47), u_(0.1)
TTL = 4.0

def par(*r): return 1.0 / sum(1.0 / x for x in r)
def kexp(rc): return 1.0 - math.exp(-DT / rc)
def v(x): return int(round(x * 2**F))

out = []
def coef(name, k):
    """emit k as (k_int, shift); k_int in signed 27 bits with max precision"""
    if k == 0:
        out.append(f"\tconstant {name}_K : integer := 0;\n\tconstant {name}_S : integer := 32;")
        return
    s = 0
    while abs(k) * 2**(s + 1) < 2**26 - 1:
        s += 1
    ki = int(round(k * 2**s))
    assert abs(ki) < 2**26 and s >= 9, (name, k, s)
    out.append(f"\tconstant {name}_K : integer := {ki};\n\tconstant {name}_S : integer := {s};  -- {k:.9g}")

def volt(name, x):
    h = v(x) & (2**40 - 1)
    out.append(f"\tconstant {name} : signed(39 downto 0) := x\"{h:010X}\";  -- {x:.9g} V")

# ---------------------------------------------------------------- background LFO (555 CC, DAC R1)
rl = [R18, R17, R16, R15]
rt = par(*rl, R20, R19)
dac = []
for i in range(16):
    it = 4.4 / R20 + sum(TTL / rl[b] for b in range(4) if (i >> b) & 1)
    dac.append(it * rt)
slopes = []
for i in range(16):
    cur = max(0.0, (5.0 - (dac[i] + 0.7)) / R21)
    slopes.append(cur * DT / C15)
out.append("\t-- 555 CC LFO: cap voltage increment per step for each DAC value (V * 2^32)")
out.append("\ttype lfo_tab_t is array(0 to 15) of integer;")
out.append("\tconstant LFO_SLOPE : lfo_tab_t := (" + ", ".join(str(v(s)) for s in slopes) + ");")
out.append("\t-- LFO period per DAC value (s): " + ", ".join(f"{(10/3-5/3)/(s/DT):.3f}" for s in slopes))
volt("LFO_TH", 10.0 / 3)
volt("LFO_SPAN", 10.0 / 3 - 5.0 / 3)
# multadd + clamp
coef("CV_GAIN", R33 / par(R31, R32, R33))
volt("CV_OFS", -5.0 * R33 / R31)
# 555 astables
def coef_pair(name, kc, kd):
    """charge/discharge coefficients sharing one shift so one multiplier serves both"""
    s = 0
    while max(kc, kd) * 2**(s + 1) < 2**26 - 1:
        s += 1
    out.append(f"\tconstant {name}_KC : integer := {int(round(kc * 2**s))};  -- {kc:.9g}")
    out.append(f"\tconstant {name}_KD : integer := {int(round(kd * 2**s))};  -- {kd:.9g}")
    out.append(f"\tconstant {name}_S  : integer := {s};")
for n, (r1, r2, c) in enumerate([(R22, R23, C17), (R25, R26, C18), (R28, R29, C19)], 1):
    coef_pair(f"VCO{n}", kexp((r1 + r2) * c), kexp(r2 * c))
coef_pair("FVCO", kexp((R44 + R45) * C27), kexp(R45 * C27))
volt("V_5", 5.0)
volt("V_025", 0.25)
volt("VCO_HIGH_X1_5", 4.5 / 3)    # one 555 high, through the 3 resistor mixer
coef("BG_K", kexp(par(R24, R27, R30) * C20))

# ---------------------------------------------------------------- hit
coef("HIT_K", kexp((R35 + R36) * C21))
volt("HIT_U", TTL - 0.7)
rtb = par(R35, R36)
rF, c1, c2 = R37, C22, C23
fc = 1.0 / (2 * math.pi * math.sqrt(rtb * rF * c1 * c2))
d = (c1 + c2) / math.sqrt(rF / rtb * c1 * c2)
gain = -rF / rtb * c2 / (c1 + c2)
tot = 2 * FS
wc = FS * 2.0 * math.tan(math.pi * fc / FS)
den = tot * tot + d * wc * tot + wc * wc
a1 = 2.0 * (-tot * tot + wc * wc) / den
a2 = (tot * tot - d * wc * tot + wc * wc) / den
b0 = d * wc * tot / den * gain
out.append(f"\t-- hit bandpass: fc={fc:.2f}Hz d={d:.4f} gain={gain:.3f} a1={a1:.12f} a2={a2:.12f}")
coef("BP_C1", -a1 - 2.0)
coef("BP_C2", 1.0 - a2)
coef("BP_B0G", b0 * (rtb / R35))      # input x = n155 * rt/R35 - 2.0 (DC cancels in x - x2)
vref = 5.0 * R39 / (R38 + R39)
volt("BP_VREF", vref)
volt("BP_YMAX", 5.0 - 1.5 - vref)
volt("BP_YMIN", 0.0 - vref)

# ---------------------------------------------------------------- fire
coef("F173_K", kexp(R47 * C28))
volt("V_4", TTL)
volt("F_NOISE_CV", TTL * R48 / (R46 + R48))
coef("F_CV_G", R46 / (R46 + R48))
coef("F182_K", kexp(R41 * C25))
volt("V_1", 1.0)

# ---------------------------------------------------------------- mixers
# returns per (vol1, vol2): pitch weights per energy count (0..16 per 16 clocks -> 0..4 V),
# bg weight, hit weight, fire weight
def galaxian_mix(vol1, vol2):
    g1 = 1 / R51 + 1 / R50 + 1 / R34 + (1 / R49 if vol1 else 0) + (1 / R52 if vol2 else 0)
    wa, wc_, wd, wbg = (1 / R51) / g1, (1 / R50 + (1 / R49 if vol1 else 0)) / g1, ((1 / R52) if vol2 else 0) / g1, (1 / R34) / g1
    g2 = 1 / R34 + 1 / R40 + 1 / R43 + 1 / R91
    w279, whit, wf = (1 / R34) / g2, (1 / R40) / g2, (1 / R43) / g2
    return wa * w279, wc_ * w279, wd * w279, wbg * w279, whit, wf

def mooncrst_mix(vol1, vol2):
    g = 1 / R51 + 1 / R50 + 1 / MCRST_R34 + 1 / R40 + 1 / R43 + (1 / R49 if vol1 else 0) + (1 / R52 if vol2 else 0)
    return ((1 / R51) / g, (1 / R50 + (1 / R49 if vol1 else 0)) / g, ((1 / R52) if vol2 else 0) / g,
            (1 / MCRST_R34) / g, (1 / R40) / g, (1 / R43) / g)

out.append("\ttype mix_tab_t is array(0 to 7) of integer;")
out.append("\t-- mixer weights, index = mooncrst*4 + vol2*2 + vol1")
out.append("\t--   PW_* : pitch bit weight per 12MHz clock high (V * 2^32), out of 16 clocks per step")
for nm, idx in (("PW_A", 0), ("PW_C", 1), ("PW_D", 2)):
    vals = []
    for mc in (0, 1):
        for v2 in (0, 1):
            for v1 in (0, 1):
                w = (mooncrst_mix if mc else galaxian_mix)(v1, v2)[idx]
                vals.append(str(v(w * TTL / 16)))
    out.append(f"\tconstant {nm} : mix_tab_t := (" + ", ".join(vals) + ");")
# The fire signal reaches the mixer through C26 (an 18 us high pass), so every
# 555 edge becomes a very short spike. MAME evaluates the circuit at 48 kHz and
# loses about 4.5 dB of that spike energy; this model runs at 750 kHz and keeps
# it, which sounds brighter than MAME. FIRE_TRIM matches MAME's fire level
# (2-12 kHz band energy, see docs/fixes-2026-10-22.md). 1.0 = raw circuit.
FIRE_TRIM = 0.6
for nm, idx in (("MW_BG", 3), ("MW_HIT", 4), ("MW_FIRE", 5)):
    trim = FIRE_TRIM if idx == 5 else 1.0
    ws = [(mooncrst_mix if i >= 4 else galaxian_mix)(i & 1, (i >> 1) & 1)[idx] * trim for i in range(8)]
    # one common shift per table so the VHDL shift is constant
    s = 0
    while max(ws) * 2**(s + 1) < 2**26 - 1: s += 1
    out.append(f"\tconstant {nm}_K : mix_tab_t := (" + ", ".join(str(int(round(w * 2**s))) for w in ws) + ");")
    out.append(f"\tconstant {nm}_S : integer := {s};")
coef("C26_K_GAL", kexp(par(R43, R91) * C26))
coef("C26_K_MC", kexp(R43 * C26))
coef("AMP_K", kexp(k_(100) * C46))
# quiescent DC at the C46 node (all sound off, bandpass at vref) to preset the output cap
volt("AMP_DC_GAL", galaxian_mix(0, 0)[4] * vref)
volt("AMP_DC_MC", mooncrst_mix(0, 0)[4] * vref)

print("-- Generated by sim/galaxian_sound/gen_consts.py - do not edit by hand.")
print(f"-- Engine step rate {FS:.0f} Hz (12 MHz / 16). Voltages: signed 40 bit, 1.0 V = 2**32.")
print("library ieee;\nuse ieee.std_logic_1164.all;\nuse ieee.numeric_std.all;\n")
print("package galaxian_snd_pkg is")
print("\n".join(out))
print("end package;")
