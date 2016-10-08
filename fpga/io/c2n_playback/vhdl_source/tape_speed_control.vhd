--------------------------------------------------------------------------------
-- Entity: tape_speed_control
-- Date:2016-04-17  
-- Author: Gideon     
--
-- Description: This module controls the tape speed, based on the motor pin.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tape_speed_control is
generic (
    g_clock_freq    : natural := 50_000_000 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    motor_en    : in  std_logic;
    tick_out    : out std_logic );    

end entity;

architecture arch of tape_speed_control is
    constant c_pdm_target       : natural := (985250 * 512) / (g_clock_freq / 128);  
    constant c_slowdown_ticks   : natural := 300 * (g_clock_freq / 1000); 
    constant c_speedup_ticks    : natural := 120 * (g_clock_freq / 1000);
    constant c_slowdown_divider : natural := c_slowdown_ticks / c_pdm_target;
    constant c_speedup_divider  : natural := c_speedup_ticks / c_pdm_target;  
    signal   divider            : natural range 0 to c_slowdown_divider - 1;
    signal   pdm_value          : unsigned(10 downto 0);
    signal   pdm_accu           : unsigned(15 downto 0);
begin
    process(clock)
        variable v_sum  : unsigned(16 downto 0);
    begin
        if rising_edge(clock) then
            if divider = 0 then
                if motor_en = '1' then
                    if pdm_value /= c_pdm_target then
                        pdm_value <= pdm_value + 1;
                        divider <= c_speedup_divider - 1;
                    end if;
                else
                    if pdm_value /= 0 then
                        pdm_value <= pdm_value - 1;
                        divider <= c_slowdown_divider - 1;
                    end if;
                end if;
            else
                divider <= divider - 1;
            end if;

            v_sum := ('0' & pdm_accu) + ('0' & pdm_value);
            tick_out <= v_sum(v_sum'high);
            pdm_accu <= v_sum(pdm_accu'range);

            if reset='1' then
                divider   <= 0;
                pdm_value <= (others => '0');
                pdm_accu  <= (others => '0');
            end if;
        end if;
    end process;
    
end arch;
