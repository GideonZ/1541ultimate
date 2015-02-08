--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_host_controller
-- Date:2015-01-18  
-- Author: Gideon     
-- Description: 
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.usb_pkg.all;
use work.usb_cmd_pkg.all;

entity usb_host_controller is
    generic (
        g_simulation    : boolean := false );
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;

        ulpi_nxt    : in    std_logic;
        ulpi_dir    : in    std_logic;
        ulpi_stp    : out   std_logic;
        ulpi_data   : inout std_logic_vector(7 downto 0);

        -- 
        sys_clock   : in  std_logic;
        sys_reset   : in  std_logic;
        sys_io_req  : in  t_io_req;
        sys_io_resp : out t_io_resp );

end entity;

architecture arch of usb_host_controller is
    signal nano_addr       : unsigned(7 downto 0);
    signal nano_write      : std_logic;
    signal nano_read       : std_logic;
    signal nano_wdata      : std_logic_vector(15 downto 0);
    signal nano_rdata      : std_logic_vector(15 downto 0);
    signal nano_stall      : std_logic := '0';

    signal reg_read        : std_logic := '0';
    signal reg_write       : std_logic := '0';
    signal reg_ack         : std_logic;
    signal reg_address     : std_logic_vector(5 downto 0);  
    signal reg_wdata       : std_logic_vector(7 downto 0);  
    signal reg_rdata       : std_logic_vector(7 downto 0);  

    signal status          : std_logic_vector(7 downto 0);
    signal speed           : std_logic_vector(1 downto 0) := "10";
    signal do_chirp        : std_logic;
    signal chirp_data      : std_logic;
    signal sof_enable      : std_logic;
    signal operational     : std_logic;
    signal connected       : std_logic;
    
    signal buf_address     : unsigned(10 downto 0);
    signal buf_en          : std_logic;
    signal buf_we          : std_logic;
    signal buf_rdata       : std_logic_vector(7 downto 0);
    signal buf_wdata       : std_logic_vector(7 downto 0);

    signal usb_tx_req      : t_usb_tx_req;
    signal usb_tx_resp     : t_usb_tx_resp;
    signal usb_rx          : t_usb_rx;
    
    signal usb_cmd_req     : t_usb_cmd_req;
    signal usb_cmd_resp    : t_usb_cmd_resp;

    signal io_data_rdata        : std_logic_vector(7 downto 0);
    signal io_data_en           : std_logic;
    signal io_data_ack          : std_logic;

    signal io_reg_req           : t_io_req;
    signal io_reg_resp          : t_io_resp;
    signal io_nano_req          : t_io_req;
    signal io_nano_resp         : t_io_resp;
    signal io_data_req          : t_io_req;
    signal io_data_resp         : t_io_resp;
    signal io_usb_reg_req       : t_io_req;
    signal io_usb_reg_resp      : t_io_resp;

begin
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 12,
        g_range_hi => 13,
        g_ports    => 3 )
    port map (
        clock      => sys_clock,
        req        => sys_io_req,
        resp       => sys_io_resp,
        reqs(0)    => io_reg_req,
        reqs(1)    => io_nano_req,
        reqs(2)    => io_data_req,
        resps(0)   => io_reg_resp,
        resps(1)   => io_nano_resp,
        resps(2)   => io_data_resp );

    i_bridge: entity work.io_bus_bridge
    generic map (
        g_addr_width => 4 )
    port map (
        clock_a      => sys_clock,
        reset_a      => sys_reset,
        req_a        => io_reg_req,
        resp_a       => io_reg_resp,
        
        clock_b      => clock,
        reset_b      => reset,
        req_b        => io_usb_reg_req,
        resp_b       => io_usb_reg_resp );
    

    i_intf: entity work.usb_host_interface
    generic map (
        g_simulation => g_simulation )
    port map (
        clock       => clock,
        reset       => reset,
        usb_rx      => usb_rx,
        usb_tx_req  => usb_tx_req,
        usb_tx_resp => usb_tx_resp,
        reg_read    => reg_read,
        reg_write   => reg_write,
        reg_address => reg_address,
        reg_wdata   => reg_wdata,
        reg_rdata   => reg_rdata,
        reg_ack     => reg_ack,
        do_chirp    => do_chirp,
        chirp_data  => chirp_data,
        status      => status,
        speed       => speed,
        ulpi_nxt    => ulpi_nxt,
        ulpi_stp    => ulpi_stp,
        ulpi_dir    => ulpi_dir,
        ulpi_data   => ulpi_data );
    
    i_seq: entity work.host_sequencer
    port map (
        clock               => clock,
        reset               => reset,
        buf_address         => buf_address,
        buf_en              => buf_en,
        buf_we              => buf_we,
        buf_rdata           => buf_rdata,
        buf_wdata           => buf_wdata,
        sof_enable          => sof_enable,
        speed               => speed,
        usb_cmd_req         => usb_cmd_req,
        usb_cmd_resp        => usb_cmd_resp,
        usb_rx              => usb_rx,
        usb_tx_req          => usb_tx_req,
        usb_tx_resp         => usb_tx_resp );

    i_mem: entity work.dpram
    generic map (
        g_width_bits   => 8,
        g_depth_bits   => 11,
        g_storage      => "block" )
    port map (
        a_clock        => clock,
        a_address      => buf_address,
        a_rdata        => buf_rdata,
        a_wdata        => buf_wdata,
        a_en           => buf_en,
        a_we           => buf_we,
        b_clock        => sys_clock,
        b_address      => io_data_req.address(10 downto 0),
        b_rdata        => io_data_rdata,
        b_wdata        => io_data_req.data,
        b_en           => io_data_en,
        b_we           => io_data_req.write );

    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            io_data_ack <= io_data_req.read or io_data_req.write;
        end if;
    end process;
    io_data_en <= io_data_req.read or io_data_req.write;
    io_data_resp.data <= io_data_rdata when io_data_ack='1' else X"00";
    io_data_resp.ack <= io_data_ack;
    io_data_resp.irq <= '0';

    i_nano_io: entity work.nano_minimal_io
    generic map (
        g_support_suspend => false  )
    port map (
        clock             => clock,
        reset             => reset,

        io_addr           => nano_addr,
        io_write          => nano_write,
        io_read           => nano_read,
        io_wdata          => nano_wdata,
        io_rdata          => nano_rdata,
        stall             => nano_stall,

        reg_read          => reg_read,
        reg_write         => reg_write,
        reg_ack           => reg_ack,
        reg_address       => reg_address,
        reg_wdata         => reg_wdata,
        reg_rdata         => reg_rdata,

        status            => status,
        do_chirp          => do_chirp,
        chirp_data        => chirp_data,
        connected         => connected,
        operational       => operational,
        suspended         => open,
        sof_enable        => sof_enable,
        speed             => speed );

    i_cmd_io: entity work.usb_cmd_io
    port map (
        clock       => clock,
        reset       => reset,

        io_req      => io_usb_reg_req,
        io_resp     => io_usb_reg_resp,

        -- status (maybe only temporary)
        connected   => connected,
        operational => operational,
        speed       => speed,
        
        cmd_req     => usb_cmd_req,
        cmd_resp    => usb_cmd_resp );

    i_nano: entity work.nano
    port map (
        clock       => clock,
        reset       => reset,
        io_addr     => nano_addr,
        io_write    => nano_write,
        io_read     => nano_read,
        io_wdata    => nano_wdata,
        io_rdata    => nano_rdata,
        stall       => nano_stall,

        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
        sys_io_req  => io_nano_req,
        sys_io_resp => io_nano_resp  );

end arch;

