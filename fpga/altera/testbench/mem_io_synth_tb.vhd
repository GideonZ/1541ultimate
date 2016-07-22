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

entity mem_io_synth_tb is
generic (
    g_out_delay     : time := 8.5 ns;
    g_in_delay      : time := 2.0 ns );
end entity;

architecture arch of mem_io_synth_tb is
    signal ref_clock        : std_logic := '0';
    signal ref_reset        : std_logic;
    signal SDRAM_CLK        : std_logic := 'Z';
    signal SDRAM_CLKn       : std_logic := 'Z';
    signal SDRAM_A          : std_logic_vector(7 downto 0);
    signal SDRAM_DQ         : std_logic_vector(3 downto 0) := (others => 'Z');
    signal SDRAM_DQS        : std_logic;
    signal write_data       : std_logic_vector(15 downto 0);    
    signal read_data        : std_logic_vector(15 downto 0);
    signal do_read          : std_logic;
    signal delayed_clock    : std_logic;
    signal delayed_addr     : std_logic_vector(7 downto 0);
begin
    ref_clock <= not ref_clock after 10 ns; -- 20 ns cycle time
    ref_reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.mem_io_synth
    port map (
        ref_clock  => ref_clock,
        ref_reset  => ref_reset,
        SDRAM_CLK  => SDRAM_CLK,
        SDRAM_CLKn => SDRAM_CLKn,
        SDRAM_A    => SDRAM_A,
        SDRAM_DQ   => SDRAM_DQ,
        SDRAM_DQS  => SDRAM_DQS,
        write_data => write_data,
        read_data  => read_data,
        do_read    => do_read
    );

    delayed_clock <= transport SDRAM_CLK after g_out_delay;
    delayed_addr  <= transport SDRAM_A   after g_out_delay;
    
    process(delayed_clock)
        variable delay : integer := -10;
    begin
        if rising_edge(delayed_clock) then
            if delayed_addr(7 downto 4) = "0110" then
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

end arch;
