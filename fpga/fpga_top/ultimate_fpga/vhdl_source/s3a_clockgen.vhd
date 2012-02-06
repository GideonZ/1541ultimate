library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

library unisim;
use unisim.vcomponents.all;

entity s3a_clockgen is
port (
    clk_50       : in  std_logic;
    reset_in     : in  std_logic;
    
    dcm_lock     : out std_logic;
    
    sys_clock    : out std_logic; -- 50 MHz
    sys_reset    : out std_logic;
    sys_clock_2x : out std_logic;
    sys_clock_4x : out std_logic;
    
    drive_stop   : in  std_logic := '0';
    drv_clock_en : out std_logic; -- 1/12.5 (4 MHz)
    cpu_clock_en : out std_logic; -- 1/50   (1 MHz)
    
    eth_clock    : out std_logic; -- / 2.5  (20 MHz)

    iec_reset_n  : in  std_logic := '1';
    iec_reset_o  : out std_logic );
    
end s3a_clockgen;

architecture Gideon of s3a_clockgen is
	signal clk_in_buf		: std_logic;
    signal sys_clk_buf      : std_logic;

    signal reset_dcm        : std_logic;
    signal reset_cnt        : integer range 0 to 63  := 0;
    signal dcm1_locked      : std_logic := '1';

	signal sys_clk_i		: std_logic := '0';
	signal sys_reset_i		: std_logic := '1';
	signal sys_reset_p		: std_logic := '1';
	
    signal div_cnt          : std_logic_vector(3 downto 0) := "0000";
    signal pre_cnt          : std_logic_vector(1 downto 0) := "00";
	signal cpu_cke_i		: std_logic := '0';
    signal toggle           : std_logic := '0';
    signal reset_c          : std_logic;
    
    signal reset_out        : std_logic := '1';
    signal sysrst_cnt       : integer range 0 to 63;

    signal iec_reset_sh     : std_logic_vector(0 to 2) := "000";
--    signal reset_sample_cnt : integer range 0 to 127 := 0;
--    signal reset_float      : std_logic := '1';
    
    attribute register_duplication : string;
    attribute register_duplication of sys_reset_i : signal is "no";

    signal clk_0_pre       : std_logic;
    signal clk_2x_pre      : std_logic;
    signal clk_4x_pre      : std_logic;
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
        CLKFX_MULTIPLY     => 4,
        CLKFX_DIVIDE       => 1,
		STARTUP_WAIT       => true
	)
	port map
	(
		CLKIN    => clk_in_buf,
		CLKFB    => sys_clk_buf,
		CLK0     => clk_0_pre,
        CLK2X    => clk_2x_pre,
        CLKFX    => clk_4x_pre,
        CLKDV    => eth_clock,
		LOCKED   => dcm1_locked,
		RST      => reset_dcm
	);

	bufg_sys:   BUFG port map (I => clk_0_pre,  O => sys_clk_buf);
	bufg_sys2x: BUFG port map (I => clk_2x_pre, O => sys_clock_2x);
	bufg_sys4x: BUFG port map (I => clk_4x_pre, O => sys_clock_4x);

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

			drv_clock_en <= '0';
			cpu_cke_i    <= '0';
			
            if drive_stop='0' then
    			if (div_cnt = X"B" and toggle='0') or
    			   (div_cnt = X"C" and toggle='1') then
    				div_cnt <= X"0";
    				drv_clock_en <= '1';
    
                    toggle <= not toggle;
    
    	            pre_cnt <= pre_cnt + 1;
    	
    	            if pre_cnt = "11" then
    	                cpu_cke_i <= '1';
    	            else
    	                cpu_cke_i <= '0';
    	            end if;
    			else
    				div_cnt <= div_cnt + 1;
    			end if;
            end if;

            if cpu_cke_i = '1' then
                iec_reset_sh(0) <= not iec_reset_n;
                iec_reset_sh(1 to 2) <= iec_reset_sh(0 to 1);
            end if;                    

            if sys_reset_p='1' then
                toggle      <= '0';
                pre_cnt     <= (others => '0');
                div_cnt     <= (others => '0');
            end if;
        end if;
        if dcm1_locked='0' then
            sysrst_cnt  <= 0;
            sys_reset_i <= '1';
            sys_reset_p <= '1';
        end if;
    end process;

    sys_reset    <= sys_reset_p;
	cpu_clock_en <= cpu_cke_i;
    iec_reset_o  <= '1' when iec_reset_sh="111" else '0';
end Gideon;