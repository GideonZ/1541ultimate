--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_harness
-- Date:2015-01-27  
-- Author: Gideon     
-- Description: Harness for USB Host Controller
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;

entity usb_harness is
port (
    clocks_stopped : in boolean := false );

end entity;

architecture arch of usb_harness is
    signal sys_clock    : std_logic := '0';
    signal sys_reset    : std_logic;
    signal clock        : std_logic := '0';
    signal reset        : std_logic;

    signal sys_io_req   : t_io_req;
    signal sys_io_resp  : t_io_resp;

    signal ulpi_nxt     : std_logic;
    signal ulpi_stp     : std_logic;
    signal ulpi_dir     : std_logic;
    signal ulpi_data    : std_logic_vector(7 downto 0);

begin
    sys_clock <= not sys_clock after 10 ns when not clocks_stopped;
    sys_reset <= '1', '0' after 50 ns;
    clock <= not clock after 8 ns when not clocks_stopped;
    reset <= '1', '0' after 250 ns;
    

    i_io_bus_bfm: entity work.io_bus_bfm
    generic map (
        g_name => "io" )

    port map (
        clock  => sys_clock,
        req    => sys_io_req,
        resp   => sys_io_resp );
    
    i_host: entity work.usb_host_controller
    generic map (
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

end arch;
