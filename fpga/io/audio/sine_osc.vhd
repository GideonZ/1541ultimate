-------------------------------------------------------------------------------
-- Title      : sine_osc
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: I2S Serializer / Deserializer
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sine_osc is
port (
    clock   : in  std_logic;
    enable  : in  std_logic := '1';
    reset   : in  std_logic;
    
    freq    : in  unsigned(15 downto 0) := X"FFFF";

    sine    : out signed(19 downto 0);
    cosine  : out signed(19 downto 0) );
end sine_osc;

architecture gideon of sine_osc is
    signal enable_d : std_logic;
    signal cos_i    : signed(sine'range);
    signal sin_i    : signed(cosine'range);
    signal scaled_sin   : signed(sine'high + freq'high + 4 downto sine'high);
    signal scaled_cos   : signed(sine'high + freq'high + 4 downto sine'high);
begin
    process(clock)
        function scale(i: signed; f : unsigned) return signed is
            variable fs : signed(f'high+3 downto 0);
            variable o : signed(i'high + f'high + 4 downto 0);
        begin
            fs := "000" & signed(f);
            o := i * fs;
            return o(o'high downto o'high-i'high);
        end function;
        
        function sum_limit(i1, i2 : signed) return signed is
            variable o : signed(i1'range);
        begin
            o := i1 + i2;
            if (i1(i1'high) = i2(i2'high)) and (o(o'high) /= i1(i1'high)) then
                if i1(i1'high)='0' then
                    o := (others => '1');
                    o(o'left) := '0';
                else
                    o := (others => '0');
                    o(o'left) := '1';
                end if;
            end if;
            return o;            
        end function;

        function sub_limit(i1, i2 : signed) return signed is
            variable o : signed(i1'range);
        begin
            o := i1 - i2;
            if (i1(i1'high) /= i2(i2'high)) and (o(o'high) /= i1(i1'high)) then
                if i1(i1'high)='0' then
                    o := (others => '1');
                    o(o'left) := '0';
                else
                    o := (others => '0');
                    o(o'left) := '1';
                end if;
            end if;
            return o;    
        end function;
    begin
        if rising_edge(clock) then
            if reset='1' then
                sin_i <= (others => '0');
                cos_i <= (others => '1');
                cos_i(cos_i'left) <= '0';
                enable_d <= '0';
            else
                enable_d <= enable;
                if enable = '1' then
                    scaled_cos <= scale(cos_i, freq);
                    scaled_sin <= scale(sin_i, freq);
                end if;
                if enable_d = '1' then
                    sin_i <= sum_limit(scaled_cos, sin_i);
                    cos_i <= sub_limit(cos_i, scaled_sin);
                end if;
            end if;
        end if;
    end process;
    
    sine <= sin_i;
    cosine <= cos_i;
    
end gideon;
