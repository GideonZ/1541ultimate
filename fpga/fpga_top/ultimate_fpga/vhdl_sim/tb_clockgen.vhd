library ieee;
use ieee.std_logic_1164.all;

entity tb_clockgen is
end;

architecture tb of tb_clockgen is
    signal clk_50       : std_logic := '0';
    signal reset_in     : std_logic;
    signal dcm_lock     : std_logic;
    signal sys_clock    : std_logic; -- 48 MHz
    signal sys_reset    : std_logic;
    signal drv_clock_en : std_logic; -- 1/12 (4 MHz)
    signal cpu_clock_en : std_logic; -- 1/48 (1 MHz)
begin

    clk_50 <= not clk_50 after 10 ns;
    reset_in <= '1', '0' after 100 ns;
    
    gen: entity work.s3e_clockgen
    generic map ( false )
    port map (
        clk_50       => clk_50,
        reset_in     => reset_in,
                                    
        dcm_lock     => dcm_lock,
                                    
        sys_clock    => sys_clock, -- 48 MHz
        sys_reset    => sys_reset,
        drv_clock_en => drv_clock_en, -- 1/12 (4 MHz)
        cpu_clock_en => cpu_clock_en, -- 1/48 (1 MHz)
                                    
    	cpu_speed	 => '0', -- 0 = 1 MHz, 1 = max.
    	mem_ready	 => '1'	);
        
end tb;

