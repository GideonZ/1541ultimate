library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- LUT/FF/S3S/BRAM: 242/148/132/1

library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.tl_string_util_pkg.all;

entity c2n_record_tb is

end c2n_record_tb;

architecture tb of c2n_record_tb is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal req             : t_io_req;
    signal resp            : t_io_resp;
    signal phi2_tick       : std_logic := '0';
    signal c64_stopped	   : std_logic := '0';
    signal c2n_motor       : std_logic := '0';
    signal c2n_sense       : std_logic := '0';
    signal c2n_read        : std_logic := '0';
    signal c2n_write       : std_logic := '0';

    type t_int_vec is array(natural range <>) of integer;
    constant c_test_vector : t_int_vec := ( 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53,
                                            100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
                                            2000, 2010, 2020, 2030, 2040, 2041, 2042, 2043, 2044, 2045,
                                            2046, 2046, 2046, 2047, 2047, 2047, 2047, 2047, 2047, 2048,
                                            2048, 2048, 2048, 2048, 2048, 2050, 2060, 2070, 2080,
                                            2090, 2090, 2090, 2090, 2090, 2090, 2090, 2090 );
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.c2n_record
    port map (
        clock           => clock,
        reset           => reset,
                                      
        req             => req,
        resp            => resp,
                                      
        phi2_tick       => phi2_tick,
        c64_stopped		=> c64_stopped,
                                      
        c2n_motor       => c2n_motor,
        c2n_sense       => c2n_sense,
        c2n_read        => c2n_read,
        c2n_write       => c2n_write );

    i_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => clock,
    
        req         => req,
        resp        => resp );

    process(clock)
        variable count : integer := 0;
    begin
        if rising_edge(clock) then
            if count = 9 then
                count := 0;
                phi2_tick <= '1';
            else
                count := count + 1;
                phi2_tick <= '0';
            end if;            
        end if;
    end process;
    
    p_generate: process
    begin
--        for i in 0 to 511 loop
--            wait for 3200 ns; -- 02
--            c2n_read <= '1', '0' after 600 ns;
--        end loop;
        
        for i in c_test_vector'range loop
            wait for c_test_vector(i) * 200 ns; -- 200 ns is one phi2_tick.
            c2n_read <= '1', '0' after 600 ns;
        end loop;
        wait;        
    end process;
    
    p_test: process
        variable io : p_io_bus_bfm_object;
        variable data : std_logic_vector(7 downto 0);
        variable received : integer := 0;
    begin
        wait until reset='0';
        bind_io_bus_bfm("io_bfm", io);

        io_write(io, X"00", X"01"); -- enable rising edge recording
        wait for 1 us;
        c2n_sense <= '1';
        wait for 1 us;
        c2n_motor <= '1';

        while true loop
            io_read(io, X"00", data);
            if data(7)='1' then
                io_read(io, X"800", data);
                report hstr(data);
                received := received + 1;
                if received = 50 then
                    wait for 10 us;
                    io_write(io, X"00", X"00"); -- disable
                    wait;
                end if;
            end if;
        end loop;
        
        wait;
    end process;
    
end tb;
