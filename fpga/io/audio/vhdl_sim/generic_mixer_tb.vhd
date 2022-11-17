--------------------------------------------------------------------------------
-- Entity: generic_mixer_tb
-- Date:2018-08-02  
-- Author: gideon     
--
-- Description: Testbench for generic mixer
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.audio_type_pkg.all;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;

entity generic_mixer_tb is
end entity;

architecture tb of generic_mixer_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal start       : std_logic;
    signal sys_clock   : std_logic;
    signal req         : t_io_req;
    signal resp        : t_io_resp;
    signal inputs      : t_audio_array(0 to 7);
    signal out_L       : std_logic_vector(23 downto 0);
    signal out_R       : std_logic_vector(23 downto 0);                
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    type t_numeric_array is array(natural range <>) of integer;

    constant c_inputs : t_numeric_array(0 to 7) := (-5, 6, -7, 8, -9, 131071, -131072, 12);
    constant c_gains : t_byte_array(0 to 15) := ( X"80", X"00",
                                                  X"00", X"80",
                                                  X"80", X"00",
                                                  X"00", X"80",
                                                  X"80", X"00",
                                                  X"00", X"80",
                                                  X"01", X"00",
                                                  X"00", X"80" );

--    constant c_inputs : t_numeric_array(0 to 7) := (1, 2, 3, 4, 5, 6, 7, 8);
--    constant c_gains : t_byte_array(0 to 15) := ( X"01", X"00",
--                                                  X"00", X"00",
--                                                  X"00", X"00",
--                                                  X"00", X"00",
--                                                  X"00", X"00",
--                                                  X"00", X"00",
--                                                  X"00", X"00",
--                                                  X"00", X"01" );

    function make_signed_array(values: t_numeric_array) return t_audio_array is
        variable ret : t_audio_array(values'range);
    begin
        for i in ret'range loop
            ret(i) := to_signed(values(i), 18);
        end loop;
        return ret;
    end function;

    signal expected_L   : integer;
    signal expected_R   : integer;    
    
begin
    inputs <= make_signed_array(c_inputs);
                
    process
        variable L, R   : integer := 0;
    begin
        for i in inputs'range loop
            L := L + to_integer(unsigned(c_gains(i*2))) * c_inputs(i);
            R := R + to_integer(unsigned(c_gains(i*2 + 1))) * c_inputs(i);
        end loop;
        expected_L <= L;
        expected_R <= R;
        wait;
    end process;

    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.generic_mixer
    generic map(
        g_num_sources => inputs'length
    )
    port map(
        clock         => clock,
        reset         => reset,
        start         => start,
        sys_clock     => clock,
        req           => req,
        resp          => resp,
        inputs        => inputs,
        out_L         => out_L,
        out_R         => out_R
    );
    
    i_bfm: entity work.io_bus_bfm
    generic map (
        g_name => "io_bus"
    )
    port map(
        clock  => clock,
        req    => req,
        resp   => resp
    );
    
    p_test: process
        variable io : p_io_bus_bfm_object;
    begin
        start <= '0';
        wait for 1 ns;
        bind_io_bus_bfm("io_bus", io);
        wait until reset = '0';
        for i in 0 to 15 loop
            io_write(io, to_unsigned(i, 20), c_gains(i));
        end loop;
        wait until clock = '1';
        start <= '1';
        wait until clock = '1';
        start <= '0';
        wait;
    end process;
end architecture;

