
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dpram is
generic (
    g_ram_size  : natural := 13 );
port (
    clock_a     : in  std_logic;
    enable_a    : in  std_logic;
    write_a     : in  std_logic;
    address_a   : in  unsigned(g_ram_size-1 downto 2);
    din_a       : in  unsigned(31 downto 0);
    dout_a      : out unsigned(31 downto 0);
    
    clock_b     : in  std_logic;
    enable_b    : in  std_logic;
    write_b     : in  std_logic;
    address_b   : in  unsigned(g_ram_size-1 downto 2);
    din_b       : in  unsigned(31 downto 0);
    dout_b      : out unsigned(31 downto 0) );

    attribute keep_hierarchy    : string;
    attribute keep_hierarchy    of dpram  : entity is "yes";

end dpram;

architecture rtl of dpram is

    constant c_num_words    : integer := 2 ** (g_ram_size - 2);
    type t_ram    is array (0 to c_num_words-1) of unsigned(31 downto 0);

    shared variable ram:    t_ram := (others => X"00000000");

    attribute ram_style           : string;
    attribute ram_style of ram    : variable is "block";
begin

    p_port_a: process (clock_a)
    begin
        if rising_edge(clock_a) then
            if enable_a = '1' then
                if write_a = '1' then
                    ram(to_integer(address_a)) := din_a;
-- synthesis translate_off
                    for i in din_a'range loop
                        assert din_a(i) = '0' or din_a(i) = '1' 
                            report "Bit " & integer'image(i) & " is not 0 or 1."
                            severity failure;
                    end loop;
-- synthesis translate_on
                end if;
                dout_a <= ram(to_integer(address_a));
            end if;
        end if;
    end process;

    p_port_b: process (clock_b)
    begin
        if rising_edge(clock_b) then
            if enable_b = '1' then
                if write_b = '1' then
                    ram(to_integer(address_b)) := din_b;
-- synthesis translate_off
                    for i in din_b'range loop
                        assert din_b(i) = '0' or din_b(i) = '1' 
                            report "Bit " & integer'image(i) & " is not 0 or 1, writing B."
                            severity failure;
                    end loop;
-- synthesis translate_on
                end if;
                dout_b <= ram(to_integer(address_b));
            end if;
        end if;
    end process;
    
end architecture;
