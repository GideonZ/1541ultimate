
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity basic_io is
generic (
    g_bootfpga      : boolean;
	g_version		: unsigned(7 downto 0) := X"77";
    g_numerator     : natural := 8;
    g_denominator   : natural := 25;
    g_baud_rate     : natural := 115_200
);
port (
    -- globals
    sys_clock   : in  std_logic;
    sys_reset   : in  std_logic;
    
    io_req      : in  t_io_req := c_io_req_init;
    io_resp     : out t_io_resp;
    io_irq      : out std_logic;

    mem_req     : out t_mem_req_32;
    mem_resp    : in  t_mem_resp_32;

	-- Debug UART
	UART_TXD	: out std_logic;
	UART_RXD	: in  std_logic := '1';
	UART_RTS	: out std_logic;
	UART_CTS	: in  std_logic := '1';
	
    -- Flash Interface
    FLASH_CSn   : out std_logic;
    FLASH_SCK   : out std_logic;
    FLASH_MOSI  : out std_logic;
    FLASH_MISO  : in  std_logic := '0';

    -- WiFi UART
    WIFI_BOOT   : out   std_logic;
    WIFI_ENABLE : out   std_logic;
    WIFI_TXD    : out   std_logic;
    WIFI_RXD    : in    std_logic := '1';
    WIFI_RTS    : out   std_logic;
    WIFI_CTS    : in    std_logic := '1';

    -- SD Card Interface
    SD_SSn      : out   std_logic;
    SD_CLK      : out   std_logic;
    SD_MOSI     : out   std_logic;
    SD_MISO     : in    std_logic := '1';
    SD_CARDDETn : in    std_logic := '1';
    
    -- USB Interface (ULPI)
    ulpi_clock  : in  std_logic;
    ulpi_reset  : in  std_logic;
    ULPI_NXT    : in  std_logic := '0';
    ULPI_STP    : out std_logic := '0';
    ULPI_DIR    : in  std_logic := '0';
    ULPI_DATA_I : in  std_logic_vector(7 downto 0) := X"00";
    ULPI_DATA_O : out std_logic_vector(7 downto 0) := X"00";
    ULPI_DATA_T : out std_logic_vector(7 downto 0) := X"00";

    -- Ethernet interface
    eth_clock       : in  std_logic := '0';
    eth_reset       : in  std_logic := '0';
    RMII_CRS_DV     : in  std_logic;
    RMII_RX_ER      : in  std_logic;
    RMII_RX_DATA    : in  std_logic_vector(1 downto 0);
    RMII_TX_DATA    : out std_logic_vector(1 downto 0);
    RMII_TX_EN      : out std_logic;
    -- MDIO_CLK        : out std_logic := '0';
    -- MDIO_DATA_O     : out std_logic := '1';
    -- MDIO_DATA_I     : in  std_logic := '1';
        
    -- Buttons
    button      : in  std_logic_vector(2 downto 0) );
	
end entity;

architecture logic of basic_io is
    function capab(boot: boolean) return std_logic_vector is
        variable ret : std_logic_vector(31 downto 0) := X"01804001"; -- Ethernet, USB, etc
    begin
        if boot then
            ret(30) := '1';
        end if;
        return ret;
    end function;

    constant c_clock_freq       : natural := (16_000_000 * g_denominator) / g_numerator;
    constant c_capabilities     : std_logic_vector(31 downto 0) := capab(g_bootfpga);
     -- Uart, Spi Flash, USB, Ethernet

    constant c_tag_usb2          : std_logic_vector(7 downto 0) := X"0B";
    constant c_tag_rmii          : std_logic_vector(7 downto 0) := X"0E"; -- and 0F
    constant c_tag_wifi_tx       : std_logic_vector(7 downto 0) := X"1E";
    constant c_tag_wifi_rx       : std_logic_vector(7 downto 0) := X"1F";

    -- Timing
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

    -- converted to 32 bits
    signal mem_req_32_usb        : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_usb       : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_rmii       : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_rmii      : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_wifi       : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_wifi      : t_mem_resp_32 := c_mem_resp_32_init;
    
    -- IO Bus
    signal io_req_itu       : t_io_req;
    signal io_resp_itu      : t_io_resp := c_io_resp_init;
    signal io_req_io        : t_io_req;
    signal io_resp_io       : t_io_resp := c_io_resp_init;
    signal io_req_flash     : t_io_req;
    signal io_resp_flash    : t_io_resp := c_io_resp_init;
    signal io_req_sd        : t_io_req;
    signal io_resp_sd       : t_io_resp := c_io_resp_init;
    signal io_req_usb       : t_io_req;
    signal io_resp_usb      : t_io_resp := c_io_resp_init;
    signal io_req_rmii      : t_io_req;
    signal io_resp_rmii     : t_io_resp := c_io_resp_init;
    signal io_req_wifi      : t_io_req;
    signal io_resp_wifi     : t_io_resp := c_io_resp_init;
    signal io_req_dummy     : t_io_req_array(0 to 8);
    signal io_resp_dummy    : t_io_resp_array(0 to 8) := (others => c_io_resp_init);
    signal sys_irq_usb      : std_logic := '0';
    signal sys_irq_wifi     : std_logic := '0';
    signal sys_irq_eth_tx   : std_logic := '0';
    signal sys_irq_eth_rx   : std_logic := '0';
    signal ulpi_data_t_i    : std_logic := '0';

    -- Ethernet
    signal eth_tx_data   : std_logic_vector(7 downto 0);
    signal eth_tx_last   : std_logic;
    signal eth_tx_valid  : std_logic;
    signal eth_tx_ready  : std_logic := '1';

    signal eth_rx_data   : std_logic_vector(7 downto 0);
    signal eth_rx_sof    : std_logic;
    signal eth_rx_eof    : std_logic;
    signal eth_rx_valid  : std_logic;
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
        g_ports     => 10 )
    port map (
        clock    => sys_clock,
        
        req      => io_req_io,
        resp     => io_resp_io,
        
        reqs(0)  => io_req_sd,       -- 4060000 -- io_req_sd
        reqs(1)  => io_req_dummy(3), -- 4060100 -- io_req_rtc
        reqs(2)  => io_req_flash,    -- 4060200 -- io_req_flash
        reqs(3)  => io_req_dummy(4), -- 4060300 -- io_req_debug
        reqs(4)  => io_req_dummy(5), -- 4060400 -- io_req_rtc_tmr
        reqs(5)  => io_req_dummy(6), -- 4060500 -- io_req_gcr_dec
        reqs(6)  => io_req_dummy(7), -- 4060600 -- io_req_icap
        reqs(7)  => io_req_dummy(8), -- 4060700 -- io_req_aud_sel
        reqs(8)  => io_req_rmii,     -- 4060800 -- io_req_rmii
        reqs(9)  => io_req_wifi,     -- 4060900 -- io_req_wifi

        resps(0) => io_resp_sd,
        resps(1) => io_resp_dummy(3),
        resps(2) => io_resp_flash,
        resps(3) => io_resp_dummy(4),
        resps(4) => io_resp_dummy(5),
        resps(5) => io_resp_dummy(6),
        resps(6) => io_resp_dummy(7),
        resps(7) => io_resp_dummy(8),
        resps(8) => io_resp_rmii,
        resps(9) => io_resp_wifi );

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
        g_capabilities  => c_capabilities,
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
        
        irq_high(2 downto 0) => "000",
        irq_high(3)          => sys_irq_wifi,
        irq_high(7 downto 4) => "0000",

        irq_out     => io_irq,
        
