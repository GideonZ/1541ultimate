-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2016 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;

entity lp_filter is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    signal_in   : in  signed(17 downto 0);
    
    high_pass   : out signed(17 downto 0);
    band_pass   : out signed(17 downto 0);
    low_pass    : out signed(17 downto 0);
    error_out   : out std_logic;
    valid_out   : out std_logic );
        
end entity;

architecture dsvf of lp_filter is
    --signal filter_q     : signed(signal_in'range);
    --signal filter_f     : signed(signal_in'range);
    signal input_sc     : signed(signal_in'range);
    signal reg_a        : signed(25 downto 0);
    signal reg_b        : signed(25 downto 0);
begin
    --filter_q <= to_signed(65536 + 16384, filter_q'length); -- 92682
    --filter_f <= to_signed(   64, filter_f'length);
    input_sc <= signal_in; -- shift_right(signal_in, 1);
    
    process(clock)
        variable af, vq, lp, hp, hpf, bp : signed(25 downto 0);

        function mult2_24 (xa, xb : signed(17 downto 0)) return signed is
            variable result : signed(35 downto 0);
        begin
            result := xa * xb;
            return result(33 downto 8);
        end function;
    begin
        if rising_edge(clock) then
            -- af := mult2_24(filter_f, reg_a(25 downto 8)); -- 26 bit
            -- *64 in 2_16 format is actually shifting to the right by (16-6=) 10 bits
            af := shift_right(reg_a, 10); 
            lp := af + reg_b; -- 26 bit

            -- vq := mult2_24(filter_q, reg_a(25 downto 8)); -- 26 bit
            -- * 65536+16384 = *1.125 = addition with its shifted self.
            vq := reg_a + shift_right(reg_a, 2);
            hp := (input_sc & X"00") - vq - lp;
            -- hpf := mult2_24(filter_f, hp(25 downto 8));
            hpf := shift_right(hp, 10);
            bp  := hpf + reg_a;
            
            reg_a <= bp;
            reg_b <= lp;
            low_pass <= lp(25 downto 8);
            band_pass <= bp(25 downto 8);
            high_pass <= hp(25 downto 8);
            valid_out <= '1';            

            if reset = '1' then
                reg_a <= (others => '0');
                reg_b <= (others => '0');
                valid_out <= '0';            
            end if;
        end if;
    end process;

    error_out <= '0';
    
end dsvf;
