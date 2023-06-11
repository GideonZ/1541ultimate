
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.core_pkg.all;

library work;
use work.tl_string_util_pkg.all;

library std;
use std.textio.all;

entity tb_core is

end entity;

architecture run of tb_core is
    signal imem_o      : imem_out_type;
    signal imem_i      : imem_in_type;
    signal dmem_o      : dmem_out_type;
    signal dmem_i      : dmem_in_type;
    signal int_i       : std_logic := '0';
    signal rst_i       : std_logic;
    signal clk_i       : std_logic := '0';

    signal imem_delay  : natural := 2;
    signal dmem_delay  : natural := 4;
	signal last_char   : character;
    signal hellos      : natural := 0;
    signal interrupts  : natural := 0;

    type t_mem_array is array(natural range <>) of std_logic_vector(31 downto 0);
    shared variable memory  : t_mem_array(0 to 1048575) := (others => (others => '0')); -- 4MB
begin
    clk_i <= not clk_i after 10 ns;

    i_core: entity work.core
    port map (
        imem_o => imem_o,
        dmem_o => dmem_o,
        imem_i => imem_i,
        dmem_i => dmem_i,
        int_i  => int_i,
        rst_i  => rst_i,
        clk_i  => clk_i
    );

    -- memory and IO
    process(clk_i)
        variable s      : line;
        variable char   : character;
        variable byte   : std_logic_vector(7 downto 0);
        variable imem_l : imem_out_type;
        variable imem_count : natural;
        variable dmem_l : dmem_out_type;
        variable dmem_count : natural;
    begin
        if rising_edge(clk_i) then
            if rst_i = '1' then
                int_i <= '0';
                interrupts <= 0;
            end if;
            if imem_i.ena_i = '1' then
                if imem_o.ena_o = '1' then
                    imem_l := imem_o;
                    imem_count := imem_delay;
                end if;
            end if;
            if imem_count /= 0 then
                imem_count := imem_count - 1;
                imem_i.ena_i <= '0';
            else
                imem_i.ena_i <= '1';
                imem_i.dat_i <= memory(to_integer(unsigned(imem_l.adr_o(21 downto 2))));
            end if;

            if dmem_i.ena_i = '1' then
                if dmem_o.ena_o = '1' then
                    dmem_l := dmem_o;
                    dmem_count := dmem_delay;
                end if;
            end if;
            if dmem_count /= 0 then
                dmem_count := dmem_count - 1;
                dmem_i.ena_i <= '0';
            else
                dmem_i.ena_i <= '1';
                if dmem_l.adr_o(31 downto 25) = "0000000" and dmem_l.ena_o = '1' then
                    if dmem_l.we_o = '1' then
                        -- report "Writing " & hstr(dmem_l.dat_o) & " to " & hstr(dmem_l.adr_o);
                        for i in 0 to 3 loop
                            if dmem_l.sel_o(i) = '1' then
                                memory(to_integer(unsigned(dmem_l.adr_o(21 downto 2))))(i*8+7 downto i*8) := dmem_l.dat_o(i*8+7 downto i*8);
                            end if;
                        end loop;
                    else -- read
                        dmem_i.dat_i <= memory(to_integer(unsigned(dmem_l.adr_o(21 downto 2))));
                        -- report "Reading " & hstr(memory(to_integer(unsigned(dmem_l.adr_o(21 downto 2))))) & " from " & hstr(dmem_l.adr_o);
                    end if;
                elsif dmem_l.ena_o = '1' then -- I/O
                    if dmem_l.we_o = '1' then -- write
                        case dmem_l.adr_o(19 downto 0) is
                        when X"0001F" => -- interrupt
                            interrupts <= interrupts + 1;
                        when X"00010" => -- UART_DATA
                            byte := dmem_l.dat_o(31 downto 24);
                            char := character'val(to_integer(unsigned(byte)));
							last_char <= char;
                            if byte = X"0D" then
                                -- Ignore character 13
                            elsif byte = X"0A" then
                                -- Writeline on character 10 (newline)
                                if s.all = "Hello world! :-)" then
                                    hellos <= hellos + 1;
                                end if;
                                writeline(output, s);
                                int_i <= '1' after 5 us;
                            else
                                -- Write to buffer
                                write(s, char);
                            end if;
                        when others =>
                            report "I/O write to " & hstr(dmem_l.adr_o) & " dropped";
                        end case;
                    else -- read
                        case dmem_l.adr_o(19 downto 0) is
                        when X"0000C" => -- Capabilities
                            dmem_i.dat_i <= X"00000002";
                        when X"00012" => -- UART_FLAGS
                            dmem_i.dat_i <= X"40404040";
                        when X"2000A" => -- 1541_A memmap
                            dmem_i.dat_i <= X"3F3F3F3F";
                        when X"2000B" => -- 1541_A audiomap
                            dmem_i.dat_i <= X"3E3E3E3E";
                        when others =>
                            report "I/O read to " & hstr(dmem_l.adr_o) & " dropped";
                            dmem_i.dat_i <= X"00000000";
                        end case;
                    end if;
                end if;
                dmem_l.ena_o := '0';
            end if;

            if rst_i = '1' then
                imem_i.ena_i <= '1';
                dmem_i.ena_i <= '1';
            end if;
        end if;
    end process;
    
    process
    begin
        for i in 0 to 4 loop
            for d in 0 to 4 loop
                imem_delay <= i;
                dmem_delay <= d;
                rst_i <= '1';
                wait for 100 ns;
                rst_i <= '0';
                wait for 20 us * (i+1) + 5 us * (d+1);
                assert interrupts > 0
                    report "Core failed to interrupt"
                    severity failure;
            end loop;
        end loop;
        assert hellos = 25
            report "Core failed to print hello world 25 times"
            severity failure;
        wait;
    end process;

end architecture;
