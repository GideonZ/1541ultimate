library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library mblite;
use mblite.config_Pkg.all;
use mblite.core_Pkg.all;
use mblite.std_Pkg.all;

library work;
use work.tl_string_util_pkg.all;

library std;
use std.textio.all;

entity mblite_simu_cached is

end entity;

architecture test of mblite_simu_cached is

    signal clock  : std_logic := '0';
    signal reset  : std_logic;

    signal mmem_o : dmem_out_type;
    signal mmem_i : dmem_in_type;
    signal irq_i  : std_logic := '0';
    signal irq_o  : std_logic;

    signal invalidate   : std_logic := '0';
    signal inv_addr     : std_logic_vector(31 downto 0) := X"00003FFC";
    
    type t_mem_array is array(natural range <>) of std_logic_vector(31 downto 0);
    
    shared variable memory  : t_mem_array(0 to 1048575) := (others => (others => '0')); -- 4MB

BEGIN
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_core: entity work.cached_mblite
    port map (
        clock      => clock,
        reset      => reset,
        invalidate => invalidate,
        inv_addr   => inv_addr,
        mmem_o     => mmem_o,
        mmem_i     => mmem_i,
        irq_i      => irq_i,
        irq_o      => irq_o  );
    
    -- IRQ generation @ 100 kHz (every 10 us)
    process
    begin
        for i in 1 to 50 loop
            wait for 10 us;
            wait until clock='1';
            irq_i <= '1';
            wait until clock='1';
            irq_i <= '0';
        end loop;
        wait;        
    end process;

    process
    begin
        wait until reset='0';
        wait until clock='1';
        wait until clock='1';
        while true loop
            invalidate <= '0';
            wait until clock='1';
            invalidate <= '0';
            wait until clock='1';
            wait until clock='1';
        end loop;        
    end process;

    -- memory and IO
    process(clock)
        variable s    : line;
        variable char : character;
        variable byte : std_logic_vector(7 downto 0);
    begin
        if rising_edge(clock) then
            mmem_i.dat_i <= (others => 'X');
            if mmem_o.ena_o = '1' then
                if mmem_o.adr_o(31 downto 25) = "0000000" then
                    if mmem_o.we_o = '1' then
                        for i in 0 to 3 loop
                            if mmem_o.sel_o(i) = '1' then
                                memory(to_integer(unsigned(mmem_o.adr_o(21 downto 2))))(i*8+7 downto i*8) := mmem_o.dat_o(i*8+7 downto i*8);
                            end if;
                        end loop;
                    else -- read
                        mmem_i.dat_i <= memory(to_integer(unsigned(mmem_o.adr_o(21 downto 2))));
                    end if;
                else -- I/O
                    if mmem_o.we_o = '1' then -- write
                        case mmem_o.adr_o(19 downto 0) is
                        when X"00000" => -- interrupt
                            null;
                        when X"00010" => -- UART_DATA
                            byte := mmem_o.dat_o(31 downto 24);
                            char := character'val(to_integer(unsigned(byte)));
                            if byte = X"0D" then
                                -- Ignore character 13
                            elsif byte = X"0A" then
                                -- Writeline on character 10 (newline)
                                writeline(output, s);
                            else
                                -- Write to buffer
                                write(s, char);
                            end if;
                        when others =>
                            report "I/O write to " & hstr(mmem_o.adr_o) & " dropped";
                        end case;
                    else -- read
                        case mmem_o.adr_o(19 downto 0) is
                        when X"0000C" => -- Capabilities
                            mmem_i.dat_i <= X"00000002";
                        when X"00012" => -- UART_FLAGS
                            mmem_i.dat_i <= X"40404040";
                        when X"2000A" => -- 1541_A memmap
                            mmem_i.dat_i <= X"3F3F3F3F";
                        when X"2000B" => -- 1541_A audiomap
                            mmem_i.dat_i <= X"3E3E3E3E";
                        when others =>
                            report "I/O read to " & hstr(mmem_o.adr_o) & " dropped";
                            mmem_i.dat_i <= X"00000000";
                        end case;
                    end if;
                end if;
            end if;

            if reset = '1' then
                mmem_i.ena_i <= '1';
            end if;
        end if;
    end process;
    
end architecture;
