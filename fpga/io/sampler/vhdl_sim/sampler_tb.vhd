library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.sampler_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.tl_flat_memory_model_pkg.all;

entity sampler_tb is
end entity;

architecture tb of sampler_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;
    signal mem_req     : t_mem_req;
    signal mem_resp    : t_mem_resp;
    signal sample_L    : signed(17 downto 0);
    signal sample_R    : signed(17 downto 0);
    signal new_sample  : std_logic;

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_dut: entity work.sampler
    generic map (
        g_clock_freq    => 62_500_000,
        g_num_voices    => 8 )
    port map (
        clock       => clock,
        reset       => reset,
        
        io_req      => io_req,
        io_resp     => io_resp,
        
        mem_req     => mem_req,
        mem_resp    => mem_resp,
        
        sample_L    => sample_L,
        sample_R    => sample_R,
        new_sample  => new_sample );

    i_io_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => clock,
    
        req         => io_req,
        resp        => io_resp );

    i_mem_bfm: entity work.mem_bus_slave_bfm
    generic map (
        g_name      => "mem_bfm",
        g_latency	=> 2 )
    port map (
        clock       => clock,
    
        req         => mem_req,
        resp        => mem_resp );

    test: process
        variable io  : p_io_bus_bfm_object;
        variable mem : h_mem_object;
        variable d   : std_logic_vector(7 downto 0); 
    begin
        wait until reset='0';
        bind_io_bus_bfm("io_bfm", io);
        bind_mem_model("mem_bfm", mem);
        for i in 0 to 255 loop
            d := std_logic_vector(to_signed(integer(127.0*sin(real(i) / 40.58451048843)), 8));
            write_memory_8(mem, std_logic_vector(to_unsigned(16#1234500#+i, 32)), d);
        end loop;
        io_write(io, X"00" + c_sample_volume        , X"3F");
        io_write(io, X"00" + c_sample_pan           , X"07");
        io_write(io, X"00" + c_sample_start_addr_h  , X"01");
        io_write(io, X"00" + c_sample_start_addr_mh , X"23");
        io_write(io, X"00" + c_sample_start_addr_ml , X"45");
        io_write(io, X"00" + c_sample_start_addr_l  , X"00");
        io_write(io, X"00" + c_sample_length_h      , X"00");
        io_write(io, X"00" + c_sample_length_m      , X"01");
        io_write(io, X"00" + c_sample_length_l      , X"00");
        io_write(io, X"00" + c_sample_rate_h        , X"00");
        io_write(io, X"00" + c_sample_rate_l        , X"18");
        io_write(io, X"00" + c_sample_control       , X"01");
        
        io_write(io, X"20" + c_sample_volume        , X"28");
        io_write(io, X"20" + c_sample_pan           , X"0F");
        io_write(io, X"20" + c_sample_start_addr_h  , X"01");
        io_write(io, X"20" + c_sample_start_addr_mh , X"23");
        io_write(io, X"20" + c_sample_start_addr_ml , X"45");
        io_write(io, X"20" + c_sample_start_addr_l  , X"00");
        io_write(io, X"20" + c_sample_length_h      , X"00");
        io_write(io, X"20" + c_sample_length_m      , X"01");
        io_write(io, X"20" + c_sample_length_l      , X"00");
        io_write(io, X"20" + c_sample_rate_h        , X"00");
        io_write(io, X"20" + c_sample_rate_l        , X"05");
        io_write(io, X"20" + c_sample_rep_b_pos_l   , X"CC");
        io_write(io, X"20" + c_sample_control       , X"03"); -- repeat on

        io_write(io, X"E0" + c_sample_volume        , X"38");
        io_write(io, X"E0" + c_sample_pan           , X"04");
        io_write(io, X"E0" + c_sample_start_addr_h  , X"01");
        io_write(io, X"E0" + c_sample_start_addr_mh , X"23");
        io_write(io, X"E0" + c_sample_start_addr_ml , X"45");
        io_write(io, X"E0" + c_sample_start_addr_l  , X"00");
        io_write(io, X"E0" + c_sample_length_h      , X"00");
        io_write(io, X"E0" + c_sample_length_m      , X"00");
        io_write(io, X"E0" + c_sample_length_l      , X"80");
        io_write(io, X"E0" + c_sample_rate_h        , X"00");
        io_write(io, X"E0" + c_sample_rate_l        , X"09");
        io_write(io, X"E0" + c_sample_rep_b_pos_l   , X"80");
        io_write(io, X"E0" + c_sample_control       , X"13"); -- repeat on, 16 bit

        wait;
        
    end process;

end architecture;
