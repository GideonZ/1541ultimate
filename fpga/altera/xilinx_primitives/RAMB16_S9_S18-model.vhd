library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

architecture model of RAMB16_S9_S18 is
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    shared variable mem : t_byte_array(0 to 2047) := (others => X"00");
begin
    process(CLKA)
    begin
        if rising_edge(CLKA) then
            if ENA = '1' then
                DOA <= mem(to_integer(unsigned(ADDRA)));
                if WEA = '1' then
                    mem(to_integer(unsigned(ADDRA))) := DIA;
                end if;
            end if;
        end if;
    end process;
    
    process(CLKB)
        variable addr : natural;
    begin
        if rising_edge(CLKB) then
            if ENB = '1' then
                addr := 2 * to_integer(unsigned(ADDRB));
                DOB <= mem(addr + 1) & mem(addr);
                if WEB = '1' then
                    mem(addr) := DIB(7 downto 0);
                    mem(addr + 1) := DIB(15 downto 8);
                end if;
            end if;
        end if;
    end process;
end architecture;
