-- Testbench: replay a MAME sound register log into GALAXIAN_SND_FBN.
--
-- Everything is driven from one clocked process: at clock edge n the inputs
-- for edge n+1 are set (same timing as sim/galaxian_sound/fbn_fix.cpp).
-- Frame tick: I_VSYNC high for clocks 100..1099 of every 198000 (60.606 Hz).
-- Output file: the audio value every 250 clocks (48 kHz), one per line.
-- Watchdog: fails if the output contains metavalues or if the sample
-- sequencer stops (no sample for 1000 clocks).
library ieee;
	use ieee.std_logic_1164.all;
	use ieee.numeric_std.all;
	use std.textio.all;
	use std.env.all;

entity tb_galaxian_snd_fbn is
	generic (
		WRITES  : string  := "writes.txt";
		OUTFILE : string  := "rtl_fbn.txt";
		MSEC    : integer := 1000
	);
end;

architecture sim of tb_galaxian_snd_fbn is
	constant TCLK : time := 1 sec / 12_000_000;
	signal clk   : std_logic := '0';
	signal done  : boolean := false;
	signal rst   : std_logic := '1';
	signal bd    : std_logic_vector(7 downto 0) := (others => '0');
	signal pwe, hit_wr, fire, vol1, vol2, vs : std_logic := '0';
	signal fs    : std_logic_vector(2 downto 0) := (others => '0');
	signal dac   : std_logic_vector(3 downto 0) := (others => '0');
	signal audio : std_logic_vector(15 downto 0);
	signal sample : std_logic;
begin
	clk <= not clk after TCLK / 2 when not done;

	dut : entity work.GALAXIAN_SND_FBN port map (
		I_CLK_12M => clk, I_RESET => rst, I_BD => bd, I_PITCH_WE => pwe,
		I_FS => fs, I_HIT_WR => hit_wr, I_FIRE => fire, I_VOL1 => vol1, I_VOL2 => vol2,
		I_DAC => dac, I_VSYNC => vs, O_AUDIO => audio, O_SAMPLE => sample);

	process
		file f  : text open read_mode is WRITES;
		file fo : text open write_mode is OUTFILE;
		variable l, lo : line;
		variable t : real;
		variable k, sp : character;
		variable a, d : integer;
		variable have : boolean := false;
		variable n : natural := 0;
		variable last_audio : std_logic_vector(15 downto 0);
		variable same : natural := 0;
		variable bit : std_logic;
	begin
		if not endfile(f) then
			readline(f, l); read(l, t); read(l, sp); read(l, k); read(l, a); read(l, d); have := true;
		end if;
		loop
			wait until rising_edge(clk);
			n := n + 1;
			if n mod 250 = 0 then
				assert not is_x(audio) report "audio has metavalues" severity failure;
				write(lo, to_integer(signed(audio))); writeline(fo, lo);
			end if;
			-- inputs for the next edge
			rst <= '1' when n < 4 else '0';
			pwe <= '0'; hit_wr <= '0';
			vs <= '1' when (n mod 198000) >= 100 and (n mod 198000) < 1100 else '0';
			while have and natural(t * 12.0e6) <= n loop
				bit := '1' when (d mod 2) = 1 else '0';
				if k = 'L' then
					dac(a) <= bit;
				elsif k = 'P' then
					bd <= std_logic_vector(to_unsigned(d, 8)); pwe <= '1';
				else
					case a is
						when 0 | 1 | 2 => fs(a) <= bit;
						when 3 => if bit = '1' then hit_wr <= '1'; end if;
						when 5 => fire <= bit;
						when 6 => vol1 <= bit;
						when 7 => vol2 <= bit;
						when others => null;
					end case;
				end if;
				if endfile(f) then
					have := false;
				else
					readline(f, l); read(l, t); read(l, sp); read(l, k); read(l, a); read(l, d);
				end if;
			end loop;
			if n mod 12_000_000 = 0 then report "sim time " & integer'image(n / 12_000_000) & " s"; end if;
			if n >= MSEC * 12000 then
				report "done";
				done <= true;
				finish;
			end if;
		end loop;
	end process;

	-- watchdog: the sample sequencer must keep producing samples (every 250 clocks)
	process(clk)
		variable idle : natural := 0;
	begin
		if rising_edge(clk) then
			if rst = '0' then
				idle := idle + 1;
				if sample = '1' then idle := 0; end if;
				assert idle < 1000 report "watchdog: sample sequencer stopped" severity failure;
			end if;
		end if;
	end process;
end;
