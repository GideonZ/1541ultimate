library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;

entity pdm_with_volume is
generic (
    g_width         : positive := 12;
    g_invert        : boolean := false;
    g_use_mid_only  : boolean := true;
    g_left_shift    : natural := 1 );
port (
    clock   : in  std_logic;
    reset   : in  std_logic;
    
    volume  : in  std_logic_vector(3 downto 0) := X"F";
    dac_in  : in  signed(g_width-1 downto 0);
    dac_out : out std_logic );

end entity;

architecture gideon of pdm_with_volume is
    signal dac_in_scaled    : signed(g_width-1 downto g_left_shift);
    signal converted   : unsigned(g_width downto g_left_shift);
    signal out_i       : std_logic;
    signal accu        : unsigned(converted'range);
    signal divider     : integer range 0 to 15;
    signal pattern     : std_logic_vector(15 downto 0);

    type t_patterns is array(0 to 15) of std_logic_vector(15 downto 0);
--    constant c_patterns : t_patterns := (
--        0 => X"FF00", -- 8 ones and 8 zeros (off, but you may hear the unbalance)
--        1 => X"FF80", -- 9 ones and 7 zeros
--        2 => X"FFC0", -- 10 ones and 6 zeros
--        3 => X"FFE0", -- 11 ones and 5 zeros
--        4 => X"FFF0", -- 12 ones and 4 zeros
--        5 => X"FFF8", -- 13 ones and 3 zeros
--        6 => X"FFFC", -- 14 ones and 2 zeros
--        7 => X"FFFE", -- 15 ones and 1 zero
--        8 => X"FFFF", -- 16 ones and 0 zeros
--        9 => X"FF08", -- experimental 8 ones 7 zeros (short '1')         
--       10 => X"F88F", -- experimental 9 ones 6 zeros (short '1')
--       11 => X"FC0F", -- 10 ones and 6 zeros shifted
--       12 => X"FE0F", -- 11 ones and 5 zeros shifted
--       13 => X"FF0F", -- 12 ones and 4 zeros shifted
--       14 => X"FF8F", -- 13 ones and 3 zeros shifted
--       15 => X"FFCF"  -- 14 ones and 2 zeros shifted
--    );

    constant c_patterns : t_patterns := (
         1 => "1111000010001111", -- 9 ones and 7 zeros (single one separate)
         2 => "1111100000001111", -- 9 ones and 7 zeros (one extended)
         3 => "1111100010001111", -- 10 ones and 6 zeros (single one separate)
         4 => "1111100000011111", -- 10 ones and 6 zeros (one extended)
         5 => "1111110000011111", -- 11 ones and 5 zeros
         6 => "1111110000111111", -- 12 ones and 4 zeros
         7 => "1111111000111111", -- 13 ones and 3 zeros
         8 => "1111111001111111", -- 14 ones and 2 zeros
         9 => "1111111101111111", -- 15 ones and 1 zero
        10 => "1111111111111111", -- 16 ones and 0 zeros
    others => "1111000000001111"  -- 8 ones and 8 zeros (off, but you may hear the unbalance)
    );
begin
    dac_in_scaled <= left_scale(dac_in, g_left_shift) when rising_edge(clock);
    converted <= (not dac_in_scaled(dac_in_scaled'high) & unsigned(dac_in_scaled(dac_in_scaled'high downto g_left_shift))) when g_use_mid_only else
                 (not dac_in_scaled(dac_in_scaled'high) & unsigned(dac_in_scaled(dac_in_scaled'high-1 downto g_left_shift))) & '0';

    process(clock)
        procedure sum_with_carry(a, b : unsigned; y : out unsigned; c : out std_logic ) is
            variable a_ext  : unsigned(a'length downto 0);
            variable b_ext  : unsigned(a'length downto 0);
            variable summed : unsigned(a'length downto 0);
        begin
            a_ext := '0' & a;
            b_ext := '0' & b;
            summed := a_ext + b_ext;
            c := summed(summed'left);
            y := summed(a'length-1 downto 0);
        end procedure; 

        variable a_new  : unsigned(accu'range);
        variable carry  : std_logic;
    begin
        if rising_edge(clock) then
            if divider = 15 then
                if out_i = '0' then
                    pattern <= c_patterns(to_integer(unsigned(volume)));
                else
                    pattern <= not c_patterns(to_integer(unsigned(volume)));
                end if;
                divider <= 0;

                sum_with_carry(accu, converted, a_new, carry);
                accu <= a_new;
                if g_invert then
                    out_i <= not carry;
                else
                    out_i <= carry;
                end if;
            else
                divider <= divider + 1;
                pattern <= '0' & pattern(pattern'high downto 1);
            end if;
            
            if reset='1' then
                out_i     <= not out_i;
                accu      <= (others => '0');
                pattern   <= X"5555";
            end if;
        end if;
    end process;
    dac_out <= pattern(0);
end gideon;
