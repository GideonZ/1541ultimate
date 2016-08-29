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
generic (
    g_divider   : natural := 221 );
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
    signal filter_q     : signed(signal_in'range);
    signal filter_f     : signed(signal_in'range);
    signal input_sc     : signed(signal_in'range);
    signal xa           : signed(signal_in'range);
    signal xb           : signed(signal_in'range);
    signal sum_b        : signed(signal_in'range);
    signal sub_a        : signed(signal_in'range);
    signal sub_b        : signed(signal_in'range);
    signal x_reg        : signed(signal_in'range) := (others => '0');
    signal bp_reg       : signed(signal_in'range);
    signal hp_reg       : signed(signal_in'range);
    signal lp_reg       : signed(signal_in'range);
    signal temp_reg     : signed(signal_in'range);
    signal error        : std_logic := '0';
    signal divider      : integer range 0 to g_divider-1;

    signal instruction  : std_logic_vector(7 downto 0);
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    constant c_program  : t_byte_array := (X"80", X"12", X"81", X"4C", X"82", X"20");

    alias  xa_select    : std_logic is instruction(0);
    alias  xb_select    : std_logic is instruction(1);
    alias  sub_a_sel    : std_logic is instruction(2);
    alias  sub_b_sel    : std_logic is instruction(3);
    alias  sum_to_lp    : std_logic is instruction(4);
    alias  sum_to_bp    : std_logic is instruction(5);
    alias  sub_to_hp    : std_logic is instruction(6);
    alias  mult_enable  : std_logic is instruction(7);

begin
--    -- Derive the actual 'f' and 'q' parameters
--    i_q_table: entity work.Q_table
--    port map (
--        Q_reg       => X"6",
--        filter_q    => filter_q ); -- 2.16 format

    filter_q <= to_signed(65536, filter_q'length); -- 92682
    filter_f <= to_signed(16384, filter_f'length);
    input_sc <= signal_in; -- shift_right(signal_in, 1);

    -- operations to execute the filter:
    -- bp_f      = f * bp_reg      
    -- q_contrib = q * bp_reg      
    -- lp        = bp_f + lp_reg   
    -- temp      = input - lp      
    -- hp        = temp - q_contrib
    -- hp_f      = f * hp          
    -- bp        = hp_f + bp_reg   
    -- bp_reg    = bp              
    -- lp_reg    = lp              

    -- x_reg     = f * bp_reg           -- 10000000 -- 80
    -- lp_reg    = x_reg + lp_reg       -- 00010010 -- 12
    -- q_contrib = q * bp_reg           -- 10000001 -- 81
    -- temp      = input - lp           -- 00000000 -- 00 (can be merged with previous!)
    -- hp_reg    = temp - q_contrib     -- 01001100 -- 4C
    -- x_reg     = f * hp_reg           -- 10000010 -- 82
    -- bp_reg    = x_reg + bp_reg       -- 00100000 -- 20

    
    -- now perform the arithmetic
    xa    <= filter_f when xa_select='0' else filter_q;
    xb    <= bp_reg   when xb_select='0' else hp_reg;
    sum_b <= bp_reg   when xb_select='0' else lp_reg;
    sub_a <= input_sc when sub_a_sel='0' else temp_reg;
    sub_b <= lp_reg   when sub_b_sel='0' else x_reg;
    
    process(clock)
        variable x_result   : signed(35 downto 0);
        variable sum_result : signed(17 downto 0);
        variable sub_result : signed(17 downto 0);
    begin
        if rising_edge(clock) then
            x_result := xa * xb;
            if mult_enable='1' then
                x_reg <= x_result(33 downto 16);
                if (x_result(35 downto 33) /= "000") and (x_result(35 downto 33) /= "111") then
                    error <= not error;
                end if;
            end if;

            sum_result := sum_limit(x_reg, sum_b);
            temp_reg   <= sum_result;
            if sum_to_lp='1' then
                lp_reg <= sum_result;
            end if;
            if sum_to_bp='1' then
                bp_reg <= sum_result;
            end if;
            
            sub_result := sub_limit(sub_a, sub_b);
            temp_reg   <= sub_result;
            if sub_to_hp='1' then
                hp_reg <= sub_result;
            end if;

            -- control part
            instruction <= (others => '0');
            if reset='1' then
                hp_reg <= (others => '0');            
                lp_reg <= (others => '0');            
                bp_reg <= (others => '0');            
                divider <= 0;
            elsif divider = g_divider-1 then
                divider <= 0;
            else
                divider <= divider + 1;
                if divider < c_program'length then
                    instruction <= c_program(divider);
                end if;
            end if;
            if divider = c_program'length then
                valid_out <= '1';
            else
                valid_out <= '0';
            end if;
        end if;
    end process;

    high_pass <= hp_reg;
    band_pass <= bp_reg;
    low_pass  <= lp_reg;
    error_out <= error;
end dsvf;
