--------------------------------------------------------------------------------
-- Entity: tape_speed_control_tb
-- Date:2016-04-17  
-- Author: Gideon     
--
-- Description: Testbench
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity tape_speed_control_tb is

end tape_speed_control_tb;

architecture arch of tape_speed_control_tb is
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '0';
    signal tick_out : std_logic;
    signal motor_en : std_logic;
    signal clock_stop : boolean := false;
begin
    clock <= not clock after 10 ns when not clock_stop;
    reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.tape_speed_control
    port map (
        clock    => clock,
        reset    => reset,
        motor_en => motor_en,
        tick_out => tick_out
    );
    
    p_test: process
    begin
        motor_en <= '0';
        wait for 1 ms;
        motor_en <= '1';
        wait for 199 ms;
        motor_en <= '0';
        wait for 400 ms;
        clock_stop <= true;
    end process;    
end arch;
