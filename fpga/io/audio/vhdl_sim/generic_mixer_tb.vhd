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
    signal out_L       : t_audio;
    signal out_R       : t_audio;                
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    constant c_gains : t_byte_array(0 to 15) := ( X"80", X"00",
                                                  X"00", X"80",
                                                  X"80", X"00",
                                                  X"00", X"80",
                                                  X"80", X"00",
                                                  X"00", X"80",
                                                  X"01", X"00",
                                                  X"00", X"80" );
    
begin
    inputs <= ( 0 => to_signed(-5, 18),
                1 => to_signed(6, 18),
                2 => to_signed(-7, 18),
                3 => to_signed(8, 18),
                4 => to_signed(-9, 18),
                5 => "011111111111111111",
                6 => "100000000000000000",
                7 => to_signed(12, 18) );
                
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

