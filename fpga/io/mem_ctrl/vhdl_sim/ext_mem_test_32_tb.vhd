-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 32 bit (burst of 2), externally 8x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use ieee.vital_timing.all;


library work;
    use work.mem_bus_pkg.all;

entity ext_mem_test_32_tb is

end ext_mem_test_32_tb;

architecture tb of ext_mem_test_32_tb is
    signal clock       : std_logic := '1';
    signal clk_2x      : std_logic := '1';
    signal reset       : std_logic := '0';
    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic := '0';
    signal req_16      : t_mem_burst_16_req := c_mem_burst_16_req_init;
    signal resp_16     : t_mem_burst_16_resp;
    signal req_32      : t_mem_burst_32_req := c_mem_burst_32_req_init;
    signal resp_32     : t_mem_burst_32_resp;
    signal okay        : std_logic;
    
	signal SDRAM_CLK   : std_logic;
	signal SDRAM_CKE   : std_logic;
    signal SDRAM_CSn   : std_logic := '1';
	signal SDRAM_RASn  : std_logic := '1';
	signal SDRAM_CASn  : std_logic := '1';
	signal SDRAM_WEn   : std_logic := '1';
    signal SDRAM_DQM   : std_logic := '0';
    signal SDRAM_A     : std_logic_vector(12 downto 0);
    signal SDRAM_BA    : std_logic_vector(1 downto 0);
    signal MEM_D       : std_logic_vector(7 downto 0) := (others => 'Z');

	signal logic_CLK   : std_logic;
	signal logic_CKE   : std_logic;
    signal logic_CSn   : std_logic := '1';
	signal logic_RASn  : std_logic := '1';
	signal logic_CASn  : std_logic := '1';
	signal logic_WEn   : std_logic := '1';
    signal logic_DQM   : std_logic := '0';
    signal logic_A     : std_logic_vector(12 downto 0);
    signal logic_BA    : std_logic_vector(1 downto 0);

    signal dummy_data  : std_logic_vector(15 downto 0) := (others => 'H');
    signal dummy_dqm   : std_logic_vector(1 downto 0) := (others => 'H');
    constant c_wire_delay : VitalDelayType01 := ( 2 ns, 3 ns );
