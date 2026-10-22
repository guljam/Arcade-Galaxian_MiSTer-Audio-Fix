-- Silent stand-in for rtl/galaxian_snd.vhd (GALAXIAN_SND), used by
-- build_core.sh with STUB_DISCRETE=1 to speed up full core runs.
library ieee;
	use ieee.std_logic_1164.all;

entity GALAXIAN_SND is
port (
	I_CLK_12M  : in  std_logic;
	I_RESET    : in  std_logic;
	I_MOONCR   : in  std_logic;
	I_BD       : in  std_logic_vector(7 downto 0);
	I_PITCH_WE : in  std_logic;
	I_FS       : in  std_logic_vector(2 downto 0);
	I_HIT      : in  std_logic;
	I_FIRE     : in  std_logic;
	I_VOL1     : in  std_logic;
	I_VOL2     : in  std_logic;
	I_DAC      : in  std_logic_vector(3 downto 0);
	O_AUDIO    : out std_logic_vector(15 downto 0);
	O_DBG      : out std_logic_vector(199 downto 0);
	O_DBG_STEP : out std_logic
);
end;

architecture stub of GALAXIAN_SND is
begin
	O_AUDIO <= (others => '0');
	O_DBG <= (others => '0');
	O_DBG_STEP <= '0';
end;
