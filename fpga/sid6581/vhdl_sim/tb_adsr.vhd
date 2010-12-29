-------------------------------------------------------------------------------
-- Date       $Date: 2005/04/12 19:09:27 $
-- Author     $Author: Gideon $
-- Revision   $Revision: 1.1 $
-- Log        $Log: oscillator.vhd,v $
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity tb_adsr is
end tb_adsr;

architecture tb of tb_adsr is

    signal clk      : std_logic := '0';
    signal reset    : std_logic;
    signal gate     : std_logic;
    signal attack   : std_logic_vector(3 downto 0);
    signal decay    : std_logic_vector(3 downto 0);
    signal sustain  : std_logic_vector(3 downto 0);
    signal release  : std_logic_vector(3 downto 0);
    signal env_out  : std_logic_vector(7 downto 0);
    signal env_state: std_logic_vector(1 downto 0);
begin
    clk <= not clk after 500 ns;

    mut: entity work.adsr
    port map(
        clk      => clk,
        reset    => reset,
    
        gate     => gate,
        attack   => X"5",
        decay    => X"7",
        sustain  => X"A",
        release  => X"5",
        
        env_state=> env_state,
        env_out  => env_out );
        
    reset <= '1', '0' after 20 us;
    
    process
    begin
        gate <= '0';
        wait for 100 us;
        gate <= '1';
        wait for 150 ms;
        gate <= '0';
        wait;
    end process;
end tb;
