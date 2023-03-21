
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity basic_io is
generic (
	g_version		: unsigned(7 downto 0) := X"77";
    g_numerator     : natural := 8;
    g_denominator   : natural := 25;
    g_baud_rate     : natural := 115_200
);
port (
    -- globals
    sys_clock   : in    std_logic;
    sys_reset   : in    std_logic;
    
    io_req      : in  t_io_req := c_io_req_init;
    io_resp     : out t_io_resp;
    io_irq      : out std_logic;

    mem_req     : out   t_mem_req_32;
    mem_resp    : in    t_mem_resp_32;

	-- Debug UART
	UART_TXD	: out   std_logic;
	UART_RXD	: in    std_logic := '1';
	
    -- Flash Interface
    FLASH_CSn   : out   std_logic;
    FLASH_SCK   : out   std_logic;
    FLASH_MOSI  : out   std_logic;
    FLASH_MISO  : in    std_logic := '0';

    -- USB Interface (ULPI)
    ulpi_clock  : in    std_logic;
    ulpi_reset  : in    std_logic;
    ULPI_NXT    : in    std_logic := '0';
    ULPI_STP    : out   std_logic := '0';
    ULPI_DIR    : in    std_logic := '0';
    ULPI_DATA_I : in    std_logic_vector(7 downto 0) := X"00";
    ULPI_DATA_O : out   std_logic_vector(7 downto 0) := X"00";
    ULPI_DATA_T : out   std_logic := '0';

    -- Ethernet interface
    eth_clock       : in std_logic := '0';
    eth_reset       : in std_logic := '0';
    eth_tx_data     : out std_logic_vector(7 downto 0);
    eth_tx_eof      : out std_logic;
    eth_tx_valid    : out std_logic;
    eth_tx_ready    : in  std_logic;
    eth_rx_data     : in  std_logic_vector(7 downto 0);
    eth_rx_sof      : in  std_logic;
    eth_rx_eof      : in  std_logic;
    eth_rx_valid    : in  std_logic;
        
    -- Buttons
    button      : in  std_logic_vector(2 downto 0) );
	
end entity;

architecture logic of basic_io is

    constant c_tag_usb2          : std_logic_vector(7 downto 0) := X"0B";
    constant c_tag_rmii          : std_logic_vector(7 downto 0) := X"0E"; -- and 0F

    -- Timing
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

    -- converted to 32 bits
    signal mem_req_32_usb        : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_usb       : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_rmii       : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_rmii      : t_mem_resp_32 := c_mem_resp_32_init;
    
    -- IO Bus
    signal io_req_itu       : t_io_req;
    signal io_resp_itu      : t_io_resp := c_io_resp_init;
    signal io_req_io        : t_io_req;
    signal io_resp_io       : t_io_resp := c_io_resp_init;
    signal io_req_flash     : t_io_req;
    signal io_resp_flash    : t_io_resp := c_io_resp_init;
    signal io_req_usb       : t_io_req;
    signal io_resp_usb      : t_io_resp := c_io_resp_init;
    signal io_req_rmii      : t_io_req;
    signal io_resp_rmii     : t_io_resp := c_io_resp_init;
    signal io_req_dummy     : t_io_req_array(0 to 8);
    signal io_resp_dummy    : t_io_resp_array(0 to 8) := (others => c_io_resp_init);
    signal sys_irq_usb      : std_logic := '0';
    signal sys_irq_eth_tx   : std_logic := '0';
    signal sys_irq_eth_rx   : std_logic := '0';
