library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

architecture model of pseudo_dpram_8x16_1k is
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    shared variable mem : t_byte_array(0 to 1023) := (others => X"00");
begin
    process(wr_clock)
    begin
        if rising_edge(wr_clock) then
            if wr_en = '1' then
                mem(to_integer(unsigned(wr_address))) := wr_data;
            end if;
        end if;
    end process;
    
    process(rd_clock)
        variable addr : natural;
    begin
        if rising_edge(rd_clock) then
            if rd_en = '1' then
                addr := 2 * to_integer(unsigned(rd_address));
                rd_data <= mem(addr + 1) & mem(addr);
            end if;
        end if;
    end process;
end architecture;
