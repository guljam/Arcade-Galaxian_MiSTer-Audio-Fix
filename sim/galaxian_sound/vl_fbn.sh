#!/bin/sh
# Build the Verilator simulation of rtl/galaxian_snd_fbn.vhd:
# VHDL -> Verilog with "ghdl --synth", then Verilator with vl_tb_fbn.cpp.
# Output: vl/vl_fbn.exe
set -e
export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH
R=../../rtl
mkdir -p vl/tmp
T=$(pwd -W 2>/dev/null || pwd)/vl/tmp
# the fire ROM is a signal (Quartus infers M10K from it); ghdl --synth would
# write its 441010 bit initial value as one Verilog literal, too wide for
# Verilator, so the copy reads the package constant directly (same contents)
sed 's/sh_q <= sh_rom(sh_idx);/sh_q <= SHOOT_ROM(sh_idx);/' $R/galaxian_snd_fbn.vhd > vl/galaxian_snd_fbn.vhd
ghdl --synth --std=08 --out=verilog $R/galaxian_snd_fbn_pkg.vhd vl/galaxian_snd_fbn.vhd \
	-e galaxian_snd_fbn > vl/galaxian_snd_fbn_synth.v 2> vl/synth.log
verilator --cc --exe -O3 -Wno-fatal --top-module GALAXIAN_SND_FBN \
	-Mdir vl/obj -o ../vl_fbn.exe vl/galaxian_snd_fbn_synth.v vl_tb_fbn.cpp > vl/verilator.log 2>&1
# msys make does not pass TMP/TEMP to the compiler unless given on its command line;
# OPT_GLOBAL=-O2: with the default -Os, GCC 16 leaves std::string symbols unresolved
make -C vl/obj -f VGALAXIAN_SND_FBN.mk -j8 TMP="$T" TEMP="$T" OPT_GLOBAL=-O2 >> vl/verilator.log 2>&1