begin
    i_split1: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 17,
        g_range_hi  => 19,
        g_ports     => 5 )
    port map (
        clock    => sys_clock,
        
        req      => io_req,
        resp     => io_resp,
        
        reqs(0)  => io_req_itu,     -- 4000000 ( 16 ... 400000F)
        reqs(1)  => io_req_dummy(0),-- 4020000
        reqs(2)  => io_req_dummy(1),-- 4040000
        reqs(3)  => io_req_io,      -- 4060000 (  2K... 4060FFF)
        reqs(4)  => io_req_usb,     -- 4080000 (  8K... 4081FFF)

        resps(0) => io_resp_itu,
        resps(1) => io_resp_dummy(0),
        resps(2) => io_resp_dummy(1),
        resps(3) => io_resp_io,
        resps(4) => io_resp_usb );

    i_split2: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 8,
        g_range_hi  => 11,
        g_ports     => 9 )
    port map (
        clock    => sys_clock,
        
        req      => io_req_io,
        resp     => io_resp_io,
        
        reqs(0)  => io_req_dummy(2), -- 4060000 
        reqs(1)  => io_req_dummy(3), -- 4060100 
        reqs(2)  => io_req_flash,    -- 4060200 
        reqs(3)  => io_req_dummy(4), -- 4060300 
        reqs(4)  => io_req_dummy(5), -- 4060400
        reqs(5)  => io_req_dummy(6), -- 4060500
        reqs(6)  => io_req_dummy(7), -- 4060600
        reqs(7)  => io_req_dummy(8), -- 4060700
        reqs(8)  => io_req_rmii,     -- 4060800

        resps(0) => io_resp_dummy(2),
        resps(1) => io_resp_dummy(3),
        resps(2) => io_resp_flash,
        resps(3) => io_resp_dummy(4),
        resps(4) => io_resp_dummy(5),
        resps(5) => io_resp_dummy(6),
        resps(6) => io_resp_dummy(7),
        resps(7) => io_resp_dummy(8),
        resps(8) => io_resp_rmii );

    i_timing: entity work.fractional_div
    generic map(
        g_numerator   => g_numerator,
        g_denominator => g_denominator
    )
    port map(
        clock         => sys_clock,
        tick          => open,
        tick_by_4     => tick_4MHz,
        tick_by_16    => tick_1MHz,
        one_16000     => tick_1kHz
    );

    i_itu: entity work.itu
    generic map (
		g_version	    => g_version,
        g_capabilities  => X"00000001",
        g_uart          => true,
        g_uart_rx       => true,
        g_edge_init     => "10000101",
        g_edge_write    => false,
        g_baudrate      => g_baud_rate )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req_itu,
        io_resp     => io_resp_itu,
    
        tick_4MHz   => tick_4MHz,
        tick_1us    => tick_1MHz,
        tick_1ms    => tick_1kHz,
        buttons     => button,

        irq_in(7)   => '0',
        irq_in(6)   => sys_irq_eth_tx,
        irq_in(5)   => sys_irq_eth_rx,
        irq_in(4)   => '0',
        irq_in(3)   => '0',
        irq_in(2)   => sys_irq_usb,
        
        irq_out     => io_irq,
        
--        busy_led    => busy_led,
--        misc_io     => misc_io,

        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );


    i_usb2: entity work.usb_host_nano
    generic map (
        g_big_endian => false,
        g_tag        => c_tag_usb2,
        g_simulation => false )
    port map (
        clock        => ULPI_CLOCK,
        reset        => ulpi_reset,
        ulpi_nxt     => ulpi_nxt,
        ulpi_dir     => ulpi_dir,
        ulpi_stp     => ulpi_stp,
        ulpi_data_i  => ulpi_data_i,
        ulpi_data_o  => ulpi_data_o,
        ulpi_data_t  => ulpi_data_t,
        debug_data   => open,
        debug_valid  => open,
        error_pulse  => open,
        sys_clock    => sys_clock,
        sys_reset    => sys_reset,
        sys_mem_req  => mem_req_32_usb,
        sys_mem_resp => mem_resp_32_usb,
        sys_io_req   => io_req_usb,
        sys_io_resp  => io_resp_usb,
        sys_irq      => sys_irq_usb );

    i_spi_flash: entity work.spi_peripheral_io
    generic map (
        g_fixed_rate => true,
        g_init_rate  => 1,
        g_crc        => false )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req_flash,
        io_resp     => io_resp_flash,
            
        SD_DETECTn  => '0',
        SD_WRPROTn  => '1',
        SPI_SSn     => FLASH_CSn,
        SPI_CLK     => FLASH_SCK,
        SPI_MOSI    => FLASH_MOSI,
        SPI_MISO    => FLASH_MISO );

    i_rmii: entity work.ethernet_interface
    generic map (
        g_mem_tag   => c_tag_rmii
    )
    port map(
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
        io_req      => io_req_rmii,
        io_resp     => io_resp_rmii,
        io_irq_tx   => sys_irq_eth_tx,
        io_irq_rx   => sys_irq_eth_rx,
        mem_req     => mem_req_32_rmii,
        mem_resp    => mem_resp_32_rmii,
        
        eth_clock       => eth_clock,
        eth_reset       => eth_reset,
        eth_rx_data     => eth_rx_data,
        eth_rx_sof      => eth_rx_sof,
        eth_rx_eof      => eth_rx_eof,
        eth_rx_valid    => eth_rx_valid,

        eth_tx_data     => eth_tx_data,
        eth_tx_eof      => eth_tx_eof,
        eth_tx_valid    => eth_tx_valid,
        eth_tx_ready    => eth_tx_ready );
        
    i_mem_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_ports      => 2,
        g_registered => false )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        reqs(0)     => mem_req_32_rmii,
        reqs(1)     => mem_req_32_usb,
        
        resps(0)    => mem_resp_32_rmii,
        resps(1)    => mem_resp_32_usb,
        
        req         => mem_req,
        resp        => mem_resp );        

    r_stubs: for i in io_req_dummy'range generate
        i_dummy: entity work.io_dummy
        port map (
            clock   => sys_clock,
            io_req  => io_req_dummy(i),
            io_resp => io_resp_dummy(i)
        );
    end generate;

end logic;
