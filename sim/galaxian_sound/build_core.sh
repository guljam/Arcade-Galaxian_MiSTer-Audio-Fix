#!/bin/sh
# Build the full core GHDL testbench (tb_core). Sources are copied to wcore/src
# and "inst : work.dpram" is rewritten to "inst : entity work.dpram" (Quartus
# accepts the short form, GHDL does not). The RTL files are not modified.
set -e
export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH
R=../../rtl
rm -rf wcore && mkdir -p wcore/src
for f in T80/T80_Pack T80/T80_ALU T80/T80_MCode T80/T80_Reg T80/T80 T80/T80s T80/T80se T80/T80sed T80/T80a T80/T80as T80/T80pa T80/GBse T80/T8080se \
         galaxian_snd_pkg galaxian_snd kb_synth mc_missile mc_logic mc_ld_pls mc_inport \
         mc_hv_count mc_col_pal mc_bram mc_adec mc_stars mc_video galaxian; do
  sed -E 's/:[[:space:]]*work\.(dpram|spram)/: entity work.\1/' $R/$f.vhd > wcore/src/$(basename $f).vhd
  SRC="$SRC wcore/src/$(basename $f).vhd"
done
# galaxian.vhd: std_logic case without "when others" (Quartus accepts it)
sed -i 's/when "111" => W_A_R(13 downto 11) <= "110";/& when others => null;/' wcore/src/galaxian.vhd
O="--std=08 -fsynopsys -frelaxed --workdir=wcore"
ghdl -a $O sim_ram.vhd $SRC tb_core.vhd
ghdl -e $O -O2 tb_core
