library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.pkg_6502_defs.all;
use work.tl_flat_memory_model_pkg.all;

entity tb_proc_core is
generic (
    test_file : string := "opcode_test";
    test_base : integer := 16#02FE# );
end tb_proc_core;

architecture tb of tb_proc_core is

    signal clock        : std_logic := '0';
    signal clock_en     : std_logic;
    signal reset        : std_logic;
    signal addr_out     : std_logic_vector(16 downto 0);
    signal data_in      : std_logic_vector(7 downto 0) := X"AA";
    signal data_out     : std_logic_vector(7 downto 0);
    signal read_write_n : std_logic;
    signal nmi_n        : std_logic := '1';
    signal irq_n        : std_logic := '1';
    signal stop_clock   : boolean := false;
    shared variable ram : h_mem_object;
    signal brk          : std_logic := '0';
begin

    clock    <= not clock after 50 ns when not stop_clock;
    clock_en <= '1';
    reset    <= '1', '0' after 300 ns;

    core: entity work.proc_core
    generic map (
        support_bcd  => true )
    port map (
        clock        => clock,
        clock_en     => clock_en,
        reset        => reset,

        irq_n        => irq_n,
        nmi_n        => nmi_n,
    
        addr_out     => addr_out,
        data_in      => data_in,
        data_out     => data_out,
        read_write_n => read_write_n );

    process(clock)
        variable addr : std_logic_vector(31 downto 0) := (others => '0');
    begin
        if falling_edge(clock) then
            addr(15 downto 0) := addr_out(15 downto 0);
            data_in <= read_memory_8(ram, addr);
            if read_write_n = '0' then
                write_memory_8(ram, addr, data_out);
--                if addr_out(15 downto 0) = X"FFF8" then
--                    stop_clock <= true;
--                    if data_out = X"55" then
--                        report "Test program completed successfully." severity note;
--                    else
--                        report "Test program failed." severity error;
--                    end if;
--                elsif addr_out(15 downto 0) = X"FFF9" then
--                    case data_out is
--                    when X"01" =>  report "Break IRQ service routine." severity note;
--                    when X"02" =>  report "External IRQ service routine." severity note;
--                    when X"03" =>  report "NMI service routine." severity note;
--                    when others => report "Unknown event message." severity warning;
--                    end case;
--                end if;                       
            else -- read
                if addr_out = "11111111111111110" then -- vector for BRK
                    brk <= '1';
                end if;
            end if;
        end if;
    end process;

    test: process
    begin
        register_mem_model(tb_proc_core'path_name, "6502 ram", ram);        
        load_memory("bootstrap", ram, X"0000FFEE");
        load_memory(test_file, ram, std_logic_vector(to_unsigned(test_base, 32)));
        wait until brk = '1';
        save_memory("result", ram, X"00000000", 2048);
        stop_clock <= true;
--        wait for 30 us;
--        irq_n <= '0';
--        wait for 10 us;
--        irq_n <= '1';
--        wait for 10 us;
--        nmi_n <= '0';
--        wait for 10 us;
--        nmi_n <= '1';
        wait;
    end process;

end tb;
