library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;

entity sigma_delta_dac is
generic (
    g_width         : positive := 12;
    g_invert        : boolean := false;
    g_use_mid_only  : boolean := true;
    g_left_shift    : natural := 1 );
port (
    clock   : in  std_logic;
    reset   : in  std_logic;
    
    dac_in  : in  signed(g_width-1 downto 0);
    dac_out : out std_logic );

end sigma_delta_dac;

architecture gideon of sigma_delta_dac is
--    signal converted   : unsigned(g_width-1 downto 0);
    signal dac_in_scaled    : signed(g_width-1 downto g_left_shift);
    signal converted   : unsigned(g_width downto g_left_shift);
    signal out_i       : std_logic;
    signal accu        : unsigned(converted'range);
    signal divider     : unsigned(2 downto 0) := "000";
    signal sine        : signed(15 downto 0);
    signal sine_enable : std_logic;
begin
    dac_in_scaled <= left_scale(dac_in, g_left_shift);
    converted <= (not dac_in_scaled(dac_in_scaled'high) & unsigned(dac_in_scaled(dac_in_scaled'high downto g_left_shift))) when g_use_mid_only else
                 (not dac_in_scaled(dac_in_scaled'high) & unsigned(dac_in_scaled(dac_in_scaled'high-1 downto g_left_shift))) & '0';

--    converted <= not sine(sine'high) & unsigned(sine(sine'high downto 0));

--    i_pilot: entity work.sine_osc
--    port map (
--        clock   => clock,
--        enable  => sine_enable,
--        reset   => reset,
--        
--        sine    => sine,
--        cosine  => open );
--
--    sine_enable <= '1' when divider="001" else '0';

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
            divider <= divider + 1;
            sum_with_carry(accu, converted, a_new, carry);
            accu <= a_new;
            if g_invert then
                out_i <= not carry;
            else
                out_i <= carry;
            end if;
            
            if reset='1' then
                out_i     <= not out_i;
                accu      <= (others => '0');
            end if;
        end if;
    end process;
    dac_out <= out_i;
end gideon;
