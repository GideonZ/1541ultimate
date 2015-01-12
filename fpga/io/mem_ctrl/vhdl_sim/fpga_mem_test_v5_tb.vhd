--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: fpga_mem_test_v5_tb
-- Date:2015-01-02  
-- Author: Gideon     
-- Description: Testbench for FPGA mem tester
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fpga_mem_test_v5_tb is

end entity;

architecture arch of fpga_mem_test_v5_tb is
    signal CLOCK_50        : std_logic := '0';
    signal SDRAM_CLK       : std_logic;
    signal SDRAM_CKE       : std_logic;
    signal SDRAM_CSn       : std_logic := '1';
    signal SDRAM_RASn      : std_logic := '1';
    signal SDRAM_CASn      : std_logic := '1';
    signal SDRAM_WEn       : std_logic := '1';
    signal SDRAM_DQM       : std_logic := '0';
    signal SDRAM_A         : std_logic_vector(12 downto 0);
    signal SDRAM_BA        : std_logic_vector(1 downto 0);
    signal SDRAM_DQ        : std_logic_vector(7 downto 0) := (others => 'Z');
    signal MOTOR_LEDn      : std_logic;
    signal DISK_ACTn       : std_logic; 

begin
    CLOCK_50 <= not CLOCK_50 after 10 ns;

    fpga: entity work.fpga_mem_test_v5
    port map (
        CLOCK_50   => CLOCK_50,
        SDRAM_CLK  => SDRAM_CLK,
        SDRAM_CKE  => SDRAM_CKE,
        SDRAM_CSn  => SDRAM_CSn,
        SDRAM_RASn => SDRAM_RASn,
        SDRAM_CASn => SDRAM_CASn,
        SDRAM_WEn  => SDRAM_WEn,
        SDRAM_DQM  => SDRAM_DQM,
        SDRAM_A    => SDRAM_A,
        SDRAM_BA   => SDRAM_BA,
        SDRAM_DQ   => SDRAM_DQ,
        MOTOR_LEDn => MOTOR_LEDn,
        DISK_ACTn  => DISK_ACTn );
    
end arch;

