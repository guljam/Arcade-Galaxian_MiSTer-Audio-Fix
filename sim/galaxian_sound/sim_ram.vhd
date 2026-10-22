-- Behavioral models of rtl/dpram.vhd and rtl/spram.vhd for simulation
-- (the RTL versions instantiate Altera altsyncram).
-- Registered address, unregistered output, new data on read during write.
library ieee;
	use ieee.std_logic_1164.all;
	use ieee.numeric_std.all;

entity dpram is
	generic (
		addr_width_g : integer := 8;
		data_width_g : integer := 8
	);
	port (
		address_a : in  std_logic_vector(addr_width_g-1 downto 0);
		address_b : in  std_logic_vector(addr_width_g-1 downto 0);
		clock_a   : in  std_logic := '1';
		clock_b   : in  std_logic;
		data_a    : in  std_logic_vector(data_width_g-1 downto 0);
		data_b    : in  std_logic_vector(data_width_g-1 downto 0) := (others => '0');
		enable_a  : in  std_logic := '1';
		enable_b  : in  std_logic := '1';
		wren_a    : in  std_logic := '0';
		wren_b    : in  std_logic := '0';
		q_a       : out std_logic_vector(data_width_g-1 downto 0);
		q_b       : out std_logic_vector(data_width_g-1 downto 0)
	);
end;

architecture sim of dpram is
	type mem_t is array(0 to 2**addr_width_g-1) of std_logic_vector(data_width_g-1 downto 0);
	shared variable mem : mem_t := (others => (others => '0'));
begin
	process(clock_a)
		variable a : integer;
	begin
		if rising_edge(clock_a) and enable_a = '1' then
			a := to_integer(unsigned(address_a));
			if wren_a = '1' then mem(a) := data_a; end if;
			q_a <= mem(a);
		end if;
	end process;

	process(clock_b)
		variable a : integer;
	begin
		if rising_edge(clock_b) and enable_b = '1' then
			a := to_integer(unsigned(address_b));
			if wren_b = '1' then mem(a) := data_b; end if;
			q_b <= mem(a);
		end if;
	end process;
end;

library ieee;
	use ieee.std_logic_1164.all;
	use ieee.numeric_std.all;

entity spram is
	generic (
		addr_width_g : integer := 8;
		data_width_g : integer := 8
	);
	port (
		address : in  std_logic_vector(addr_width_g-1 downto 0);
		clken   : in  std_logic := '1';
		clock   : in  std_logic := '1';
		data    : in  std_logic_vector(data_width_g-1 downto 0);
		wren    : in  std_logic;
		q       : out std_logic_vector(data_width_g-1 downto 0)
	);
end;

architecture sim of spram is
	type mem_t is array(0 to 2**addr_width_g-1) of std_logic_vector(data_width_g-1 downto 0);
	signal mem : mem_t := (others => (others => '0'));
begin
	process(clock)
		variable a : integer;
	begin
		if rising_edge(clock) and clken = '1' then
			a := to_integer(unsigned(address));
			if wren = '1' then
				mem(a) <= data; q <= data;
			else
				q <= mem(a);
			end if;
		end if;
	end process;
end;
