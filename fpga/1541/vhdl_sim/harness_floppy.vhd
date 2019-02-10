-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : harness_floppy
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Simulation wrapper for floppy
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library std;
    use std.textio.all;
    
library work;
    use work.tl_string_util_pkg.all;    
    use work.rocket_pkg.all;
    
entity harness_floppy is

end entity;

architecture Harness of harness_floppy is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal tick_16MHz      : std_logic;
    signal mem_rdata       : std_logic_vector(7 downto 0) := X"AA";
    signal do_read         : std_logic;
    signal do_write        : std_logic;
    signal do_advance      : std_logic;
    signal floppy_inserted : std_logic := '1';
    signal motor_on        : std_logic := '1';
    signal sync            : std_logic;
    signal mode            : std_logic := '1';
    signal write_prot_n    : std_logic := '1';
    signal step            : std_logic_vector(1 downto 0) := "00";
    signal byte_ready      : std_logic;
    signal soe             : std_logic := '1';
    signal rate_ctrl       : std_logic_vector(1 downto 0) := "11";
    signal read_data       : std_logic_vector(7 downto 0);
    signal read_data_d     : std_logic_vector(7 downto 0) := X"FF";

    type t_bytes is array(natural range <>) of std_logic_vector(7 downto 0);
    constant input : t_bytes := ( X"FF", X"FF", X"55", X"55", X"00", X"00", X"00", X"00", X"00", X"00", X"00", 
                                  X"00", X"00", X"00", X"00", X"55", X"F7", X"54", X"55", X"55", X"55", X"55",
                                  X"00", X"00", X"00", X"00", X"00", X"00", X"00", X"00", X"00", X"00", X"AA" );

--    constant input : t_bytes := (
--        X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", X"55", 
--        X"55", X"55", X"55", X"55", X"55", X"55", X"5F", X"55", X"55", X"F5", X"25", X"4B", X"52", X"94", X"B5", X"29", 
--        X"4A", X"52", X"94", X"A4", X"A4", X"A4", X"A4", X"A4", X"A4", X"A4", X"A4", X"AB", X"BB", X"FF", X"FF", X"FF", 
--        X"FF", X"FF", X"FD", X"57", X"56", X"A9", X"24", X"00", X"02", X"AB", X"53", X"BB", X"BB", X"BB", X"BB", X"BB", 
--        X"BB", X"BB", X"BA", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", 
--        X"A9", X"6A", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", X"A9", X"6A", X"5A", X"96", X"A5", X"A9" ); 


    --constant bytes_per_track : natural := rocket_array'length;
    constant bytes_per_track : natural := 7756;
    constant bits_per_track  : natural := 8 * bytes_per_track;
    constant half_clocks_per_track : natural := 100_000_000 / 5; -- 5 RPS = 300 RPM
    signal bit_time        : unsigned(9 downto 0) := to_unsigned(half_clocks_per_track / bits_per_track, 10); -- steps of 10 ns (half clocks)

begin
    clock <= not clock after 10 ns; -- 50 MHz
    reset <= '1', '0' after 100 ns;
    
    i_div: entity work.fractional_div
    generic map (
        g_numerator   => 8,
        g_denominator => 25
    )
    port map (
        clock         => clock,
        tick          => tick_16MHz
    );

    i_floppy: entity work.floppy_stream
    port map (
        clock           => clock,
        reset           => reset,
        tick_16MHz      => tick_16MHz,
        mem_rdata       => mem_rdata,
        do_read         => do_read,
        do_write        => do_write,
        do_advance      => do_advance,
        floppy_inserted => floppy_inserted,
        motor_on        => motor_on,
        sync            => sync,
        mode            => mode,
        write_prot_n    => write_prot_n,
        step            => step,
        byte_ready      => byte_ready,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        bit_time        => bit_time,
        read_data       => read_data
    );

    process
    begin
        for i in input'range loop
            mem_rdata <= input(i);
            wait until do_read = '1';
        end loop;
    end process;

    process(byte_ready)
        variable last : time := 0.0 ns;
    begin
        if falling_edge(byte_ready) then
            read_data_d <= read_data;
--            report hstr(read_data) & " after " & time'image(now - last);
            last := now;
        end if;
    end process;

    process
        variable L : line;
        variable limit : natural := 0;
    begin
        wait until byte_ready = '0' or sync = '0';
        if sync = '0' then
            writeline(output, L);
            write(L, "sync ");
            limit := 0;
        end if;
        if byte_ready = '0' then
            limit := limit + 1;
            if limit <= 32 then
                write(L, hstr(read_data) & " ");
            end if;
        end if;
    end process;

end architecture;
