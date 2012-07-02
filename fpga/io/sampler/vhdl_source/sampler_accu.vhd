library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.sampler_pkg.all;
use work.my_math_pkg.all;

entity sampler_accu is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    first_chan  : in  std_logic;
    sample_in   : in  signed(15 downto 0);
    volume_in   : in  unsigned(5 downto 0);
    pan_in      : in  unsigned(3 downto 0);
    
    sample_L    : out signed(17 downto 0);
    sample_R    : out signed(17 downto 0);
    new_sample  : out std_logic );
end entity;

architecture gideon of sampler_accu is
--                 L    R
-- 0000 = left    111  000
-- 0001           111  001
-- 0010           111  010
-- ...
-- 0111 = mid     111  111
-- 1000 = mid     111  111
-- ...
-- 1101           010  111
-- 1110           001  111
-- 1111 = right   000  111

    signal pan_factor_L     : signed(3 downto 0);
    signal pan_factor_R     : signed(3 downto 0);
    signal sample_scaled    : signed(16 downto 0);
    signal first_scaled     : std_logic;
    signal accu_L           : signed(20 downto 0);
    signal accu_R           : signed(20 downto 0);
    signal current_L        : signed(20 downto 0);
    signal current_R        : signed(20 downto 0);

    attribute mult_style      : string; 
    attribute mult_style of current_L : signal is "lut"; 
    attribute mult_style of current_R : signal is "lut"; 
begin
    --pan_factor_L <= "01000" when pan_in(3)='0' else "00" & signed(not(pan_in(2 downto 0)));
    --pan_factor_R <= "01000" when pan_in(3)='1' else "00" & signed(    pan_in(2 downto 0));

    current_L <= sample_scaled * pan_factor_L;
    current_R <= sample_scaled * pan_factor_R;

    process(clock)
        variable temp : signed(22 downto 0);
    begin
        if rising_edge(clock) then
            -- stage 1
            temp := sample_in * ('0' & signed(volume_in));
            sample_scaled <= temp(21 downto 5);
            first_scaled <= first_chan;
            if pan_in(3)='0' then -- mostly left
                pan_factor_L <= "0111";
                pan_factor_R <= "0" & signed(pan_in(2 downto 0));
            else -- mostly right
                pan_factor_L <= "0" & signed(not pan_in(2 downto 0));
                pan_factor_R <= "0111";
            end if;
            
            -- stage 2
            if first_scaled='1' then
                sample_L <= accu_L(accu_L'high downto accu_L'high-17);
                sample_R <= accu_R(accu_R'high downto accu_R'high-17);
                new_sample <= '1';
                accu_L <= current_L;
                accu_R <= current_R;
            else
                new_sample <= '0';
                accu_L <= sum_limit(accu_L, current_L);
                accu_R <= sum_limit(accu_R, current_R);
            end if;
        end if;
    end process;
end architecture;