begin

    clock <= not clock after 10.2 ns;
    clk_2x <= not clk_2x after 5.1 ns;
    reset <= '1', '0' after 100 ns;

    i_checker: entity work.ext_mem_test_32
    port map (
        clock       => clock,
        reset       => reset,
        
        req         => req_32,
        resp        => resp_32,
        
        okay        => okay );

    i_convert: entity work.mem_16to32
    port map (
        clock       => clock,
        reset       => reset,
        
        req_16      => req_16,
        resp_16     => resp_16,
    
        req_32      => req_32,
        resp_32     => resp_32 );

    i_mut: entity work.ext_mem_ctrl_v6
	generic map (
        q_tcko_data  => 5 ns,
		g_simulation => true )
    port map (
        clock       => clock,
        clk_2x      => clk_2x,
        reset       => reset,
    
        inhibit     => inhibit,
        is_idle     => is_idle,
    
        req         => req_16,
        resp        => resp_16,
    
    	SDRAM_CLK	=> logic_CLK,
    	SDRAM_CKE	=> logic_CKE,
        SDRAM_CSn   => logic_CSn,
    	SDRAM_RASn  => logic_RASn,
    	SDRAM_CASn  => logic_CASn,
    	SDRAM_WEn   => logic_WEn,
        SDRAM_DQM   => logic_DQM,
    
        SDRAM_BA    => logic_BA,
        SDRAM_A     => logic_A,
        SDRAM_DQ    => MEM_D );


    i_sdram : entity work.mt48lc16m16a2
    generic map(
        tipd_BA0                 => c_wire_delay,
        tipd_BA1                 => c_wire_delay,
        tipd_DQMH                => c_wire_delay,
        tipd_DQML                => c_wire_delay,
        tipd_DQ0                 => c_wire_delay,
        tipd_DQ1                 => c_wire_delay,
        tipd_DQ2                 => c_wire_delay,
        tipd_DQ3                 => c_wire_delay,
        tipd_DQ4                 => c_wire_delay,
        tipd_DQ5                 => c_wire_delay,
        tipd_DQ6                 => c_wire_delay,
        tipd_DQ7                 => c_wire_delay,
        tipd_DQ8                 => c_wire_delay,
        tipd_DQ9                 => c_wire_delay,
        tipd_DQ10                => c_wire_delay,
        tipd_DQ11                => c_wire_delay,
        tipd_DQ12                => c_wire_delay,
        tipd_DQ13                => c_wire_delay,
        tipd_DQ14                => c_wire_delay,
        tipd_DQ15                => c_wire_delay,
        tipd_CLK                 => c_wire_delay,
        tipd_CKE                 => c_wire_delay,
        tipd_A0                  => c_wire_delay,
        tipd_A1                  => c_wire_delay,
        tipd_A2                  => c_wire_delay,
        tipd_A3                  => c_wire_delay,
        tipd_A4                  => c_wire_delay,
        tipd_A5                  => c_wire_delay,
        tipd_A6                  => c_wire_delay,
        tipd_A7                  => c_wire_delay,
        tipd_A8                  => c_wire_delay,
        tipd_A9                  => c_wire_delay,
        tipd_A10                 => c_wire_delay,
        tipd_A11                 => c_wire_delay,
        tipd_A12                 => c_wire_delay,
        tipd_WENeg               => c_wire_delay,
        tipd_RASNeg              => c_wire_delay,
        tipd_CSNeg               => c_wire_delay,
        tipd_CASNeg              => c_wire_delay,
        -- tpd delays
        tpd_CLK_DQ2              => ( 4 ns, 4 ns, 4 ns, 4 ns, 4 ns, 4 ns ),
        tpd_CLK_DQ3              => ( 4 ns, 4 ns, 4 ns, 4 ns, 4 ns, 4 ns ),
--        -- tpw values: pulse widths
--        tpw_CLK_posedge          : VitalDelayType    := UnitDelay;
--        tpw_CLK_negedge          : VitalDelayType    := UnitDelay;
--        -- tsetup values: setup times
--        tsetup_DQ0_CLK           : VitalDelayType    := UnitDelay;
--        -- thold values: hold times
--        thold_DQ0_CLK            : VitalDelayType    := UnitDelay;
--        -- tperiod_min: minimum clock period = 1/max freq
--        tperiod_CLK_posedge      : VitalDelayType    := UnitDelay;
--
        mem_file_name => "none",
        tpowerup => 100 ns )
    port map(
        BA0     => logic_BA(0),
        BA1     => logic_BA(1),
        DQMH    => dummy_dqm(1),
        DQML    => logic_DQM,
        DQ0     => MEM_D(0),
        DQ1     => MEM_D(1),
        DQ2     => MEM_D(2),
        DQ3     => MEM_D(3),
        DQ4     => MEM_D(4),
        DQ5     => MEM_D(5),
        DQ6     => MEM_D(6),
        DQ7     => MEM_D(7),
        DQ8     => dummy_data(8),
        DQ9     => dummy_data(9),
        DQ10    => dummy_data(10),
        DQ11    => dummy_data(11),
        DQ12    => dummy_data(12),
        DQ13    => dummy_data(13),
        DQ14    => dummy_data(14),
        DQ15    => dummy_data(15),
        CLK     => logic_CLK,
        CKE     => logic_CKE,
        A0      => logic_A(0),
        A1      => logic_A(1),
        A2      => logic_A(2),
        A3      => logic_A(3),
        A4      => logic_A(4),
        A5      => logic_A(5),
        A6      => logic_A(6),
        A7      => logic_A(7),
        A8      => logic_A(8),
        A9      => logic_A(9),
        A10     => logic_A(10),
        A11     => logic_A(11),
        A12     => logic_A(12),
        WENeg   => logic_WEn,
        RASNeg  => logic_RASn,
        CSNeg   => logic_CSn,
        CASNeg  => logic_CASn );

end;
