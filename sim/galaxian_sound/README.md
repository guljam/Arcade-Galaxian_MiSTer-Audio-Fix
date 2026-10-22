# Galaxian sound verification

Tools used to build and check `rtl/galaxian_snd.vhd` against MAME.
Background and results: `docs/fixes-2026-10-22.md`.

Tools: MSYS2 mingw64 (`g++`, `ghdl` 6 with LLVM backend), Python 3 with numpy/scipy, MAME.

| file | purpose |
|---|---|
| `gen_consts.py` | computes all fixed-point constants -> `rtl/galaxian_snd_pkg.vhd` |
| `galsnd_ref.cpp` | double precision port of MAME's `galaxian_discrete` (golden model) |
| `galsnd_fix.cpp` | bit-exact C++ model of the VHDL, same timing as `tb_galaxian_snd` |
| `tb_galaxian_snd.vhd` | GHDL testbench of the sound block, replays a register log |
| `tb_core.vhd`, `sim_ram.vhd`, `build_core.sh` | GHDL testbench of the whole core with a ROM image |
| `mame_log.lua` | MAME script: autoplay + sound register log |
| `galaxian_writes.txt` | register log from 150 s of MAME Galaxian (57 s of play) |
| `stim_short.txt` | the log from 8.4-16.4 s compressed 20x (dense stimulus for GHDL) |

## Typical flow

```sh
export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH

# constants
python gen_consts.py > ../../rtl/galaxian_snd_pkg.vhd

# models
g++ -O2 -o galsnd_ref.exe galsnd_ref.cpp
g++ -O2 -o galsnd_fix.exe galsnd_fix.cpp

# RTL == C++ model (bit exact), ~2.5 min
mkdir -p work
ghdl -a --std=08 --workdir=work ../../rtl/galaxian_snd_pkg.vhd ../../rtl/galaxian_snd.vhd tb_galaxian_snd.vhd
ghdl -e --std=08 --workdir=work -O2 tb_galaxian_snd
timeout 600 ./tb_galaxian_snd.exe -gWRITES=stim_short.txt -gMSEC=400 -gOUTFILE=rtl_short.txt
./galsnd_fix.exe ../../rtl/galaxian_snd_pkg.vhd stim_short.txt fix_short.txt 400
cmp rtl_short.txt fix_short.txt

# long runs with the C++ model (int16 at 750 kHz), compare with the reference
./galsnd_fix.exe ../../rtl/galaxian_snd_pkg.vhd galaxian_writes.txt fix.txt 60000 0 fix.raw
./galsnd_ref.exe galaxian_writes.txt ref.raw 750000 60          # float32 volts
./galsnd_ref.exe galaxian_writes.txt ref48.raw 48000 60         # compare with MAME's wav

# MAME log + wav (GBASE=0xa000 for Moon Cresta hardware, e.g. moonqsr)
GLOG=writes.txt mame galaxian -autoboot_script mame_log.lua -wavwrite mame.wav \
    -nothrottle -video none -sound none -seconds_to_run 150

# whole core (ROM image = MRA rom index 0 parts concatenated), ~9 min per second of game time
sh build_core.sh
timeout 600 ./tb_core.exe -gROMFILE=galaxian_rom.bin -gMSEC=1000 --ieee-asserts=disable
```

Arguments: `galsnd_fix <pkg> <writes> <out.txt> <msec> [mooncr] [audio.raw]`,
`galsnd_ref <writes> <out.raw> [fs] [seconds] [taps.raw|-] [mooncrst]`.
