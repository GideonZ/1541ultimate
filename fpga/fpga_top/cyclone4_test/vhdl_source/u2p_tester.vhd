-------------------------------------------------------------------------------
-- Title      : u2p_tester
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Toplevel for u2p_tester.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    use work.endianness_pkg.all;
        
entity u2p_tester is
port (
    -- slot side
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_RSTn        : in    std_logic;
    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : in    std_logic;
    SLOT_BA          : in    std_logic;
    SLOT_DMAn        : in    std_logic;
    SLOT_EXROMn      : out   std_logic;
    SLOT_GAMEn       : inout std_logic;
    SLOT_ROMHn       : out   std_logic;
    SLOT_ROMLn       : out   std_logic;
    SLOT_IO1n        : in    std_logic;
    SLOT_IO2n        : out   std_logic;
    SLOT_IRQn        : in    std_logic;
    SLOT_NMIn        : out   std_logic;
    SLOT_VCC         : in    std_logic;
    
    -- memory
    SDRAM_A     : out   std_logic_vector(13 downto 0); -- DRAM A
    SDRAM_BA    : out   std_logic_vector(2 downto 0) := (others => '0');
    SDRAM_DQ    : inout std_logic_vector(7 downto 0);
    SDRAM_DM    : inout std_logic;
    SDRAM_CSn   : out   std_logic;
    SDRAM_RASn  : out   std_logic;
    SDRAM_CASn  : out   std_logic;
    SDRAM_WEn   : out   std_logic;
    SDRAM_CKE   : out   std_logic;
    SDRAM_CLK   : inout std_logic;
    SDRAM_CLKn  : inout std_logic;
    SDRAM_ODT   : out   std_logic;
    SDRAM_DQS   : inout std_logic;
     
    AUDIO_MCLK  : out   std_logic := '0';
    AUDIO_BCLK  : out   std_logic := '0';
    AUDIO_LRCLK : out   std_logic := '0';
    AUDIO_SDO   : out   std_logic := '0';
    AUDIO_SDI   : in    std_logic;

    -- IEC bus
    IEC_ATN     : inout std_logic;
    IEC_DATA    : inout std_logic;
    IEC_CLOCK   : inout std_logic;
    IEC_RESET   : in    std_logic;
    IEC_SRQ_IN  : inout std_logic;
    
    LED_DISKn   : out   std_logic; -- activity LED
    LED_CARTn   : out   std_logic;
    LED_SDACTn  : out   std_logic;
    LED_MOTORn  : out   std_logic;
    
    -- Ethernet RMII
    ETH_RESETn      : out std_logic := '1';
    ETH_IRQn        : in  std_logic;
    
    RMII_REFCLK     : in  std_logic;
    RMII_CRS_DV     : in  std_logic;
    RMII_RX_ER      : in  std_logic;
    RMII_RX_DATA    : in  std_logic_vector(1 downto 0);
    RMII_TX_DATA    : out std_logic_vector(1 downto 0);
    RMII_TX_EN      : out std_logic;

    MDIO_CLK    : out   std_logic := '0';
    MDIO_DATA   : inout std_logic := 'Z';

    -- Speaker data
    SPEAKER_DATA    : out std_logic := '0';
    SPEAKER_ENABLE  : out std_logic := '0';

    -- Debug UART
    UART_TXD    : out   std_logic;
    UART_RXD    : in    std_logic;
    
    -- I2C Interface for RTC, audio codec and usb hub
    I2C_SDA     : inout std_logic := 'Z';
    I2C_SCL     : inout std_logic := 'Z';
    I2C_SDA_18  : inout std_logic := 'Z';
    I2C_SCL_18  : inout std_logic := 'Z';

    -- Flash Interface
    FLASH_CSn   : out   std_logic;
    FLASH_SCK   : out   std_logic;
    FLASH_MOSI  : out   std_logic;
    FLASH_MISO  : in    std_logic;
    FLASH_SEL   : out   std_logic := '0';
    FLASH_SELCK : out   std_logic := '0';

    -- USB Interface (ULPI)
    ULPI_RESET  : out   std_logic;
    ULPI_CLOCK  : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;
    ULPI_DIR    : in    std_logic;
    ULPI_DATA   : inout std_logic_vector(7 downto 0);

    HUB_RESETn  : out   std_logic := '1';
    HUB_CLOCK   : out   std_logic := '0';

    -- Misc
    BOARD_REVn  : in    std_logic_vector(4 downto 0);

    -- Cassette Interface
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;

architecture rtl of u2p_tester is
    component nios_tester is
        port (
            audio_in_data           : in    std_logic_vector(31 downto 0) := (others => 'X'); -- data
            audio_in_valid          : in    std_logic                     := 'X';             -- valid
            audio_in_ready          : out   std_logic;                                        -- ready
            audio_out_data          : out   std_logic_vector(31 downto 0);                    -- data
            audio_out_valid         : out   std_logic;                                        -- valid
            audio_out_ready         : in    std_logic                     := 'X';             -- ready
            dummy_export            : in    std_logic                     := 'X';             -- export
            io_ack                  : in    std_logic                     := 'X';             -- ack
            io_rdata                : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
            io_read                 : out   std_logic;                                        -- read
            io_wdata                : out   std_logic_vector(7 downto 0);                     -- wdata
            io_write                : out   std_logic;                                        -- write
            io_address              : out   std_logic_vector(19 downto 0);                    -- address
            io_irq                  : in    std_logic                     := 'X';             -- irq
            io_u2p_ack              : in    std_logic                     := 'X';             -- ack
            io_u2p_rdata            : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
            io_u2p_read             : out   std_logic;                                        -- read
            io_u2p_wdata            : out   std_logic_vector(7 downto 0);                     -- wdata
            io_u2p_write            : out   std_logic;                                        -- write
            io_u2p_address          : out   std_logic_vector(19 downto 0);                    -- address
            io_u2p_irq              : in    std_logic                     := 'X';             -- irq
            jtag0_jtag_tck          : out   std_logic;                                        -- jtag_tck
            jtag0_jtag_tms          : out   std_logic;                                        -- jtag_tms
            jtag0_jtag_tdi          : out   std_logic;                                        -- jtag_tdi
            jtag0_jtag_tdo          : in    std_logic                     := 'X';             -- jtag_tdo
            jtag1_jtag_tck          : out   std_logic;                                        -- jtag_tck
            jtag1_jtag_tms          : out   std_logic;                                        -- jtag_tms
            jtag1_jtag_tdi          : out   std_logic;                                        -- jtag_tdi
            jtag1_jtag_tdo          : in    std_logic                     := 'X';             -- jtag_tdo
            jtag_in_data            : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- data
            jtag_in_valid           : in    std_logic                     := 'X';             -- valid
            jtag_in_ready           : out   std_logic;                                        -- ready
            mem_mem_req_address     : out   std_logic_vector(25 downto 0);                    -- mem_req_address
            mem_mem_req_byte_en     : out   std_logic_vector(3 downto 0);                     -- mem_req_byte_en
            mem_mem_req_read_writen : out   std_logic;                                        -- mem_req_read_writen
            mem_mem_req_request     : out   std_logic;                                        -- mem_req_request
            mem_mem_req_tag         : out   std_logic_vector(7 downto 0);                     -- mem_req_tag
            mem_mem_req_wdata       : out   std_logic_vector(31 downto 0);                    -- mem_req_wdata
            mem_mem_resp_dack_tag   : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_dack_tag
            mem_mem_resp_data       : in    std_logic_vector(31 downto 0) := (others => 'X'); -- mem_resp_data
            mem_mem_resp_rack_tag   : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_rack_tag
            pio_in_port             : in    std_logic_vector(31 downto 0) := (others => 'X'); -- in_port
            pio_out_port            : out   std_logic_vector(31 downto 0);                    -- out_port
            spi_MISO                : in    std_logic                     := 'X';             -- MISO
            spi_MOSI                : out   std_logic;                                        -- MOSI
            spi_SCLK                : out   std_logic;                                        -- SCLK
            spi_SS_n                : out   std_logic;                                        -- SS_n
            sys_clock_clk           : in    std_logic                     := 'X';             -- clk
            sys_reset_reset_n       : in    std_logic                     := 'X';             -- reset_n
            uart_rxd                : in    std_logic                     := 'X';             -- rxd
            uart_txd                : out   std_logic;                                        -- txd
            uart_cts_n              : in    std_logic                     := 'X';             -- cts_n
            uart_rts_n              : out   std_logic                                        -- rts_n
        );
    end component nios_tester;

    component pll
        PORT
        (
            inclk0      : IN STD_LOGIC  := '0';
            c0          : OUT STD_LOGIC ;
            c1          : OUT STD_LOGIC ;
            locked      : OUT STD_LOGIC 
        );
    end component;

    signal por_n        : std_logic;
    signal ref_reset    : std_logic;
    signal por_count    : unsigned(19 downto 0) := (others => '0');
    
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal eth_reset    : std_logic;
    signal ulpi_reset_req : std_logic;
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    
    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic;
    signal is_idle          : std_logic;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;
    signal cpu_mem_req      : t_mem_req_32;
    signal cpu_mem_resp     : t_mem_resp_32;

    signal i2c_sda_i   : std_logic;
    signal i2c_sda_o   : std_logic;
    signal i2c_scl_i   : std_logic;
    signal i2c_scl_o   : std_logic;
    signal mdio_o      : std_logic;
        
    -- io buses
    signal io_irq           : std_logic;
    signal io_req           : t_io_req;
    signal io_resp          : t_io_resp;
    signal io_u2p_req       : t_io_req;
    signal io_u2p_resp      : t_io_resp;
    signal io_req_new_io    : t_io_req;
    signal io_resp_new_io   : t_io_resp;
    signal io_req_remote    : t_io_req;
    signal io_resp_remote   : t_io_resp;
    signal io_req_ddr2      : t_io_req;
    signal io_resp_ddr2     : t_io_resp;

    -- misc io
    signal audio_in_data           : std_logic_vector(31 downto 0) := (others => '0'); -- data
    signal audio_in_valid          : std_logic                     := '0';             -- valid
    signal audio_in_ready          : std_logic;                                        -- ready
    signal audio_out_data          : std_logic_vector(31 downto 0) := (others => '0'); -- data
    signal audio_out_valid         : std_logic;                                        -- valid
    signal audio_out_ready         : std_logic                     := '0';             -- ready

    signal jtag0_jtag_tck          : std_logic;                                        -- jtag_tck
    signal jtag0_jtag_tms          : std_logic;                                        -- jtag_tms
    signal jtag0_jtag_tdi          : std_logic;                                        -- jtag_tdi
    signal jtag0_jtag_tdo          : std_logic                     := 'X';             -- jtag_tdo

    signal jtag1_jtag_tck          : std_logic;                                        -- jtag_tck
    signal jtag1_jtag_tms          : std_logic;                                        -- jtag_tms
    signal jtag1_jtag_tdi          : std_logic;                                        -- jtag_tdi
    signal jtag1_jtag_tdo          : std_logic                     := 'X';             -- jtag_tdo

    signal pio_in_port             : std_logic_vector(31 downto 0) := (others => 'X'); -- in_port
    signal pio_out_port            : std_logic_vector(31 downto 0);                    -- out_port

    signal spi_MISO                : std_logic                     := 'X';             -- MISO
    signal spi_MOSI                : std_logic;                                        -- MOSI
    signal spi_SCLK                : std_logic;                                        -- SCLK
    signal spi_SS_n                : std_logic;                                        -- SS_n

    signal prim_uart_rxd           : std_logic := '1';
    signal prim_uart_txd           : std_logic := '1';
    signal prim_uart_cts_n         : std_logic := '1';
    signal prim_uart_rts_n         : std_logic := '1';

    signal io_uart_rxd             : std_logic := '1';
    signal io_uart_txd             : std_logic := '1';

    signal jtag_in_data            : std_logic_vector(7 downto 0)  := (others => 'X'); -- data
    signal jtag_in_valid           : std_logic                     := 'X';             -- valid
    signal jtag_in_ready           : std_logic;                                        -- ready

begin
    process(RMII_REFCLK)
    begin
        if rising_edge(RMII_REFCLK) then
            if por_count = X"FFFFF" then
                por_n <= '1';
            else
                por_n <= '0';
                por_count <= por_count + 1;
            end if;
        end if;
    end process;

    ref_reset <= not por_n;
    
    i_pll: pll port map (
        inclk0  => RMII_REFCLK, -- 50 MHz
        c0      => HUB_CLOCK, -- 24 MHz
        c1      => audio_clock, -- 12.245 MHz (47.831 kHz sample rate)
        locked  => open );

    i_audio_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => audio_clock,
        input       => sys_reset,
        input_c     => audio_reset  );
    
    i_ulpi_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => ulpi_clock,
        input       => ulpi_reset_req,
        input_c     => ulpi_reset_i  );

    i_eth_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => RMII_REFCLK,
        input       => sys_reset,
        input_c     => eth_reset  );

    i_nios: nios_tester
    port map (
        audio_in_data           => audio_in_data,
        audio_in_valid          => audio_in_valid,
        audio_in_ready          => audio_in_ready,
        audio_out_data          => audio_out_data,
        audio_out_valid         => audio_out_valid,
        audio_out_ready         => audio_out_ready,
        dummy_export            => '0',

        io_u2p_ack              => io_u2p_resp.ack,
        io_u2p_rdata            => io_u2p_resp.data,
        io_u2p_read             => io_u2p_req.read,
        io_u2p_wdata            => io_u2p_req.data,
        io_u2p_write            => io_u2p_req.write,
        unsigned(io_u2p_address) => io_u2p_req.address,
        io_u2p_irq              => '0',

        io_ack               => io_resp.ack,
        io_rdata             => io_resp.data,
        io_read              => io_req.read,
        io_wdata             => io_req.data,
        io_write             => io_req.write,
        unsigned(io_address) => io_req.address,
        io_irq               => io_irq,

        jtag0_jtag_tck          => jtag0_jtag_tck,
        jtag0_jtag_tms          => jtag0_jtag_tms,
        jtag0_jtag_tdi          => jtag0_jtag_tdi,
        jtag0_jtag_tdo          => jtag0_jtag_tdo,

        jtag1_jtag_tck          => jtag1_jtag_tck,
        jtag1_jtag_tms          => jtag1_jtag_tms,
        jtag1_jtag_tdi          => jtag1_jtag_tdi,
        jtag1_jtag_tdo          => jtag1_jtag_tdo,

        jtag_in_data            => jtag_in_data,
        jtag_in_valid           => jtag_in_valid,
        jtag_in_ready           => jtag_in_ready,

        unsigned(mem_mem_req_address) => cpu_mem_req.address,
        mem_mem_req_byte_en     => cpu_mem_req.byte_en,
        mem_mem_req_read_writen => cpu_mem_req.read_writen,
        mem_mem_req_request     => cpu_mem_req.request,
        mem_mem_req_tag         => cpu_mem_req.tag,
        mem_mem_req_wdata       => cpu_mem_req.data,
        mem_mem_resp_dack_tag   => cpu_mem_resp.dack_tag,
        mem_mem_resp_data       => cpu_mem_resp.data,
        mem_mem_resp_rack_tag   => cpu_mem_resp.rack_tag,

        pio_in_port             => pio_in_port,
        pio_out_port            => pio_out_port,

        spi_MISO                => spi_miso,
        spi_MOSI                => spi_mosi,
        spi_SCLK                => spi_sclk,
        spi_SS_n                => spi_ss_n,

        sys_clock_clk           => sys_clock,
        sys_reset_reset_n       => not sys_reset,

        uart_rxd                => prim_uart_rxd,
        uart_txd                => prim_uart_txd,
        uart_cts_n              => prim_uart_cts_n,
        uart_rts_n              => prim_uart_rts_n
    );

    UART_TXD <= prim_uart_txd;
    
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 8,
        g_range_hi => 10,
        g_ports    => 3
    )
    port map (
        clock      => sys_clock,
        req        => io_u2p_req,
        resp       => io_u2p_resp,
        reqs(0)    => io_req_new_io,
        reqs(1)    => io_req_ddr2,
        reqs(2)    => io_req_remote,
        resps(0)   => io_resp_new_io,
        resps(1)   => io_resp_ddr2,
        resps(2)   => io_resp_remote
    );

    i_memphy: entity work.ddr2_ctrl
    port map (
        ref_clock         => RMII_REFCLK,
        ref_reset         => ref_reset,
        sys_clock_o       => sys_clock,
        sys_reset_o       => sys_reset,
        clock             => sys_clock,
        reset             => sys_reset,
        io_req            => io_req_ddr2,
        io_resp           => io_resp_ddr2,
        inhibit           => '0', --memctrl_inhibit,
        is_idle           => is_idle,

        req               => mem_req,
        resp              => mem_resp,
        
        SDRAM_CLK         => SDRAM_CLK,
        SDRAM_CLKn        => SDRAM_CLKn,
        SDRAM_CKE         => SDRAM_CKE,
        SDRAM_ODT         => SDRAM_ODT,
        SDRAM_CSn         => SDRAM_CSn,
        SDRAM_RASn        => SDRAM_RASn,
        SDRAM_CASn        => SDRAM_CASn,
        SDRAM_WEn         => SDRAM_WEn,
        SDRAM_A           => SDRAM_A,
        SDRAM_BA          => SDRAM_BA(1 downto 0),
        SDRAM_DM          => SDRAM_DM,
        SDRAM_DQ          => SDRAM_DQ,
        SDRAM_DQS         => SDRAM_DQS
    );

    i_remote: entity work.update_io
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        slow_clock  => audio_clock,
        slow_reset  => audio_reset,
        io_req      => io_req_remote,
        io_resp     => io_resp_remote,
        flash_selck => FLASH_SELCK,
        flash_sel   => FLASH_SEL
    );

    i_u2p_io: entity work.u2p_io
    port map (
        clock      => sys_clock,
        reset      => sys_reset,
        io_req     => io_req_new_io,
        io_resp    => io_resp_new_io,
        mdc        => MDIO_CLK,
        mdio_i     => MDIO_DATA,
        mdio_o     => mdio_o,
        i2c_scl_i  => i2c_scl_i,
        i2c_scl_o  => i2c_scl_o,
        i2c_sda_i  => i2c_sda_i,
        i2c_sda_o  => i2c_sda_o,
        iec_i      => "1111",
        board_rev  => not BOARD_REVn,
        iec_o      => open,
        eth_irq_i  => ETH_IRQn,
        speaker_en => SPEAKER_ENABLE,
        hub_reset_n=> HUB_RESETn,
        ulpi_reset => ulpi_reset_req
    );

    i2c_scl_i   <= I2C_SCL and I2C_SCL_18;
    i2c_sda_i   <= I2C_SDA and I2C_SDA_18;
    I2C_SCL     <= '0' when i2c_scl_o = '0' else 'Z';
    I2C_SDA     <= '0' when i2c_sda_o = '0' else 'Z';
    I2C_SCL_18  <= '0' when i2c_scl_o = '0' else 'Z';
    I2C_SDA_18  <= '0' when i2c_sda_o = '0' else 'Z';
    MDIO_DATA   <= '0' when mdio_o = '0' else 'Z';

    i_logic: entity work.ultimate_logic_32
    generic map (
        g_version       => X"7F",
        g_simulation    => false,
        g_ultimate2plus => true,
        g_clock_freq    => 62_500_000,
        g_baud_rate     => 115_200,
        g_timer_rate    => 200_000,
        g_microblaze    => false,
        g_big_endian    => false,
        g_icap          => false,
        g_uart          => true,
        g_drive_1541    => false,
        g_drive_1541_2  => false,
        g_hardware_gcr  => false,
        g_ram_expansion => false,
        g_extended_reu  => false,
        g_stereo_sid    => false,
        g_hardware_iec  => false,
        g_iec_prog_tim  => false,
        g_c2n_streamer  => false,
        g_c2n_recorder  => false,
        g_cartridge     => false,
        g_command_intf  => false,
        g_drive_sound   => false,
        g_rtc_chip      => false,
        g_rtc_timer     => false,
        g_usb_host      => false,
        g_usb_host2     => true,
        g_spi_flash     => true,
        g_vic_copper    => false,
        g_video_overlay => false,
        g_sampler       => false,
        g_analyzer      => false,
        g_profiler      => true,
        g_rmii          => true )
    port map (
        -- globals
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
    
        ulpi_clock  => ulpi_clock,
        ulpi_reset  => ulpi_reset_i,
    
        ext_io_req  => io_req,
        ext_io_resp => io_resp,
        ext_mem_req => cpu_mem_req,
        ext_mem_resp=> cpu_mem_resp,
        cpu_irq     => io_irq,
        
        -- local bus side
        mem_req     => mem_req,
        mem_resp    => mem_resp,
        
        -- Debug UART
        UART_TXD    => io_uart_txd,
        UART_RXD    => io_uart_rxd,
        
        -- Flash Interface
        FLASH_CSn   => FLASH_CSn,
        FLASH_SCK   => FLASH_SCK,
        FLASH_MOSI  => FLASH_MOSI,
        FLASH_MISO  => FLASH_MISO,
    
        -- USB Interface (ULPI)
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        ULPI_DIR    => ULPI_DIR,
        ULPI_DATA   => ULPI_DATA,
    
        -- Ethernet Interface (RMII)
        eth_clock   => RMII_REFCLK, 
        eth_reset   => eth_reset,
        rmii_crs_dv => RMII_CRS_DV, 
        rmii_rxd    => RMII_RX_DATA,
        rmii_tx_en  => RMII_TX_EN,
        rmii_txd    => RMII_TX_DATA,

        -- Buttons
        BUTTON      => not BUTTON );


