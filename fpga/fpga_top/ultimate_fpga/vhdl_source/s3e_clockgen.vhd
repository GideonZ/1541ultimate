library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

library unisim;
use unisim.vcomponents.all;

entity s3e_clockgen is
port (
    clk_50      : in  std_logic;
    reset_in    : in  std_logic;
    
    dcm_lock    : out std_logic;
    
    sys_clock   : out std_logic; -- 50 MHz
    sys_reset   : out std_logic;
    sys_shifted : out std_logic;

    eth_clock   : out std_logic ); -- / 2.5  (20 MHz)
    
end s3e_clockgen;

architecture Gideon of s3e_clockgen is
	signal clk_in_buf		: std_logic;
    signal sys_clk_buf      : std_logic;

    signal reset_dcm        : std_logic;
    signal reset_cnt        : integer range 0 to 63  := 0;
    signal dcm1_locked      : std_logic := '1';

	signal sys_clk_i		: std_logic := '0';
	signal sys_reset_i		: std_logic := '1';
	signal sys_reset_p		: std_logic := '1';
	
    signal reset_c          : std_logic;
    
    signal reset_out        : std_logic := '1';
    signal sysrst_cnt       : integer range 0 to 63;

    attribute register_duplication : string;
    attribute register_duplication of sys_reset_i : signal is "no";

    signal clk_0_pre       : std_logic;
    signal clk_270_pre     : std_logic;
begin
    dcm_lock <= dcm1_locked;
   
	bufg_in : BUFG port map (I => clk_50, O => clk_in_buf);

    process(clk_in_buf)
    begin
        if rising_edge(clk_in_buf) then
            if reset_cnt = 63 then
                reset_dcm <= '0';
            else
                reset_cnt <= reset_cnt + 1;
                reset_dcm <= '1';
            end if;
        end if;
        if reset_in='1' then
            reset_dcm <= '1';
            reset_cnt <= 0;
        end if;
    end process;

    dcm_shft: DCM
	generic map
	(
		CLKIN_PERIOD       => 20.0,
--		CLKOUT_PHASE_SHIFT => "FIXED",
		CLK_FEEDBACK       => "1X",
--		PHASE_SHIFT        => -20,
        CLKDV_DIVIDE       => 2.5,
		STARTUP_WAIT       => true
	)
	port map
	(
		CLKIN    => clk_in_buf,
		CLKFB    => sys_clk_buf,
		CLK0     => clk_0_pre,
        CLK270   => clk_270_pre,
        CLKDV    => eth_clock,
		LOCKED   => dcm1_locked,
		RST      => reset_dcm
	);

	bufg_sys:  BUFG port map (I => clk_0_pre,   O => sys_clk_buf);
	bufg_shft: BUFG port map (I => clk_270_pre, O => sys_shifted);

    sys_clk_i   <= sys_clk_buf;
    sys_clock   <= sys_clk_buf;

    process(sys_clk_i, dcm1_locked)
    begin
        if rising_edge(sys_clk_i) then
            if sysrst_cnt = 63 then
                sys_reset_i <= '0';
            else
                sysrst_cnt <= sysrst_cnt + 1;
            end if;
            sys_reset_p  <= sys_reset_i;
        end if;
        if dcm1_locked='0' then
            sysrst_cnt  <= 0;
            sys_reset_i <= '1';
            sys_reset_p <= '1';
        end if;
    end process;

    sys_reset    <= sys_reset_p;
end Gideon;