--        busy_led    => busy_led,
--        misc_io     => misc_io,

        uart_cts    => UART_CTS,
        uart_rts    => UART_RTS,
        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );

    i_sd: entity work.spi_peripheral_io
    generic map (
        g_fixed_rate => false,
        g_init_rate  => 500,
        g_crc        => true )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req_sd,
        io_resp     => io_resp_sd,
            
        busy		=> open,
        
        SD_DETECTn  => SD_CARDDETn,
        SD_WRPROTn  => '1', --SD_WRPROTn,
        SPI_SSn     => SD_SSn,
        SPI_CLK     => SD_CLK,
        SPI_MOSI    => SD_MOSI,
        SPI_MISO    => SD_MISO );

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
        ulpi_data_t  => ulpi_data_t_i,
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

    ULPI_DATA_T <= (others => ulpi_data_t_i);

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

    i_eth: entity work.ethernet_interface
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
        eth_tx_eof      => eth_tx_last,
        eth_tx_valid    => eth_tx_valid,
        eth_tx_ready    => eth_tx_ready );
        
    i_wifi_uart_dma: entity work.uart_dma
    generic map (
        g_rx_tag  => c_tag_wifi_rx,
        g_tx_tag  => c_tag_wifi_tx,
        g_divisor => (c_clock_freq / g_baud_rate)
    )
    port map (
        clock    => sys_clock,
        reset    => sys_reset,

        io_req   => io_req_wifi,
        io_resp  => io_resp_wifi,
        irq      => sys_irq_wifi,

        mem_req  => mem_req_32_wifi,
        mem_resp => mem_resp_32_wifi,

        boot     => WIFI_BOOT,
        enable   => WIFI_ENABLE,
        route    => open,
        txd      => WIFI_TXD,
        rxd      => WIFI_RXD,
        rts      => WIFI_RTS,
        cts      => WIFI_CTS );

    i_mem_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_ports      => 3,
        g_registered => false )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        reqs(0)     => mem_req_32_rmii,
        reqs(1)     => mem_req_32_usb,
        reqs(2)     => mem_req_32_wifi,
        
        resps(0)    => mem_resp_32_rmii,
        resps(1)    => mem_resp_32_usb,
        resps(2)    => mem_resp_32_wifi,
        
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

    -- Ethernet Transceiver
    i_rmii: entity work.rmii_transceiver
    port map (
        clock           => eth_clock,
        reset           => eth_reset,

        rmii_crs_dv     => RMII_CRS_DV, 
        rmii_rxd        => RMII_RX_DATA,
        rmii_tx_en      => RMII_TX_EN,
        rmii_txd        => RMII_TX_DATA,
        
        eth_rx_data     => eth_rx_data,
        eth_rx_sof      => eth_rx_sof,
        eth_rx_eof      => eth_rx_eof,
        eth_rx_valid    => eth_rx_valid,

        eth_tx_data     => eth_tx_data,
        eth_tx_eof      => eth_tx_last,
        eth_tx_valid    => eth_tx_valid,
        eth_tx_ready    => eth_tx_ready,
        ten_meg_mode    => '0'   );

end architecture logic;
