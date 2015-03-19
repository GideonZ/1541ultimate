library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity usb_controller is
generic (
    g_tag          : std_logic_vector(7 downto 0) := X"55" );
port (
    ulpi_clock  : in  std_logic;
    ulpi_reset  : in  std_logic;

    -- ULPI Interface
    ULPI_DATA   : inout std_logic_vector(7 downto 0);
    ULPI_DIR    : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;

    -- register interface bus
    sys_clock   : in    std_logic;
    sys_reset   : in    std_logic;
    
    sys_mem_req : out   t_mem_req;
    sys_mem_resp: in    t_mem_resp;

    sys_io_req  : in    t_io_req;
    sys_io_resp : out   t_io_resp );

end usb_controller;

architecture wrap of usb_controller is

    signal nano_addr       : unsigned(7 downto 0);
    signal nano_write      : std_logic;
    signal nano_read       : std_logic;
    signal nano_wdata      : std_logic_vector(15 downto 0);
    signal nano_rdata      : std_logic_vector(15 downto 0);
    signal stall           : std_logic := '0';
    
    signal rx_pid          : std_logic_vector(3 downto 0) := X"0";
    signal rx_token        : std_logic_vector(10 downto 0) := (others => '0');
    signal rx_valid_token  : std_logic := '0';
    signal rx_valid_handsh : std_logic := '0';
    signal rx_valid_packet : std_logic := '0';
    signal rx_error        : std_logic := '0';
    signal rx_user_valid   : std_logic := '0';
    signal rx_user_start   : std_logic := '0';
    signal rx_user_data    : std_logic_vector(7 downto 0) := X"12";

    signal tx_busy         : std_logic;
    signal tx_ack          : std_logic;
    signal tx_send_token   : std_logic;
    signal tx_send_handsh  : std_logic;
    signal tx_pid          : std_logic_vector(3 downto 0);
    signal tx_token        : std_logic_vector(10 downto 0);
    signal tx_send_data    : std_logic;
    signal tx_no_data      : std_logic;
    signal tx_user_data    : std_logic_vector(7 downto 0);
    signal tx_user_last    : std_logic;
    signal tx_user_next    : std_logic;
    signal tx_length       : unsigned(10 downto 0);
    signal transferred     : unsigned(10 downto 0);
    
    -- cmd interface
    signal cmd_addr        : std_logic_vector(3 downto 0);
    signal cmd_valid       : std_logic;
    signal cmd_write       : std_logic;
    signal cmd_wdata       : std_logic_vector(15 downto 0);
    signal cmd_ack         : std_logic;
    signal cmd_ready       : std_logic;

    signal sys_buf_addr    : std_logic_vector(10 downto 0);
    signal sys_buf_en      : std_logic;
    signal sys_buf_we      : std_logic;
    signal sys_buf_wdata   : std_logic_vector(7 downto 0);
    signal sys_buf_rdata   : std_logic_vector(7 downto 0);

    signal ulpi_buf_addr   : std_logic_vector(10 downto 0);
    signal ulpi_buf_en     : std_logic;
    signal ulpi_buf_we     : std_logic;
    signal ulpi_buf_wdata  : std_logic_vector(7 downto 0);
    signal ulpi_buf_rdata  : std_logic_vector(7 downto 0);


    -- low level    
    signal tx_data         : std_logic_vector(7 downto 0) := X"00";
    signal tx_last         : std_logic := '0';
    signal tx_valid        : std_logic := '0';
    signal tx_start        : std_logic := '0';
    signal tx_next         : std_logic := '0';
    signal tx_chirp_start  : std_logic;
    signal tx_chirp_level  : std_logic;
    signal tx_chirp_end    : std_logic;

    signal rx_data         : std_logic_vector(7 downto 0);
    signal status          : std_logic_vector(7 downto 0);
    signal rx_last         : std_logic;
    signal rx_valid        : std_logic;
    signal rx_store        : std_logic;
    signal rx_register     : std_logic;

    signal reg_read        : std_logic := '0';
    signal reg_write       : std_logic := '0';
    signal reg_ack         : std_logic;
    signal reg_addr        : std_logic_vector(5 downto 0);  
    signal reg_wdata       : std_logic_vector(7 downto 0);  
    
    signal speed           : std_logic_vector(1 downto 0) := "10"; -- TODO!
