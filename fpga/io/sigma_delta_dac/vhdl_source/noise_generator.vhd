library ieee;
use ieee.std_logic_1164.all;

entity noise_generator is
generic (
    g_type          : string := "Fibonacci"; -- can also be "Galois"
    g_polynom       : std_logic_vector := X"E10000";
    g_fixed_polynom : boolean := true;
    g_seed          : std_logic_vector := X"000001" );
port (
    clock           : in  std_logic;
    enable          : in  std_logic;
    reset           : in  std_logic;
    polynom         : in  std_logic_vector(g_polynom'length-1 downto 0) := (others => '0');
    q               : out std_logic_vector(g_polynom'length-1 downto 0) );
end noise_generator;

architecture gideon of noise_generator is
    signal c_poly   : std_logic_vector(g_polynom'length-1 downto 0);
    signal reg      : std_logic_vector(g_polynom'length-1 downto 0);
begin
    assert (g_type = "Fibonacci") or (g_type = "Galois")
        report "Type of LFSR should be Fibonacci or Galois.."
        severity failure;
    
    c_poly <= g_polynom when g_fixed_polynom else polynom;    
    
    process(clock)
        variable new_bit  : std_logic;
    begin
        if rising_edge(clock) then
            if enable='1' then
                if g_type = "Fibonacci" then
                    new_bit := '0';
                    for i in c_poly'range loop
                        if c_poly(i)='1' then
                            new_bit := new_bit xor reg(i);
                        end if;
                    end loop;
                    reg <= reg(reg'high-1 downto 0) & new_bit;
                else -- "Galois", enforced by assert
                    if reg(reg'high)='1' then
                        reg <= (reg(reg'high-1 downto 0) & '0') xor c_poly;
                    else
                        reg <=  reg(reg'high-1 downto 0) & '1';
                    end if;
                end if;
            end if;
            
            if reset='1' then
                reg <= g_seed;
            end if;
        end if;
    end process;
    q <= reg;
end gideon;
