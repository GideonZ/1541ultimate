--------------------------------------------------------------------------------
-- Entity: mem_io_synth
-- Date:2016-07-17  
-- Author: Gideon     
--
-- Description: Testbench for altera io for ddr
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mem_io_timing_tb is
generic (
    g_in_delay      : time := 0.3 ns );
end entity;

architecture arch of mem_io_timing_tb is
    signal ref_clock        : std_logic := '0';
    signal ref_reset        : std_logic;
    signal sys_clock        : std_logic := '0';
    signal sys_reset        : std_logic;
    signal SDRAM_CLK        : std_logic := 'Z';
    signal SDRAM_CLKn       : std_logic := 'Z';
    signal SDRAM_A          : std_logic_vector(7 downto 0);
    signal SDRAM_DQ         : std_logic_vector(3 downto 0) := (others => 'Z');
    signal SDRAM_DQS        : std_logic;
    signal write_data       : std_logic_vector(15 downto 0);    
    signal read_data        : std_logic_vector(15 downto 0);
    signal do_read          : std_logic;
    signal mode             : std_logic_vector(1 downto 0) := "01"; 
    signal correct          : std_logic;
    signal measurement      : std_logic_vector(11 downto 0);
    signal latency          : integer := 0;
    signal rdata_valid      : std_logic;
begin
    ref_clock <= not ref_clock after 10 ns; -- 20 ns cycle time
    ref_reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.mem_io_synth
    port map (
        ref_clock  => ref_clock,
        ref_reset  => ref_reset,
        sys_clock  => sys_clock,
        sys_reset  => sys_reset,
        SDRAM_CLK  => SDRAM_CLK,
        SDRAM_CLKn => SDRAM_CLKn,
        SDRAM_A    => SDRAM_A,
        SDRAM_DQ   => SDRAM_DQ,
        SDRAM_DQS  => SDRAM_DQS,
        write_data => write_data,
        read_data  => read_data,
        do_read    => do_read,
        rdata_valid=> rdata_valid,
        mode       => mode,
        measurement => measurement
    );

    process(SDRAM_CLK)
        variable delay : integer := -10;
    begin
        if rising_edge(SDRAM_CLK) then
            if SDRAM_A(7 downto 4) = "0110" then
                delay := 7;
            end if;
        end if;
        delay := delay - 1;
        case delay is
        when 0 =>
            SDRAM_DQ <= transport X"2" after g_in_delay;
        when -1 =>
            SDRAM_DQ <= transport X"3" after g_in_delay;
        when -2 =>
            SDRAM_DQ <= transport X"4" after g_in_delay;
        when -3 =>
            SDRAM_DQ <= transport X"5" after g_in_delay;
        when others =>
            SDRAM_DQ <= transport "ZZZZ" after g_in_delay;
        end case;
    end process;

    process(sys_clock)
        variable timer : integer := 0;
    begin
        if falling_edge(sys_clock) then
            if do_read = '1' then
                timer := 0;
            else
                timer := timer + 1;
            end if;
            
            if read_data = X"5432" then
                correct <= '1';
                latency <= timer;
            else
                correct <= '0';
            end if;
        end if;
    end process;

end arch;
