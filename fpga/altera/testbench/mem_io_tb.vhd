--------------------------------------------------------------------------------
-- Entity: mem_io_tb
-- Date:2016-07-17  
-- Author: Gideon     
--
-- Description: Testbench for altera io for ddr
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity mem_io_tb is

end mem_io_tb;

architecture arch of mem_io_tb is
    signal ref_clock        : std_logic := '0';
    signal ref_reset        : std_logic;
    
    signal sys_clock        : std_logic;
    signal sys_reset        : std_logic;

    signal phasecounterselect : std_logic_vector(2 downto 0) := "000";
    signal phasestep          : std_logic := '0';
    signal phaseupdown        : std_logic := '0';
    signal phasedone          : std_logic := '0';
    
    signal mem_clk_p          : std_logic := 'Z';
    signal addr_first         : std_logic_vector(7 downto 0) := X"00";
    signal addr_second        : std_logic_vector(7 downto 0) := X"00";
    signal wdata              : std_logic_vector(15 downto 0) := X"0000";
    signal wdata_oe           : std_logic;
    signal dqs_burst_oe       : std_logic_vector(3 downto 0) := "0000";
    signal SDRAM_DQ           : std_logic_vector(3 downto 0);
    signal SDRAM_CLK          : std_logic := '0';

    signal count1, count2     : integer := 999;
begin
    ref_clock <= not ref_clock after 10 ns; -- 20 ns cycle time
    ref_reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.mem_io
    port map(
        ref_clock          => ref_clock,
        ref_reset          => ref_reset,
        sys_clock          => sys_clock,
        sys_reset          => sys_reset,
        phasecounterselect => phasecounterselect,
        phasestep          => phasestep,
        phaseupdown        => phaseupdown,
        phasedone          => phasedone,
        addr_first         => addr_first,
        addr_second        => addr_second,
        wdata              => wdata,
        wdata_oe           => wdata_oe,
        dqs_burst_oe       => dqs_burst_oe,
        mem_clk_p          => mem_clk_p,
        mem_dq             => SDRAM_DQ
    );
    
    process(sys_clock)
        variable i  : integer := 16;
    begin
        if rising_edge(sys_clock) then
            i := i - 1;
                
            if i = 0 then
                addr_first  <= X"AB";
                addr_second <= X"CD";
                wdata <= X"4321";
                wdata_oe <= '1';
            else
                addr_first <= X"00";
                addr_second <= X"00";
                wdata <= X"0000";
                wdata_oe <= '0';
            end if;
        end if;
    end process;

    SDRAM_CLK <= transport mem_clk_p after 11.5 ns;

    process(mem_clk_p)
        variable delay : integer := 28;
    begin
        delay := delay - 1;
        count2 <= delay;
    end process;
    
    process(SDRAM_CLK)
        variable delay : integer := 26; -- 28
    begin
        delay := delay - 1;
        count1 <= delay;
        case delay is
        when 0 =>
            SDRAM_DQ <= X"2";
        when -1 =>
            SDRAM_DQ <= X"3";
        when -2 =>
            SDRAM_DQ <= X"4";
        when -3 =>
            SDRAM_DQ <= X"5";
        when others =>
            SDRAM_DQ <= "ZZZZ";
        end case;
    end process;
end arch;
