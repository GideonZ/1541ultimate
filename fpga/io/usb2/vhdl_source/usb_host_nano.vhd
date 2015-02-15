--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_host_controller
-- Date:2015-02-12  
-- Author: Gideon     
-- Description: Top level of second generation USB controller with memory 
--              interface.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.usb_pkg.all;
use work.usb_cmd_pkg.all;
use work.mem_bus_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity usb_host_nano is
    generic (
        g_tag          : std_logic_vector(7 downto 0) := X"55";
        g_simulation   : boolean := false );
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

        sys_mem_req : out t_mem_req_32;
        sys_mem_resp: in  t_mem_resp_32;

        sys_io_req  : in  t_io_req;
        sys_io_resp : out t_io_resp );

end entity;

architecture arch of usb_host_nano is
    signal nano_addr       : unsigned(7 downto 0);
    signal nano_write      : std_logic;
    signal nano_read       : std_logic;
    signal nano_wdata      : std_logic_vector(15 downto 0);
    signal nano_rdata      : std_logic_vector(15 downto 0);
    signal nano_rdata_regs : std_logic_vector(15 downto 0);
    signal nano_rdata_cmd  : std_logic_vector(15 downto 0);
    signal nano_stall      : std_logic := '0';

    signal reg_read        : std_logic := '0';
    signal reg_write       : std_logic := '0';
    signal reg_ack         : std_logic;
    signal reg_address     : std_logic_vector(5 downto 0);  
    signal reg_wdata       : std_logic_vector(7 downto 0);  
    signal reg_rdata       : std_logic_vector(7 downto 0);  

    -- cmd interface
    signal cmd_addr        : std_logic_vector(3 downto 0);
    signal cmd_valid       : std_logic;
    signal cmd_write       : std_logic;
    signal cmd_wdata       : std_logic_vector(15 downto 0);
    signal cmd_ack         : std_logic;
    signal cmd_ready       : std_logic;

    signal status          : std_logic_vector(7 downto 0);
    signal speed           : std_logic_vector(1 downto 0) := "10";
    signal do_chirp        : std_logic;
    signal chirp_data      : std_logic;
    signal sof_enable      : std_logic;
    signal mem_ctrl_ready  : std_logic;
        
    signal buf_address     : unsigned(10 downto 0);
    signal buf_en          : std_logic;
    signal buf_we          : std_logic;
    signal buf_rdata       : std_logic_vector(7 downto 0);
    signal buf_wdata       : std_logic_vector(7 downto 0);

    signal sys_buf_addr    : std_logic_vector(10 downto 2);
    signal sys_buf_en      : std_logic;
    signal sys_buf_we      : std_logic;
    signal sys_buf_wdata   : std_logic_vector(31 downto 0);
    signal sys_buf_rdata   : std_logic_vector(31 downto 0);

    signal usb_tx_req      : t_usb_tx_req;
    signal usb_tx_resp     : t_usb_tx_resp;
    signal usb_rx          : t_usb_rx;
    
    signal usb_cmd_req     : t_usb_cmd_req;
    signal usb_cmd_resp    : t_usb_cmd_resp;
begin

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

    i_buf_ram: RAMB16_S9_S36
    port map (
        CLKA  => clock,
        SSRA  => reset,
        ENA   => buf_en,
        WEA   => buf_we,
        ADDRA => std_logic_vector(buf_address),
        DIA   => buf_wdata,
        DIPA  => "0",
        DOA   => buf_rdata,

        CLKB  => sys_clock,
        SSRB  => sys_reset,
        ENB   => sys_buf_en,
        WEB   => sys_buf_we,
        ADDRB => sys_buf_addr,
        DIB   => sys_buf_wdata,
        DIPB  => "0000",
        DOB   => sys_buf_rdata );
        
    i_bridge_to_mem_ctrl: entity work.bridge_to_mem_ctrl
    port map (
        ulpi_clock  => clock,
        ulpi_reset  => reset,
        nano_addr   => nano_addr,
        nano_write  => nano_write,
        nano_wdata  => nano_wdata,
        
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,

        -- cmd interface
        cmd_addr    => cmd_addr,
        cmd_valid   => cmd_valid,
        cmd_write   => cmd_write,
        cmd_wdata   => cmd_wdata,
        cmd_ack     => cmd_ack );

    i_memctrl: entity work.usb_memory_ctrl
    generic map (
        g_tag => g_tag )
    
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        -- cmd interface
        cmd_addr    => cmd_addr,
        cmd_valid   => cmd_valid,
        cmd_write   => cmd_write,
        cmd_wdata   => cmd_wdata,
        cmd_ack     => cmd_ack,
        cmd_ready   => cmd_ready,
    
        -- BRAM interface
        ram_addr    => sys_buf_addr,
        ram_en      => sys_buf_en,
        ram_we      => sys_buf_we,
        ram_wdata   => sys_buf_wdata,
        ram_rdata   => sys_buf_rdata,
        
        -- memory interface
        mem_req     => sys_mem_req,
        mem_resp    => sys_mem_resp );

    i_sync: entity work.level_synchronizer
    port map (
        clock       => clock,
        reset       => reset,
        input       => cmd_ready,
        input_c     => mem_ctrl_ready );

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
        io_rdata          => nano_rdata_regs,
        stall             => nano_stall,

        reg_read          => reg_read,
        reg_write         => reg_write,
        reg_ack           => reg_ack,
        reg_address       => reg_address,
        reg_wdata         => reg_wdata,
        reg_rdata         => reg_rdata,

        status            => status,
        mem_ctrl_ready    => mem_ctrl_ready,
        do_chirp          => do_chirp,
        chirp_data        => chirp_data,
        connected         => open,
        operational       => open,
        suspended         => open,
        sof_enable        => sof_enable,
        speed             => speed );

    i_cmd_io: entity work.usb_cmd_nano
    port map (
        clock       => clock,
        reset       => reset,

        io_addr     => nano_addr,
        io_write    => nano_write,
        io_read     => nano_read,
        io_wdata    => nano_wdata,
        io_rdata    => nano_rdata_cmd,

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
        sys_io_req  => sys_io_req,
        sys_io_resp => sys_io_resp  );

    nano_rdata <= nano_rdata_regs or nano_rdata_cmd;

end arch;