--    i_pwm0: entity work.sigma_delta_dac --delta_sigma_2to5
--    generic map (
--        g_left_shift => 2,
--        g_width => audio_speaker'length )
--    port map (
--        clock   => sys_clock,
--        reset   => sys_reset,
--        
--        dac_in  => audio_speaker,
--    
--        dac_out => SPEAKER_DATA );


    LED_MOTORn <= not pio_out_port(0);
    LED_DISKn  <= not pio_out_port(1);
    LED_CARTn  <= not pio_out_port(2);
    LED_SDACTn <= not pio_out_port(3);

    IEC_SRQ_IN <= 'Z';
    IEC_ATN    <= 'Z';
    IEC_DATA   <= 'Z';
    IEC_CLOCK  <= 'Z';

    pio_in_port(2 downto 0)  <= not BUTTON;

    ULPI_RESET <= por_n;

    b_audio: block
        signal audio_in_data_codec : std_logic_vector(31 downto 0) := (others => '0');
        signal stream_out_data  : std_logic_vector(23 downto 0);
        signal stream_out_tag   : std_logic_vector(0 downto 0);
        signal stream_out_valid : std_logic;
        signal stream_in_data   : std_logic_vector(23 downto 0);
        signal stream_in_tag    : std_logic_vector(0 downto 0);
        signal stream_in_ready  : std_logic;
        signal audio_out_full   : std_logic;
        signal class_d_sampled  : std_logic_vector(1 downto 0);
        signal class_d_data     : signed(17 downto 0);
        signal class_d_filtered : signed(17 downto 0);
        signal class_d_std      : std_logic_vector(31 downto 0) := (others => '0');
    begin
        process(sys_clock)
        begin
            if rising_edge(sys_clock) then
                class_d_sampled <= pio_in_port(6 downto 5);
                if class_d_sampled = "10" then
                    class_d_data <= to_signed(110000, 18);
                elsif class_d_sampled = "01" then
                    class_d_data <= to_signed(-110000, 18);
                else
                    class_d_data <= to_signed(0, 18);
                end if;
            end if;
        end process;
        class_d_std(25) <= '1';
        class_d_std(23 downto 6) <= std_logic_vector(class_d_filtered);

        -- multiplexer and byte reversal
        audio_in_data <= byte_swap(class_d_std, true) when pio_out_port(31) = '1' else
                         byte_swap(audio_in_data_codec, true);
                
        i_filt_left: entity work.lp_filter
        port map (
            clock     => sys_clock,
            reset     => sys_reset,
            signal_in => class_d_data,
            low_pass  => class_d_filtered );

        i_aout: entity work.async_fifo_ft
        generic map (
            g_depth_bits => 4,
            g_data_width => 25
        )
        port map(
            wr_clock     => sys_clock,
            wr_reset     => sys_reset,
            wr_en        => audio_out_valid,
            wr_din(24)   => audio_out_data(0),
            wr_din(23 downto 16) => audio_out_data(15 downto 8),
            wr_din(15 downto 8)  => audio_out_data(23 downto 16),
            wr_din(7 downto 0)   => audio_out_data(31 downto 24),
            wr_full      => audio_out_full,
            
            rd_clock     => audio_clock,
            rd_reset     => audio_reset,
            rd_next      => stream_in_ready,
            rd_dout(24 downto 24) => stream_in_tag,
            rd_dout(23 downto 0) => stream_in_data,
            rd_valid     => open --stream_in_valid
        );
        audio_out_ready <= not audio_out_full;

        i_ain: entity work.synchronizer_gzw
        generic map(
            g_width     => 25,
            g_fast      => false
        )
        port map(
            tx_clock    => audio_clock,
            tx_push     => stream_out_valid,
            tx_data(24 downto 24) => stream_out_tag,
            tx_data(23 downto 0) => stream_out_data,
            tx_done     => open,
            rx_clock    => sys_clock,
            rx_new_data => audio_in_valid,
            rx_data     => audio_in_data_codec(24 downto 0)
        );

        i2s: entity work.i2s_serializer
        port map (
            clock            => audio_clock,
            reset            => audio_reset,
            i2s_out          => AUDIO_SDO,
            i2s_in           => AUDIO_SDI,
            i2s_bclk         => AUDIO_BCLK,
            i2s_fs           => AUDIO_LRCLK,
            stream_out_data  => stream_out_data,
            stream_out_tag   => stream_out_tag,
            stream_out_valid => stream_out_valid,
            stream_in_data   => stream_in_data,
            stream_in_tag    => stream_in_tag,
            stream_in_valid  => '1',
            stream_in_ready  => stream_in_ready );

        AUDIO_MCLK <= audio_clock;

    end block;    
    
    SLOT_BUFFER_ENn <= '0'; -- once configured, we can connect

    SLOT_ROMHn     <= prim_uart_rts_n;
                      prim_uart_cts_n   <= SLOT_RSTn;
                      prim_uart_rxd     <= SLOT_IRQn;
                      io_uart_rxd       <= SLOT_IRQn;
    SLOT_NMIn      <= prim_uart_txd and io_uart_txd;
    
    pio_in_port(4) <= SLOT_PHI2;  --        <= usb_to_host_vbus; // B5 input only on FPGA
    pio_in_port(5) <= SLOT_DOTCLK; --       <= jig_spkr.n;       // T6 input only on FPGA
    pio_in_port(6) <= SLOT_RWn; --          <= jig_spkr.p;
    -- todo: add another uart
    -- IO1n        <= jig_rxd;
    -- GAMEn       <= jig_txd;
    pio_in_port(8)  <= SLOT_ADDR(14);  -- usb_vcc[1];
    pio_in_port(9)  <= SLOT_ADDR(13);  -- usb_vcc[2];
    pio_in_port(10) <= SLOT_ADDR(15);  -- usb_vcc[3];
    pio_in_port(11) <= SLOT_ADDR(9);   -- jtag_vcc;
    
    SLOT_ADDR(8)     <= not pio_out_port(4); -- dut_en_n;
    SLOT_EXROMn      <= not pio_out_port(5); -- jig_v50_5v_en_n;
    SLOT_IO2n        <= not pio_out_port(6); -- jig_c64_5v_en_n;
    SLOT_ROMLn       <= not pio_out_port(7); -- jig_ext_5v_en_n;

    pio_in_port(12)  <= SLOT_BA; -- adc_eoc_n; // input only fpga T12

    spi_miso         <= SLOT_DMAn;
    SLOT_ADDR(7)     <= spi_mosi;
    SLOT_DATA(7)     <= spi_sclk;
    SLOT_DATA(6)     <= spi_ss_n;

    jtag0_jtag_tdo   <= SLOT_ADDR(6);
    SLOT_DATA(5)     <= jtag0_jtag_tdi when pio_out_port(16) = '0' else 'Z';
    SLOT_ADDR(5)     <= jtag0_jtag_tms when pio_out_port(16) = '0' else 'Z';
    SLOT_DATA(4)     <= jtag0_jtag_tck when pio_out_port(16) = '0' else 'Z';

    jtag1_jtag_tdo   <= SLOT_DATA(3);
    SLOT_DATA(2)     <= jtag1_jtag_tdi;
    SLOT_DATA(1)     <= jtag1_jtag_tms;
    SLOT_DATA(0)     <= jtag1_jtag_tck; 

    SLOT_ADDR(4)     <= not pio_out_port(8); -- exp_sclr_n
    SLOT_ADDR(3)     <= not pio_out_port(9); -- exp_oe_n
    SLOT_ADDR(2)     <= pio_out_port(10); -- exp_rclk;
    SLOT_ADDR(1)     <= pio_out_port(11); -- exp_sclk;
    SLOT_ADDR(0)     <= pio_out_port(12); -- exp_data;

    SLOT_ADDR(10)    <= pio_out_port(15); -- load 3.3V
    SLOT_ADDR(11)    <= pio_out_port(14); -- load 1.8V
    SLOT_ADDR(12)    <= pio_out_port(13); -- load 1.2V

    b_jtag_debug: block
        constant c_test_logic_reset : integer :=  0;
        constant c_run_test_idle    : integer :=  1;
        constant c_select_dr        : integer :=  2;    
        constant c_capture_dr       : integer :=  3;    
        constant c_shift_dr         : integer :=  4;    
        constant c_exit1_dr         : integer :=  5;
        constant c_pause_dr         : integer :=  6;
        constant c_exit2_dr         : integer :=  7;
        constant c_update_dr        : integer :=  8;
        constant c_select_ir        : integer :=  9;    
        constant c_capture_ir       : integer := 10;    
        constant c_shift_ir         : integer := 11;    
        constant c_exit1_ir         : integer := 12;
        constant c_pause_ir         : integer := 13;
        constant c_exit2_ir         : integer := 14;
        constant c_update_ir        : integer := 15;
    
        type t_next_jtag_state is record
            tms0    : integer range 0 to 15;
            tms1    : integer range 0 to 15;
        end record;
        
        type t_next_jtag_state_array is array (natural range <>) of t_next_jtag_state;
        
        constant c_next_jtag_state    : t_next_jtag_state_array(0 to 15) := (
            c_test_logic_reset => ( tms0 => c_run_test_idle,  tms1 => c_test_logic_reset ),
            c_run_test_idle    => ( tms0 => c_run_test_idle,  tms1 => c_select_dr ),
    
            c_select_dr        => ( tms0 => c_capture_dr,     tms1 => c_select_ir ),    
            c_capture_dr       => ( tms0 => c_shift_dr,       tms1 => c_exit1_dr  ),    
            c_shift_dr         => ( tms0 => c_shift_dr,       tms1 => c_exit1_dr  ),    
            c_exit1_dr         => ( tms0 => c_pause_dr,       tms1 => c_update_dr ),
            c_pause_dr         => ( tms0 => c_pause_dr,       tms1 => c_exit2_dr  ),
            c_exit2_dr         => ( tms0 => c_shift_dr,       tms1 => c_update_dr ),
            c_update_dr        => ( tms0 => c_run_test_idle,  tms1 => c_select_dr ),
    
            c_select_ir        => ( tms0 => c_capture_ir,     tms1 => c_test_logic_reset ),    
            c_capture_ir       => ( tms0 => c_shift_ir,       tms1 => c_exit1_ir  ),    
            c_shift_ir         => ( tms0 => c_shift_ir,       tms1 => c_exit1_ir  ),    
            c_exit1_ir         => ( tms0 => c_pause_ir,       tms1 => c_update_ir ),
            c_pause_ir         => ( tms0 => c_pause_ir,       tms1 => c_exit2_ir  ),
            c_exit2_ir         => ( tms0 => c_shift_ir,       tms1 => c_update_ir ),
            c_update_ir        => ( tms0 => c_run_test_idle,  tms1 => c_select_dr ) );

        signal jtag_state   : integer range 0 to 15 := 0;
        signal edge         : std_logic;
        signal jtag_tck_c1, jtag_tck_c2, jtag_tck   : std_logic;    
        signal jtag_tms_c1, jtag_tms_c2, jtag_tms   : std_logic;    
        signal jtag_tdi_c1, jtag_tdi_c2, jtag_tdi   : std_logic;    
        signal jtag_tdo_c1, jtag_tdo_c2, jtag_tdo   : std_logic;    
    begin
        process(sys_clock) 
        begin
            if rising_edge(sys_clock) then
                edge <= '0';
                
                jtag_tck_c1 <= SLOT_DATA(4);
                jtag_tck_c2 <= jtag_tck_c1;
                jtag_tck <= jtag_tck_c2;
                
                jtag_tms_c1 <= SLOT_ADDR(5);
                jtag_tms_c2 <= jtag_tms_c1;
                jtag_tms <= jtag_tms_c2;
                
                jtag_tdi_c1 <= SLOT_DATA(5);
                jtag_tdi_c2 <= jtag_tdi_c1;
                jtag_tdi <= jtag_tdi_c2;
                
                jtag_tdo_c1 <= SLOT_ADDR(6);
                jtag_tdo_c2 <= jtag_tdo_c1;
                jtag_tdo <= jtag_tdo_c2;
                
                if jtag_tck = '0' and jtag_tck_c2 = '1' then
                    edge <= '1';
                end if;
                
                if edge = '1' then
                    if jtag_tms = '0' then
                        jtag_state <= c_next_jtag_state(jtag_state).tms0;
                    else
                        jtag_state <= c_next_jtag_state(jtag_state).tms1;
                    end if;
                end if;
            end if;
        end process;
    
        jtag_in_data(7 downto 4) <= std_logic_vector(to_unsigned(jtag_state, 4));
        jtag_in_data(3) <= jtag_tck;
        jtag_in_data(2) <= jtag_tms;
        jtag_in_data(1) <= jtag_tdo;
        jtag_in_data(0) <= jtag_tdi;
        jtag_in_valid   <= edge and pio_out_port(16);
    end block;

end architecture;
