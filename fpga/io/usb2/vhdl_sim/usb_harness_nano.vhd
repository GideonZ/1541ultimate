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
    signal sys_clock_2x : std_logic := '1';
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

    signal SDRAM_CLK    : std_logic;
    signal SDRAM_CKE    : std_logic;
    signal SDRAM_CSn    : std_logic := '1';
    signal SDRAM_RASn   : std_logic := '1';
    signal SDRAM_CASn   : std_logic := '1';
    signal SDRAM_WEn    : std_logic := '1';
    signal SDRAM_DQM    : std_logic := '0';
    signal SDRAM_A      : std_logic_vector(12 downto 0);
    signal SDRAM_BA     : std_logic_vector(1 downto 0);
    signal SDRAM_DQ     : std_logic_vector(7 downto 0) := (others => 'Z');

begin
    sys_clock <= not sys_clock after 10 ns when not clocks_stopped;
    sys_clock_2x <= not sys_clock_2x after 5 ns when not clocks_stopped;
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

    i_mem_ctrl: entity work.ext_mem_ctrl_v5
    generic map (
        g_simulation => true )
    port map (
        clock       => sys_clock,
        clk_2x      => sys_clock_2x,
        reset       => sys_reset,
    
        inhibit     => '0',
        is_idle     => open,
    
        req         => sys_mem_req,
        resp        => sys_mem_resp,
    
        SDRAM_CLK   => SDRAM_CLK,
        SDRAM_CKE   => SDRAM_CKE,
        SDRAM_CSn   => SDRAM_CSn,
        SDRAM_RASn  => SDRAM_RASn,
        SDRAM_CASn  => SDRAM_CASn,
        SDRAM_WEn   => SDRAM_WEn,
        SDRAM_DQM   => SDRAM_DQM,
    
        SDRAM_BA    => SDRAM_BA,
        SDRAM_A     => SDRAM_A,
        SDRAM_DQ    => SDRAM_DQ );

    ram: entity work.dram_8
    generic map(
        g_cas_latency => 3,
        g_burst_len_r => 4,
        g_burst_len_w => 4,
        g_column_bits => 10,
        g_row_bits    => 13,
        g_bank_bits   => 2
    )
    port map(
        CLK           => SDRAM_CLK,
        CKE           => SDRAM_CKE,
        A             => SDRAM_A,
        BA            => SDRAM_BA,
        CSn           => SDRAM_CSn,
        RASn          => SDRAM_RASn,
        CASn          => SDRAM_CASn,
        WEn           => SDRAM_WEn,
        DQM           => SDRAM_DQM,
        DQ            => SDRAM_DQ
    );


end arch;
