# Galaxian sound verification

Tools used to build and check `rtl/galaxian_snd.vhd` against MAME, and
`rtl/galaxian_snd_fbn.vhd` (OSD "Legacy Synth") against FBNeo.
Background and results: `docs/fixes-2026-10-22.md`, `docs/fixes-2026-10-23.md`.

Tools: MSYS2 mingw64 (`g++`, `ghdl` 6 with LLVM backend, `verilator` 5), Python 3 with numpy/scipy, MAME.

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
| `fbn_ref.cpp` | FBNeo's Galaxian sound ported as-is (golden model of the classic sound); `pkg` -> `rtl/galaxian_snd_fbn_pkg.vhd` |
| `fbn_fix.cpp` | bit-exact C++ model of `galaxian_snd_fbn.vhd` + its testbench |
| `tb_galaxian_snd_fbn.vhd` | GHDL testbench of the classic sound block |
| `vl_fbn.sh`, `vl_tb_fbn.cpp` | Verilator build (VHDL -> Verilog via `ghdl --synth`) of the classic sound block |
| `snd_stub.vhd` | silent discrete sound block for fast full core runs (`STUB_DISCRETE=1 sh build_core.sh`) |

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

Legacy Synth (FBNeo) sound:

```sh
g++ -O2 -o fbn_ref.exe fbn_ref.cpp && g++ -O2 -o fbn_fix.exe fbn_fix.cpp
./fbn_ref.exe pkg > ../../rtl/galaxian_snd_fbn_pkg.vhd

# Verilator == C++ model (bit exact), ~1 min for 60 s
sh vl_fbn.sh
timeout 600 vl/vl_fbn.exe galaxian_writes.txt vl_out.txt 60000
./fbn_fix.exe ../../rtl/galaxian_snd_fbn_pkg.vhd galaxian_writes.txt fix_fbn.txt 60000 fix_fbn.raw
cmp vl_out.txt fix_fbn.txt

# GHDL == C++ model, 400 ms in a few seconds
ghdl -a --std=08 --workdir=work ../../rtl/galaxian_snd_fbn_pkg.vhd ../../rtl/galaxian_snd_fbn.vhd tb_galaxian_snd_fbn.vhd
ghdl -e --std=08 --workdir=work -O2 tb_galaxian_snd_fbn
timeout 600 ./tb_galaxian_snd_fbn.exe -gWRITES=stim_short.txt -gMSEC=400 -gOUTFILE=rtl_fbn.txt

# C++ model vs FBNeo (sample timing of the RTL; FBN_EXACT=1: exact wave positions)
FBN_EXACT=1 ./fbn_ref.exe aligned galaxian_writes.txt ref_fbn.raw 60
# (FBN_SOLO=0..3 renders only tone / explosion / fire / swarm, in both programs)

# whole core, classic sound, discrete block stubbed (~4.4 min per second of game time)
STUB_DISCRETE=1 sh build_core.sh
timeout 600 ./tb_core.exe -gROMFILE=galaxian_rom.bin -gMSEC=2000 -gCOIN_MS=300 -gSTART_MS=700 -gCLASSIC=1 --ieee-asserts=disable
```

Arguments: `galsnd_fix <pkg> <writes> <out.txt> <msec> [mooncr] [audio.raw]`,
`galsnd_ref <writes> <out.raw> [fs] [seconds] [taps.raw|-] [mooncrst]`.
