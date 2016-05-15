-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT TECHNOLUTION BV, GOUDA NL
-- | =======          I                   ==          I    =
-- |    I             I                    I          I
-- |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
-- |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
-- |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
-- |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
-- |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
-- |                 +---------------------------------------------------+
-- +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
--      |            |             ++++++++++++++++++++++++++++++++++++++|
--      +------------+                          +++++++++++++++++++++++++|
--                                                         ++++++++++++++|
--              A U T O M A T I O N     T E C H N O L O G Y         +++++|
--
-------------------------------------------------------------------------------
-- Title      : u2p_nios
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@technolution.eu>
-------------------------------------------------------------------------------
-- Description: Toplevel 
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    
entity u2p_nios is
port (
    -- slot side
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_RSTn        : inout std_logic;
--    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : inout std_logic;
    SLOT_BA          : in    std_logic;
    SLOT_DMAn        : out   std_logic;
    SLOT_EXROMn      : inout std_logic;
    SLOT_GAMEn       : inout std_logic;
    SLOT_ROMHn       : in    std_logic;
    SLOT_ROMLn       : in    std_logic;
    SLOT_IO1n        : in    std_logic;
    SLOT_IO2n        : in    std_logic;
    SLOT_IRQn        : inout std_logic;
    SLOT_NMIn        : inout std_logic;
    SLOT_VCC         : in    std_logic;
    
    -- memory
    SDRAM_A     : out   std_logic_vector(13 downto 0); -- DRAM A
    SDRAM_BA    : out   std_logic_vector(2 downto 0) := (others => '0');
    SDRAM_DQ    : inout std_logic_vector(7 downto 0);
    SDRAM_CSn   : out   std_logic;
    SDRAM_RASn  : out   std_logic;
    SDRAM_CASn  : out   std_logic;
    SDRAM_WEn   : out   std_logic;
    SDRAM_DM    : out   std_logic;
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

    -- Cassette Interface
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;

architecture rtl of u2p_nios is
    component nios is
        port (
            altmemddr_0_auxfull_clk        : out   std_logic;                                        -- clk
            altmemddr_0_auxhalf_clk        : out   std_logic;                                        -- clk
            clk50_clk                      : in    std_logic                     := 'X';             -- clk
            io_ack                         : in    std_logic                     := 'X';             -- ack
            io_rdata                       : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
            io_read                        : out   std_logic;                                        -- read
            io_wdata                       : out   std_logic_vector(7 downto 0);                     -- wdata
            io_write                       : out   std_logic;                                        -- write
            io_address                     : out   std_logic_vector(19 downto 0);                    -- address
            io_irq                         : in    std_logic                     := 'X';             -- irq
            mem32_address                  : in    std_logic_vector(25 downto 0) := (others => 'X'); -- address
            mem32_direction                : in    std_logic                     := 'X';             -- direction
            mem32_byte_en                  : in    std_logic_vector(3 downto 0)  := (others => 'X'); -- byte_en
            mem32_wdata                    : in    std_logic_vector(31 downto 0) := (others => 'X'); -- wdata
            mem32_request                  : in    std_logic                     := 'X';             -- request
            mem32_tag                      : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- tag
            mem32_dack_tag                 : out   std_logic_vector(7 downto 0);                     -- dack_tag
            mem32_rdata                    : out   std_logic_vector(31 downto 0);                    -- rdata
            mem32_rack                     : out   std_logic;                                        -- rack
            mem32_rack_tag                 : out   std_logic_vector(7 downto 0);                     -- rack_tag
            mem_external_local_refresh_ack : out   std_logic;                                        -- local_refresh_ack
            mem_external_local_init_done   : out   std_logic;                                        -- local_init_done
            mem_external_reset_phy_clk_n   : out   std_logic;                                        -- reset_phy_clk_n
            memory_mem_odt                 : out   std_logic_vector(0 downto 0);                     -- mem_odt
            memory_mem_clk                 : inout std_logic_vector(0 downto 0)  := (others => 'X'); -- mem_clk
            memory_mem_clk_n               : inout std_logic_vector(0 downto 0)  := (others => 'X'); -- mem_clk_n
            memory_mem_cs_n                : out   std_logic_vector(0 downto 0);                     -- mem_cs_n
            memory_mem_cke                 : out   std_logic_vector(0 downto 0);                     -- mem_cke
            memory_mem_addr                : out   std_logic_vector(13 downto 0);                    -- mem_addr
            memory_mem_ba                  : out   std_logic_vector(1 downto 0);                     -- mem_ba
            memory_mem_ras_n               : out   std_logic;                                        -- mem_ras_n
            memory_mem_cas_n               : out   std_logic;                                        -- mem_cas_n
            memory_mem_we_n                : out   std_logic;                                        -- mem_we_n
            memory_mem_dq                  : inout std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_dq
            memory_mem_dqs                 : inout std_logic_vector(0 downto 0)  := (others => 'X'); -- mem_dqs
            memory_mem_dm                  : out   std_logic_vector(0 downto 0);                     -- mem_dm
            pio_in_port                    : in    std_logic_vector(31 downto 0) := (others => 'X'); -- in_port
            pio_out_port                   : out   std_logic_vector(31 downto 0);                    -- out_port
            reset_reset_n                  : in    std_logic                     := 'X';             -- reset_n
            sys_clock_clk                  : out   std_logic;                                        -- clk
            sys_reset_reset_n              : out   std_logic                                         -- reset_n
        );
    end component nios;

    component pll
        PORT
        (
            inclk0      : IN STD_LOGIC  := '0';
            c0          : OUT STD_LOGIC ;
            c1          : OUT STD_LOGIC ;
            locked      : OUT STD_LOGIC 
        );
    end component;

    signal init_done    : std_logic;
    signal por_n        : std_logic;
    signal por_count    : unsigned(23 downto 0) := (others => '0');

    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal sys_reset_n  : std_logic;
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    
    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;

    signal uart_txd_from_logic  : std_logic;
    signal pio_in_port          : std_logic_vector(31 downto 0) := (others => '0');
    signal pio_out_port         : std_logic_vector(31 downto 0);
    
    -- IEC open drain
    signal iec_atn_o   : std_logic;
    signal iec_data_o  : std_logic;
    signal iec_clock_o : std_logic;
    signal iec_srq_o   : std_logic;

    signal io_irq       : std_logic;
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    signal io_req_addr  : std_logic_vector(19 downto 0);

    signal eth_rx_data         : std_logic_vector(7 downto 0);
    signal eth_rx_sof          : std_logic;
    signal eth_rx_eof          : std_logic;
    signal eth_rx_valid        : std_logic;

    signal eth_tx_data         : std_logic_vector(7 downto 0);
    signal eth_tx_sof          : std_logic;
    signal eth_tx_eof          : std_logic;
    signal eth_tx_valid        : std_logic;
    signal eth_tx_ready        : std_logic;

begin
    process(RMII_REFCLK)
    begin
        if rising_edge(RMII_REFCLK) then
            if por_count = X"FFFFFF" then
                por_n <= '1';
            else
                por_n <= '0';
                por_count <= por_count + 1;
            end if;
        end if;
    end process;

    sys_reset <= not sys_reset_n;
    
    process(audio_clock)
    begin
        if rising_edge(audio_clock) then
            audio_reset <= not sys_reset_n;
        end if;
    end process;

    i_nios: nios
    port map(
        clk50_clk                      => RMII_REFCLK,
        reset_reset_n                  => por_n,
        sys_clock_clk                  => sys_clock,
        sys_reset_reset_n              => sys_reset_n,

        mem_external_local_refresh_ack => open,
        mem_external_local_init_done   => init_done,
        mem_external_reset_phy_clk_n   => open,

        memory_mem_odt(0)              => SDRAM_ODT,
        memory_mem_clk(0)              => SDRAM_CLK,
        memory_mem_clk_n(0)            => SDRAM_CLKn,
        memory_mem_cs_n(0)             => SDRAM_CSn,
        memory_mem_cke(0)              => SDRAM_CKE,
        memory_mem_addr                => SDRAM_A,
        memory_mem_ba                  => SDRAM_BA(1 downto 0),
        memory_mem_ras_n               => SDRAM_RASn,
        memory_mem_cas_n               => SDRAM_CASn,
        memory_mem_we_n                => SDRAM_WEn,
        memory_mem_dq                  => SDRAM_DQ,
        memory_mem_dqs(0)              => SDRAM_DQS,
        memory_mem_dm(0)               => SDRAM_DM,

        mem32_address                  => std_logic_vector(mem_req.address),
        mem32_direction                => mem_req.read_writen,
        mem32_byte_en                  => mem_req.byte_en,
        mem32_wdata                    => mem_req.data,
        mem32_request                  => mem_req.request,
        mem32_tag                      => mem_req.tag,
        mem32_dack_tag                 => mem_resp.dack_tag,
        mem32_rdata                    => mem_resp.data,
        mem32_rack                     => mem_resp.rack,
        mem32_rack_tag                 => mem_resp.rack_tag,

        pio_in_port                    => pio_in_port,
        pio_out_port                   => pio_out_port,

        io_ack                         => io_resp.ack,
        io_rdata                       => io_resp.data,
        io_read                        => io_req.read,
        io_wdata                       => io_req.data,
        io_write                       => io_req.write,
        io_address                     => io_req_addr,
        io_irq                         => io_irq
    );

    io_req.address <= unsigned(io_req_addr);

    pio_in_port(0) <= I2C_SCL;
    pio_in_port(1) <= I2C_SDA;
    pio_in_port(5) <= MDIO_DATA;
    pio_in_port(9) <= ETH_IRQn;
    
    I2C_SCL        <= '0' when pio_out_port(0) = '0' else 'Z';
    I2C_SDA        <= '0' when pio_out_port(1) = '0' else 'Z';
    FLASH_SEL      <= pio_out_port(2);
    FLASH_SELCK    <= pio_out_port(3);
    MDIO_CLK       <= pio_out_port(4);
    MDIO_DATA      <= '0' when pio_out_port(5) = '0' else 'Z';
    ETH_RESETn     <= not pio_out_port(6); -- we may run into issues with the clock!
    HUB_RESETn     <= pio_out_port(7);
    SPEAKER_ENABLE <= pio_out_port(8);
    

    i_logic: entity work.ultimate_logic_32
    generic map (
        g_version       => X"44",
        g_simulation    => false,
        g_clock_freq    => 62_500_000,
        g_baud_rate     => 115_200,
        g_timer_rate    => 200_000,
        g_microblaze    => false,
        g_big_endian    => false,
        g_icap          => false,
        g_uart          => true,
        g_drive_1541    => true,
        g_drive_1541_2  => false,
        g_hardware_gcr  => true,
        g_ram_expansion => true,
        g_extended_reu  => false,
        g_stereo_sid    => false,
        g_hardware_iec  => true,
        g_iec_prog_tim  => false,
        g_c2n_streamer  => true,
        g_c2n_recorder  => true,
        g_cartridge     => true,
        g_command_intf  => true,
        g_drive_sound   => true,
        g_rtc_chip      => false,
        g_rtc_timer     => false,
        g_usb_host      => false,
        g_usb_host2     => true,
        g_spi_flash     => true,
        g_vic_copper    => false,
        g_video_overlay => false,
        g_sampler       => false,
        g_analyzer      => false,
        g_profiler      => true )
    port map (
        -- globals
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
    
        ulpi_clock  => ulpi_clock,
        ulpi_reset  => ulpi_reset_i,
    
        ext_io_req  => io_req,
        ext_io_resp => io_resp,
        cpu_irq     => io_irq,
        
        -- slot side
        BUFFER_ENn  => open,
        PHI2        => SLOT_PHI2,
        DOTCLK      => SLOT_DOTCLK,
        RSTn        => SLOT_RSTn,
                                   
        SLOT_ADDR   => SLOT_ADDR,
        SLOT_DATA   => SLOT_DATA,
        RWn         => SLOT_RWn,
        BA          => SLOT_BA,
        DMAn        => SLOT_DMAn,
                                   
        EXROMn      => SLOT_EXROMn,
        GAMEn       => SLOT_GAMEn,
                                   
        ROMHn       => SLOT_ROMHn,
        ROMLn       => SLOT_ROMLn,
        IO1n        => SLOT_IO1n,
        IO2n        => SLOT_IO2n,

        IRQn        => SLOT_IRQn,
        NMIn        => SLOT_NMIn,
        
        -- local bus side
        mem_inhibit => memctrl_inhibit,
        mem_req     => mem_req,
        mem_resp    => mem_resp,
                 
        -- PWM outputs (for audio)
        PWM_OUT(0)  => SPEAKER_DATA,
        PWM_OUT(1)  => open,
    
        -- IEC bus
        iec_reset_i => IEC_RESET,
        iec_atn_i   => IEC_ATN,
        iec_data_i  => IEC_DATA,
        iec_clock_i => IEC_CLOCK,
        iec_srq_i   => IEC_SRQ_IN,
                                  
        iec_reset_o => open,
        iec_atn_o   => iec_atn_o,
        iec_data_o  => iec_data_o,
        iec_clock_o => iec_clock_o,
        iec_srq_o   => iec_srq_o,
                                    
        DISK_ACTn   => LED_DISKn, -- activity LED
        CART_LEDn   => LED_CARTn,
        SDACT_LEDn  => LED_SDACTn,
        MOTOR_LEDn  => LED_MOTORn,
        
        -- Debug UART
        UART_TXD    => uart_txd_from_logic,
        UART_RXD    => UART_RXD,
        
        -- SD Card Interface
        SD_SSn      => open,
        SD_CLK      => open,
        SD_MOSI     => open,
        SD_MISO     => '1',
        SD_CARDDETn => '1',
        SD_DATA     => open,
        
        -- RTC Interface
        RTC_CS      => open,
        RTC_SCK     => open,
        RTC_MOSI    => open,
        RTC_MISO    => '1',
    
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
    
        -- Cassette Interface
        CAS_MOTOR   => CAS_MOTOR,
        CAS_SENSE   => CAS_SENSE,
        CAS_READ    => CAS_READ,
        CAS_WRITE   => CAS_WRITE,
        
        -- Buttons
        BUTTON      => button_i );

    IEC_ATN    <= '0' when iec_atn_o   = '0' else 'Z';
    IEC_DATA   <= '0' when iec_data_o  = '0' else 'Z';
    IEC_CLOCK  <= '0' when iec_clock_o = '0' else 'Z';
    IEC_SRQ_IN <= '0' when iec_srq_o   = '0' else 'Z';

    button_i <= not BUTTON;

    -- stub
--    RMII_TX_EN   <= RMII_RX_ER and RMII_REFCLK and RMII_CRS_DV and ETH_IRQn and SLOT_VCC;
--    RMII_TX_DATA <= RMII_RX_DATA;

    i_rmii: entity work.rmii_transceiver
    port map (
        clock        => RMII_REFCLK,
        reset        => not por_n,
        rmii_crs_dv  => RMII_CRS_DV,
        rmii_rxd     => RMII_RX_DATA,
        rmii_tx_en   => RMII_TX_EN,
        rmii_txd     => RMII_TX_DATA,

        eth_rx_data  => eth_rx_data,
        eth_rx_sof   => eth_rx_sof,
        eth_rx_eof   => eth_rx_eof,
        eth_rx_valid => eth_rx_valid,
        eth_tx_data  => eth_tx_data,
        eth_tx_sof   => eth_tx_sof,
        eth_tx_eof   => eth_tx_eof,
        eth_tx_valid => eth_tx_valid,
        eth_tx_ready => eth_tx_ready,
        ten_meg_mode => '0' );

    i_packet: entity work.packet_generator
    port map (
        clock        => RMII_REFCLK,
        reset        => not por_n,
        eth_tx_data  => eth_tx_data,
        eth_tx_sof   => eth_tx_sof,
        eth_tx_eof   => eth_tx_eof,
        eth_tx_valid => eth_tx_valid,
        eth_tx_ready => eth_tx_ready );
    


    process(ulpi_clock)
    begin
        if rising_edge(ulpi_clock) then
            ulpi_reset_i <= sys_reset;
        end if;
    end process;

    ULPI_RESET <= por_n;

    i_pll: pll port map (
        inclk0  => RMII_REFCLK, -- 50 MHz
        c0      => HUB_CLOCK, -- 24 MHz
        c1      => audio_clock, -- 12.245 MHz (47.831 kHz sample rate)
        locked  => open );


    UART_TXD <= uart_txd_from_logic; -- and uart_txd_from_qsys;

    b_audio: block
        signal audio_left       : signed(19 downto 0);
        signal audio_right      : signed(19 downto 0);
        signal stream_out_data  : std_logic_vector(23 downto 0);
        signal stream_out_tag   : std_logic_vector(0 downto 0);
        signal stream_out_valid : std_logic;
        signal stream_in_data   : std_logic_vector(23 downto 0);
        signal stream_in_tag    : std_logic_vector(0 downto 0);
        signal stream_in_ready  : std_logic;
    begin
        
        -- audio stuff for testing
        i_sineL: entity work.sine_osc
        port map (
            clock  => audio_clock,
            reset  => audio_reset,
            freq   => X"0235",
            sine   => audio_left,
            cosine => open );
        
        i_sineR: entity work.sine_osc
        port map (
            clock  => audio_clock,
            reset  => audio_reset,
            freq   => X"0123",
            sine   => audio_right,
            cosine => open );
    
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

        process(audio_clock)
        begin
            if rising_edge(audio_clock) then
                if pio_out_port(15) = '0' then
                    if stream_in_ready = '1' then
                        if stream_in_tag(0) = '0' then
                            stream_in_tag(0) <= '1';
                            stream_in_data <= std_logic_vector(audio_right) & "0000";
                        else
                            stream_in_tag(0) <= '0';
                            stream_in_data <= std_logic_vector(audio_left) & "0000";
                        end if;
                    end if;
                else
                    stream_in_tag <= stream_out_tag;
                    stream_in_data <= stream_in_data;
                end if;
                if audio_reset = '1' then
                    stream_in_tag(0) <= '1';
                end if;
            end if; 
        end process;
    end block;    
    
end architecture;
