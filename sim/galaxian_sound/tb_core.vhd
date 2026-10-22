-- Full core testbench: load a ROM image (MRA rom index 0 layout), boot the
-- game and record the sound output.
--   ROMFILE : binary ROM image
--   MSEC    : simulated time after reset in ms
--   COIN_MS / START_MS : press coin / 1P start at these times (0 = never)
--   CLASSIC : 1 = OSD sound "Classic (FBNeo)"
-- Output OUTFILE: "<frame> <audio mean over the frame> <audio min> <audio max>"
-- Watchdog: fails if VSYNC stops (CPU/video deadlock) or audio has metavalues.
library ieee;
	use ieee.std_logic_1164.all;
	use ieee.numeric_std.all;
	use std.textio.all;
	use std.env.all;

entity tb_core is
	generic (
		ROMFILE  : string  := "galaxian_rom.bin";
		OUTFILE  : string  := "core_out.txt";
		MSEC     : integer := 100;
		COIN_MS  : integer := 0;
		START_MS : integer := 0;
		CLASSIC  : integer := 0
	);
end;

architecture sim of tb_core is
	constant T24 : time := 1 sec / 24_000_000;
	signal clk12, clk6 : std_logic := '0';
	signal rst   : std_logic := '1';
	signal dn_addr : std_logic_vector(15 downto 0) := (others => '0');
	signal dn_data : std_logic_vector(7 downto 0) := (others => '0');
	signal dn_wr : std_logic := '0';
	signal sw0, sw1 : std_logic_vector(7 downto 0) := (others => '0');
	signal audio : std_logic_vector(15 downto 0);
	signal vs, hs : std_logic;
	signal done : boolean := false;
	signal t_ms : integer := 0;
	signal classic_sel : std_logic;
begin
	classic_sel <= '1' when CLASSIC = 1 else '0';

	-- 12 MHz and 6 MHz with aligned rising edges, like the PLL outputs
	process
	begin
		while not done loop
			clk12 <= '1'; clk6 <= not clk6; wait for T24;
			clk12 <= '0'; wait for T24;
		end loop;
		wait;
	end process;

	dut : entity work.galaxian port map (
		W_CLK_12M => clk12, W_CLK_6M => clk6,
		W_SW0_DI => sw0, W_SW1_DI => sw1, W_DIP_DI => x"00", I_RESET => rst,
		dn_addr => dn_addr, dn_data => dn_data, dn_wr => dn_wr,
		hs_address => (others => '0'), hs_data_out => open, hs_data_in => (others => '0'), hs_write => '0',
		pause_cpu_n => '1',
		mod_mooncr => '0', mod_devilfsh => '0', mod_pisces => '0', mod_uniwars => '0',
		mod_kingbal => '0', mod_orbitron => '0', mod_moonqsr => '0', mod_porter => '0',
		Flip_Vertical => '0', snd_classic => classic_sel,
		W_R => open, W_G => open, W_B => open, HBLANK => open, VBLANK => open,
		W_H_SYNC => hs, W_V_SYNC => vs, W_AUDIO => audio, W_SDAT_C => open, O_CMPBL => open);

	-- ROM download while in reset, then inputs
	process
		type char_file is file of character;
		file f : char_file open read_mode is ROMFILE;
		variable c : character;
		variable a : natural := 0;
	begin
		rst <= '1';
		while not endfile(f) loop
			read(f, c);
			wait until rising_edge(clk12);
			dn_addr <= std_logic_vector(to_unsigned(a, 16));
			dn_data <= std_logic_vector(to_unsigned(character'pos(c), 8));
			dn_wr <= '1';
			wait until rising_edge(clk12);
			dn_wr <= '0';
			a := a + 1;
		end loop;
		report "ROM loaded: " & integer'image(a) & " bytes";
		for i in 0 to 15 loop wait until rising_edge(clk12); end loop;
		rst <= '0';
		loop
			wait until rising_edge(clk12);
			sw0(0) <= '1' when COIN_MS  > 0 and t_ms >= COIN_MS  and t_ms < COIN_MS  + 200 else '0';
			sw1(0) <= '1' when START_MS > 0 and t_ms >= START_MS and t_ms < START_MS + 200 else '0';
		end loop;
	end process;

	-- per frame audio summary, progress, watchdog
	process(clk12)
		file fo : text open write_mode is OUTFILE;
		variable l : line;
		variable cyc, vs_age : natural := 0;
		variable frame : natural := 0;
		variable acc : integer := 0;
		variable n : natural := 0;
		variable amin, amax, s : integer := 0;
		variable vs_last : std_logic := '0';
	begin
		if rising_edge(clk12) and rst = '0' then
			cyc := cyc + 1;
			if cyc mod 12000 = 0 then t_ms <= cyc / 12000; end if;
			assert not is_x(audio) report "audio has metavalues" severity failure;
			if cyc mod 16 = 0 then
				s := to_integer(signed(audio));
				acc := acc + s; n := n + 1;
				if n = 1 or s < amin then amin := s; end if;
				if n = 1 or s > amax then amax := s; end if;
			end if;
			vs_age := vs_age + 1;
			if vs = '1' and vs_last = '0' then
				frame := frame + 1; vs_age := 0;
				if n = 0 then n := 1; end if;
				write(l, frame); write(l, ' '); write(l, acc / n); write(l, ' ');
				write(l, amin); write(l, ' '); write(l, amax);
				writeline(fo, l);
				acc := 0; n := 0;
				if frame mod 30 = 0 then report "frame " & integer'image(frame); end if;
			end if;
			vs_last := vs;
			assert vs_age < 12_000_000 / 20 report "watchdog: no VSYNC for 50 ms" severity failure;
			if cyc >= MSEC * 12000 then
				report "done, frames " & integer'image(frame);
				done <= true;
				finish;
			end if;
		end if;
	end process;
end;
