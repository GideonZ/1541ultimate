--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_harness
-- Date:2015-02-14  
-- Author: Gideon     
-- Description: Harness for USB Host Controller with memory controller
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity usb_harness_nano is
port (
    clocks_stopped : in boolean := false );

end entity;

architecture arch of usb_harness_nano is
    signal sys_clock    : std_logic := '1';
    signal sys_reset    : std_logic;
    signal clock        : std_logic := '0';
    signal reset        : std_logic;

    signal sys_io_req   : t_io_req;
    signal sys_io_resp  : t_io_resp;
    signal sys_mem_req  : t_mem_req_32;
    signal sys_mem_resp : t_mem_resp_32;

    signal ulpi_nxt     : std_logic;
    signal ulpi_stp     : std_logic;
    signal ulpi_dir     : std_logic;
    signal ulpi_data    : std_logic_vector(7 downto 0);

begin
    sys_clock <= not sys_clock after 10 ns when not clocks_stopped;
    sys_reset <= '1', '0' after 50 ns;
    clock <= not clock after 8.333 ns when not clocks_stopped;
    reset <= '1', '0' after 250 ns;

    i_io_bus_bfm: entity work.io_bus_bfm
    generic map (
        g_name => "io" )

    port map (
        clock  => sys_clock,
        req    => sys_io_req,
        resp   => sys_io_resp );
    
    i_host: entity work.usb_host_nano
    generic map (
        g_big_endian => true,
        g_simulation => true )
    port map (
        clock       => clock,
        reset       => reset,
        ulpi_nxt    => ulpi_nxt,
        ulpi_dir    => ulpi_dir,
        ulpi_stp    => ulpi_stp,
        ulpi_data   => ulpi_data,

        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
        sys_mem_req => sys_mem_req,
        sys_mem_resp=> sys_mem_resp,
        sys_io_req  => sys_io_req,
        sys_io_resp => sys_io_resp );
        
    i_ulpi_phy: entity work.ulpi_master_bfm
    generic map (
        g_given_name    => "device" )

    port map (
        clock           => clock,
        reset           => reset,
        ulpi_nxt        => ulpi_nxt,
        ulpi_stp        => ulpi_stp,
        ulpi_dir        => ulpi_dir,
        ulpi_data       => ulpi_data );

    i_device: entity work.usb_device_model;

    i_memory: entity work.mem_bus_32_slave_bfm
    generic map (
        g_name    => "memory",
        g_latency => 3
    )
    port map (
        clock     => sys_clock,
        req       => sys_mem_req,
        resp      => sys_mem_resp
    );

end arch;
