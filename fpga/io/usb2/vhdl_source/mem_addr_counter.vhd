library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mem_addr_counter is
port (
    clock       : in  std_logic;
    load_value  : in  unsigned(25 downto 0);
    do_load     : in  std_logic;
    do_inc      : in  std_logic;
    inc_by_4    : in  std_logic;
    
    address     : out unsigned(25 downto 0) );
end mem_addr_counter;

architecture test of mem_addr_counter is
    signal addr_i   : unsigned(address'range) := (others => '0');
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if do_load='1' then
                addr_i <= load_value;
            elsif do_inc='1' then
                if inc_by_4='1' then
                    addr_i <= addr_i + 4;
                else
                    addr_i <= addr_i + 1;
                end if;
            end if;
        end if;
    end process;

    address <= addr_i;
    
end architecture;