begin                             
    i_nano: entity work.nano 
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        -- i/o interface
        io_addr     => nano_addr,
        io_write    => nano_write,
        io_read     => nano_read,
        io_wdata    => nano_wdata,
        io_rdata    => nano_rdata,
        stall       => stall,
                
        -- system interface (to write code into the nano)
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
        sys_io_req  => sys_io_req,
        sys_io_resp => sys_io_resp );
    
    i_regs: entity work.usb_io_bank 
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        -- i/o interface
        io_addr     => nano_addr,
        io_read     => nano_read,
        io_write    => nano_write,
        io_wdata    => nano_wdata,
        io_rdata    => nano_rdata,
        stall       => stall,
    
        -- memory controller
        mem_ready   => cmd_ready,
        transferred => transferred,
        
        -- Register access
        reg_addr        => reg_addr,
        reg_read        => reg_read,
        reg_write       => reg_write,
        reg_ack         => reg_ack,
        reg_wdata       => reg_wdata,
        reg_rdata       => rx_data,
        status          => status,
        
        -- I/O pins from RX
        rx_pid          => rx_pid,
        rx_token        => rx_token,
        rx_valid_token  => rx_valid_token,
        rx_valid_handsh => rx_valid_handsh,
        rx_valid_packet => rx_valid_packet,
        rx_error        => rx_error,
    
        -- I/O pins to TX
        tx_pid          => tx_pid,
        tx_token        => tx_token,
        tx_send_token   => tx_send_token,
        tx_send_handsh  => tx_send_handsh,
        tx_send_data    => tx_send_data,
        tx_length       => tx_length,
        tx_no_data      => tx_no_data,
        tx_ack          => tx_ack,
        tx_chirp_start  => tx_chirp_start,
        tx_chirp_end    => tx_chirp_end,
        tx_chirp_level  => tx_chirp_level );

    i_bridge_to_mem_ctrl: entity work.bridge_to_mem_ctrl
    port map (
        ulpi_clock      => ulpi_clock,
        ulpi_reset      => ulpi_reset,
        nano_addr       => nano_addr,
        nano_write      => nano_write,
        nano_wdata      => nano_wdata,
        
        sys_clock       => sys_clock,
        sys_reset       => sys_reset,

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

    i_buf_ram: RAMB16_S9_S9
    port map (
		CLKA  => sys_clock,
		SSRA  => sys_reset,
		ENA   => sys_buf_en,
		WEA   => sys_buf_we,
        ADDRA => sys_buf_addr,
		DIA   => sys_buf_wdata,
		DIPA  => "0",
		DOA   => sys_buf_rdata,
		
		CLKB  => ulpi_clock,
		SSRB  => ulpi_reset,
		ENB   => ulpi_buf_en,
		WEB   => ulpi_buf_we,
        ADDRB => ulpi_buf_addr,
		DIB   => ulpi_buf_wdata,
        DIPB  => "0",
		DOB   => ulpi_buf_rdata );


    i_buf_ctrl: entity work.rxtx_to_buf
    port map (
        clock           => ulpi_clock,
        reset           => ulpi_reset,
        
        -- transferred length
        transferred     => transferred,
        
        -- bram interface
        ram_addr        => ulpi_buf_addr,
        ram_wdata       => ulpi_buf_wdata,
        ram_rdata       => ulpi_buf_rdata,
        ram_we          => ulpi_buf_we,
        ram_en          => ulpi_buf_en,
        
        -- Interface from RX
        user_rx_valid   => rx_user_valid,
        user_rx_start   => rx_user_start,
        user_rx_data    => rx_user_data,
        user_rx_last    => rx_last,
        
        -- Interface to TX
        send_data       => tx_send_data,
        last_addr       => tx_length,
        no_data         => tx_no_data,
        user_tx_data    => tx_user_data,
        user_tx_last    => tx_user_last,
        user_tx_next    => tx_user_next );

    i_tx: entity work.ulpi_tx
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        -- Bus Interface
        tx_start    => tx_start,
        tx_last     => tx_last,
        tx_valid    => tx_valid,
        tx_next     => tx_next,
        tx_data     => tx_data,
        
        -- Status
        speed       => speed,
        status      => status,
        busy        => tx_busy,
        tx_ack      => tx_ack,         
        
        -- Interface to send tokens
        send_token  => tx_send_token,
        send_handsh => tx_send_handsh,
        pid         => tx_pid,
        token       => tx_token,
        
        -- Interface to send data packets
        send_data   => tx_send_data,
        no_data     => tx_no_data,
        user_data   => tx_user_data,
        user_last   => tx_user_last,
        user_next   => tx_user_next,
                
        -- Interface to read/write registers and reset packets
        send_reset_data => tx_chirp_start,
        reset_data      => tx_chirp_level,
        reset_last      => tx_chirp_end );

    
    i_rx: entity work.ulpi_rx
    generic map (
        g_allow_token   => false )
    port map (
        clock           => ulpi_clock,
        reset           => ulpi_reset,
                        
        rx_data         => rx_data,
        rx_last         => rx_last,
        rx_valid        => rx_valid,
        rx_store        => rx_store,
                        
        pid             => rx_pid,
        token           => rx_token,
        valid_token     => rx_valid_token,
        valid_handsh    => rx_valid_handsh,
    
        valid_packet    => rx_valid_packet,
        data_out        => rx_user_data,
        data_valid      => rx_user_valid,
        data_start      => rx_user_start,
        
        error           => rx_error );

    i_bus: entity work.ulpi_bus
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        
        status      => status,
        
        -- register interface
        reg_read    => reg_read,
        reg_write   => reg_write,
        reg_address => reg_addr,
        reg_wdata   => reg_wdata,
        reg_ack     => reg_ack,

        -- stream interface
        tx_data     => tx_data,
        tx_last     => tx_last,
        tx_valid    => tx_valid,
        tx_start    => tx_start,
        tx_next     => tx_next,
    
        rx_data     => rx_data,
        rx_last     => rx_last,
        rx_register => rx_register,
        rx_store    => rx_store,
        rx_valid    => rx_valid );
		
end wrap;
