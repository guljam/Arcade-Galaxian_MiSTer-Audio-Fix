-- Testbench: replay a MAME sound register log into GALAXIAN_SND.
--
-- Output file: one line per 256 clocks (46.875 kHz):
--   <audio boxcar mean> <lfo_cap> <cv> <bg> <hit_out> <fire_out>
-- audio in 16 bit units, the taps in volts * 2**20.
library ieee;
	use ieee.std_logic_1164.all;
	use ieee.numeric_std.all;
	use std.textio.all;
	use std.env.all;

entity tb_galaxian_snd is
	generic (
		WRITES  : string  := "writes.txt";
		OUTFILE : string  := "rtl_out.txt";
		MSEC    : integer := 60000;
		MOONCR  : integer := 0
	);
end;

architecture sim of tb_galaxian_snd is
	constant TCLK : time := 1 sec / 12_000_000;
	signal clk   : std_logic := '0';
	signal rst   : std_logic := '1';
	signal bd    : std_logic_vector(7 downto 0) := (others => '0');
	signal pwe   : std_logic := '0';
	signal fs    : std_logic_vector(2 downto 0) := (others => '0');
	signal hit, fire, vol1, vol2 : std_logic := '0';
	signal dac   : std_logic_vector(3 downto 0) := (others => '0');
	signal audio : std_logic_vector(15 downto 0);
	signal mc_sel : std_logic := '0';
	signal cycle : natural := 0;
	signal done  : boolean := false;
	signal dbg   : std_logic_vector(199 downto 0);
	signal dbg_step : std_logic;
begin
	mc_sel <= '1' when MOONCR = 1 else '0';

	dut : entity work.GALAXIAN_SND port map (
		I_CLK_12M => clk, I_RESET => rst, I_MOONCR => mc_sel,
		I_BD => bd, I_PITCH_WE => pwe, I_FS => fs, I_HIT => hit, I_FIRE => fire,
		I_VOL1 => vol1, I_VOL2 => vol2, I_DAC => dac, O_AUDIO => audio,
		O_DBG => dbg, O_DBG_STEP => dbg_step);

	clk <= not clk after TCLK / 2 when not done;

	-- stimulus: apply each write at its logged time
	process
		file f : text open read_mode is WRITES;
		variable l : line;
		variable t : real;
		variable k : character;
		variable a, d : integer;
		variable cyc : natural;
	begin
		rst <= '1';
		wait until rising_edge(clk); wait until rising_edge(clk);
		rst <= '0';
		while not endfile(f) loop
			readline(f, l);
			read(l, t); read(l, k); read(l, k); read(l, a); read(l, d);
			exit when t * 1000.0 >= real(MSEC);
			cyc := natural(t * 12.0e6);
			while cycle < cyc loop wait until rising_edge(clk); end loop;
			if k = 'L' then
				dac(a) <= '1' when (d mod 2) = 1 else '0';
			elsif k = 'P' then
				bd <= std_logic_vector(to_unsigned(d, 8)); pwe <= '1';
				wait until rising_edge(clk);
				pwe <= '0';
			else
				case a is
					when 0 | 1 | 2 => fs(a) <= '1' when (d mod 2) = 1 else '0';
					when 3 => hit  <= '1' when (d mod 2) = 1 else '0';
					when 5 => fire <= '1' when (d mod 2) = 1 else '0';
					when 6 => vol1 <= '1' when (d mod 2) = 1 else '0';
					when 7 => vol2 <= '1' when (d mod 2) = 1 else '0';
					when others => null;
				end case;
			end if;
		end loop;
		wait;
	end process;

	-- output, progress, watchdog
	mon : block
		signal lfo_cap : signed(39 downto 0);
		signal cv : signed(39 downto 0);
		signal bg : signed(39 downto 0);
		signal hit_out : signed(39 downto 0);
		signal fire_out : signed(39 downto 0);
	begin
	lfo_cap <= signed(dbg(39 downto 0));
	cv <= signed(dbg(79 downto 40));
	bg <= signed(dbg(119 downto 80));
	hit_out <= signed(dbg(159 downto 120));
	fire_out <= signed(dbg(199 downto 160));
	process(clk)
		file fo : text open write_mode is OUTFILE;
		variable l : line;
		variable acc : integer := 0;
		variable n : natural := 0;
		variable last_lfo : signed(39 downto 0) := (others => '0');
		variable same_lfo : natural := 0;
		function v20(x : signed(39 downto 0)) return integer is
		begin
			return to_integer(x(39 downto 12));
		end;
	begin
		if rising_edge(clk) then
			cycle <= cycle + 1;
			if rst = '0' and dbg_step = '1' then   -- audio register updated at step_cnt 0
				acc := acc + to_integer(signed(audio));
				n := n + 1;
				-- watchdog: the LFO sawtooth must keep moving
				if lfo_cap = last_lfo then same_lfo := same_lfo + 1; else same_lfo := 0; end if;
				last_lfo := lfo_cap;
				assert same_lfo < 750000 report "watchdog: LFO stuck for 1 s" severity failure;
				if n = 16 then
					write(l, acc / 16); write(l, ' ');
					write(l, v20(lfo_cap)); write(l, ' '); write(l, v20(cv)); write(l, ' ');
					write(l, v20(bg)); write(l, ' '); write(l, v20(hit_out)); write(l, ' ');
					write(l, v20(fire_out));
					writeline(fo, l);
					acc := 0; n := 0;
				end if;
			end if;
			if cycle mod 12_000_000 = 0 then
				report "sim time " & integer'image(cycle / 12_000_000) & " s";
			end if;
			if cycle >= MSEC * 12000 then
				done <= true;
				report "done";
				finish;
			end if;
		end if;
	end process;
	end block;
end;
