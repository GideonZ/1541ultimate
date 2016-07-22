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

entity mem_io_synth is
port (
    ref_clock        : in  std_logic := '0';
    ref_reset        : in  std_logic;
    sys_clock        : out   std_logic;
    sys_reset        : out   std_logic;
    write_data       : out   std_logic_vector(15 downto 0);
    read_data        : out   std_logic_vector(15 downto 0);
    do_read          : out   std_logic;
    rdata_valid      : out   std_logic;
    mode             : in    std_logic_vector(1 downto 0) := "00";
    measurement      : out   std_logic_vector(11 downto 0);
    
    SDRAM_CLK        : inout std_logic := 'Z';
    SDRAM_CLKn       : inout std_logic := 'Z';
    SDRAM_A          : out   std_logic_vector(7 downto 0);
    SDRAM_DQ         : inout std_logic_vector(3 downto 0);
    SDRAM_DQS        : inout std_logic );
end entity;

architecture arch of mem_io_synth is
    
    signal sys_clock_i      : std_logic;
    signal sys_reset_i      : std_logic;

    signal phasecounterselect : std_logic_vector(2 downto 0) := (others => '0');
    signal phasestep          : std_logic := '0';
    signal phaseupdown        : std_logic := '0';
    signal phasedone          : std_logic := '0';
    
    signal addr_first         : std_logic_vector(7 downto 0) := X"00";
    signal addr_second        : std_logic_vector(7 downto 0) := X"00";
    signal wdata              : std_logic_vector(15 downto 0) := X"0000";
    signal wdata_oe           : std_logic;

    signal counter            : unsigned(4 downto 0) := (others => '0');
begin
    i_mut: entity work.mem_io
    port map (
        ref_clock          => ref_clock,
        ref_reset          => ref_reset,
        sys_clock          => sys_clock_i,
        sys_reset          => sys_reset_i,
        phasecounterselect => phasecounterselect,
        phasestep          => phasestep,
        phaseupdown        => phaseupdown,
        phasedone          => phasedone,
        mode               => mode,
        measurement        => measurement,
        
        addr_first         => addr_first,
        addr_second        => addr_second,
        wdata              => wdata,
        wdata_oe           => wdata_oe,
        rdata              => read_data,
        mem_clk_p          => SDRAM_CLK,
        mem_clk_n          => SDRAM_CLKn,
        mem_addr           => SDRAM_A,
        mem_dq             => SDRAM_DQ,
        mem_dqs            => SDRAM_DQS
    );
    

    process(sys_clock_i, phasedone)
    begin
        if rising_edge(sys_clock_i) then
            counter <= counter + 1;
            wdata_oe <= '0';
            addr_first <= (others => '1');
            addr_second <= (others => '1');
            do_read <= '0';
            rdata_valid <= '0';
            
            case counter is
            when "01101" =>
                addr_first(7) <= '0';
                addr_first(5) <= '0'; -- write
            when "01110" =>
                wdata <= wdata(0) & wdata(15 downto 1);
                wdata_oe <= '1';
            when "01111" =>
                addr_first(7) <= '0';
                addr_first(6) <= '0'; -- row
            when "10000" =>
                addr_first(7) <= '0';
                addr_first(4) <= '0'; -- read
                do_read <= '1';
            when "10100" =>
                rdata_valid <= '1';
            when "10111" =>
                phasecounterselect <= "101"; -- read clock
                phasestep <= '1';
            when "01100" =>
                phasecounterselect <= "110"; -- measure
                phasestep <= '1';
                            
            when others =>
                null;
            end case;
            
            if sys_reset_i = '1' then
                phasestep <= '0';
                wdata <= X"B769";
            end if;
        end if;
        if phasedone = '0' then
            phasestep <= '0';
        end if;

    end process;

    sys_clock <= sys_clock_i;
    sys_reset <= sys_reset_i;
    
    write_data  <= wdata;
    
end arch;
