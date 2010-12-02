library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mash is
generic (
    g_order     : positive := 2;
    g_width     : positive := 16 );
port (
    clock       : in  std_logic;
    enable      : in  std_logic := '1';
    reset       : in  std_logic;
    
    dac_in      : in  unsigned(g_width-1 downto 0);
    dac_out     : out integer range 0 to (2**g_order)-1);
end entity;

architecture gideon of mash is
    type t_accu_array   is array(natural range <>) of unsigned(g_width-1 downto 0);
    signal accu         : t_accu_array(0 to g_order-1);
    signal carry        : std_logic_vector(0 to g_order-1);
    signal sum          : t_accu_array(0 to g_order-1);
    subtype t_delta_range is integer range -(2**(g_order-1)) to (2**(g_order-1));
    type t_int_array    is array(natural range <>) of t_delta_range;
    signal delta        : t_int_array(0 to g_order-1) := (others => 0);
    signal delta_d      : t_int_array(0 to g_order-1) := (others => 0);
    
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

    function count_deltas(a : std_logic; b : t_delta_range; c : t_delta_range) return t_delta_range is
    begin
        if a = '1' then
            return 1 + b - c;
        end if;
        return b - c;
    end function;
begin
    process(accu, dac_in, carry, delta, delta_d, sum)
        variable a : unsigned(dac_in'range);
        variable y : unsigned(dac_in'range);
        variable c : std_logic;
    begin
        for i in 0 to g_order-1 loop
            if i=0 then
                a := dac_in;
            else
                a := sum(i-1);
            end if;
            sum_with_carry(a, accu(i), y, c);
            sum(i) <= y;
            carry(i) <= c;

            if i = g_order-1 then
                delta(i) <= count_deltas(carry(i), 0, 0);
            else
                delta(i) <= count_deltas(carry(i), delta(i+1), delta_d(i+1));
            end if;
        end loop;
    end process;

    dac_out <= delta_d(0) + (2 ** (g_order-1) - 1);

    process(clock)
    begin
        if rising_edge(clock) then
            if enable='1' then
                accu    <= sum;
                delta_d <= delta;
            end if;
            
            if reset='1' then
                accu    <= (others => (others => '0'));
                delta_d <= (others => 0);
            end if;
        end if;
    end process;
end gideon;
    