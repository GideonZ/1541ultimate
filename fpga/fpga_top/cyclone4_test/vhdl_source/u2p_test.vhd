-------------------------------------------------------------------------------
-- Title      : u2p_dut
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Toplevel for u2p_dut.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    
entity u2p_test is
port (
    -- slot side
    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_RSTn        : inout std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : inout std_logic;
    SLOT_BA          : in    std_logic;
    SLOT_DMAn        : inout std_logic;
    SLOT_EXROMn      : inout std_logic;
    SLOT_GAMEn       : inout std_logic;
    SLOT_ROMHn       : inout std_logic;
    SLOT_ROMLn       : inout std_logic;
    SLOT_IO1n        : inout std_logic;
    SLOT_IO2n        : inout std_logic;
    SLOT_IRQn        : inout std_logic;
    SLOT_NMIn        : inout std_logic;
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
    IEC_RESET   : inout std_logic;
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
    CAS_MOTOR   : inout std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;

architecture rtl of u2p_test is
    component nios_dut is
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
            jtag_io_input_vector    : in    std_logic_vector(47 downto 0) := (others => 'X'); -- input_vector
            jtag_io_output_vector   : out   std_logic_vector(7 downto 0);                     -- output_vector
            jtag_test_clocks_clock_1 : in    std_logic                     := 'X';             -- clock_1
            jtag_test_clocks_clock_2 : in    std_logic                     := 'X';             -- clock_2
            mem_mem_req_address     : out   std_logic_vector(25 downto 0);                    -- mem_req_address
            mem_mem_req_byte_en     : out   std_logic_vector(3 downto 0);                     -- mem_req_byte_en
            mem_mem_req_read_writen : out   std_logic;                                        -- mem_req_read_writen
            mem_mem_req_request     : out   std_logic;                                        -- mem_req_request
            mem_mem_req_tag         : out   std_logic_vector(7 downto 0);                     -- mem_req_tag
            mem_mem_req_wdata       : out   std_logic_vector(31 downto 0);                    -- mem_req_wdata
            mem_mem_resp_dack_tag   : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_dack_tag
            mem_mem_resp_data       : in    std_logic_vector(31 downto 0) := (others => 'X'); -- mem_resp_data
            mem_mem_resp_rack_tag   : in    std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_rack_tag
            pio1_export             : in    std_logic_vector(31 downto 0) := (others => 'X'); -- export
            pio2_export             : in    std_logic_vector(19 downto 0) := (others => 'X'); -- export
            pio3_export             : out   std_logic_vector(7 downto 0);                     -- export
            sys_clock_clk           : in    std_logic                     := 'X';             -- clk
            sys_reset_reset_n       : in    std_logic                     := 'X';             -- reset_n
            uart_rxd                : in    std_logic                     := 'X';             -- rxd
            uart_txd                : out   std_logic;                                        -- txd
            uart_cts_n              : in    std_logic                     := 'X';             -- cts_n
            uart_rts_n              : out   std_logic                                         -- rts_n
        );
    end component nios_dut;

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
    signal sys_count    : unsigned(23 downto 0) := (others => '0');    
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal eth_reset    : std_logic;
    signal ulpi_reset_req : std_logic;
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    
    -- memory controller interconnect
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
    signal io_irq   : std_logic;
    signal io_req   : t_io_req;
    signal io_resp  : t_io_resp;
    signal io_u2p_req   : t_io_req;
    signal io_u2p_resp  : t_io_resp;
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
    signal audio_speaker           : signed(15 downto 0);
    
    signal pll_locked              : std_logic;

    signal pio1_export             : std_logic_vector(31 downto 0) := (others => '0'); -- in_port
    signal pio2_export             : std_logic_vector(19 downto 0) := (others => '0'); -- in_port
    signal pio3_export             : std_logic_vector(7 downto 0);                     -- out_port

    signal prim_uart_rxd           : std_logic := '1';
    signal prim_uart_txd           : std_logic := '1';
    signal prim_uart_cts_n         : std_logic := '1';
    signal prim_uart_rts_n         : std_logic := '1';

    signal io_uart_rxd             : std_logic := '1';
    signal io_uart_txd             : std_logic := '1';

    signal slot_test_vector     : std_logic_vector(47 downto 0);
    signal jtag_write_vector    : std_logic_vector(7 downto 0);

    signal test_shift           : std_logic_vector(47 downto 0);
begin
    process(RMII_REFCLK)
    begin
        if rising_edge(RMII_REFCLK) then
            if jtag_write_vector(7) = '1' then
                por_count <= (others => '0');
                por_n <= '0';
            elsif por_count = X"FFFFF" then
                por_n <= '1';
            else
                por_count <= por_count + 1;
                por_n <= '0';
            end if;
        end if;
    end process;

    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            sys_count <= sys_count + 1;
        end if;
    end process;

    ref_reset <= not por_n;
    
    i_pll: pll port map (
        inclk0  => RMII_REFCLK, -- 50 MHz
        c0      => HUB_CLOCK, -- 24 MHz
        c1      => audio_clock, -- 12.245 MHz (47.831 kHz sample rate)
        locked  => pll_locked );

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

    i_nios: nios_dut
    port map (
        audio_in_data           => audio_in_data,
        audio_in_valid          => audio_in_valid,
        audio_in_ready          => audio_in_ready,
        audio_out_data          => audio_out_data,
        audio_out_valid         => audio_out_valid,
        audio_out_ready         => audio_out_ready,
        dummy_export            => '0',

        io_ack               => io_resp.ack,
        io_rdata             => io_resp.data,
        io_read              => io_req.read,
        io_wdata             => io_req.data,
        io_write             => io_req.write,
        unsigned(io_address) => io_req.address,
        io_irq               => io_irq,

        io_u2p_ack              => io_u2p_resp.ack,
        io_u2p_rdata            => io_u2p_resp.data,
        io_u2p_read             => io_u2p_req.read,
        io_u2p_wdata            => io_u2p_req.data,
        io_u2p_write            => io_u2p_req.write,
        unsigned(io_u2p_address) => io_u2p_req.address,
        io_u2p_irq              => '0',

        jtag_io_input_vector     => slot_test_vector,
        jtag_io_output_vector    => jtag_write_vector,
        jtag_test_clocks_clock_1 => RMII_REFCLK,
        jtag_test_clocks_clock_2 => ULPI_CLOCK,

        unsigned(mem_mem_req_address) => cpu_mem_req.address,
        mem_mem_req_byte_en     => cpu_mem_req.byte_en,
        mem_mem_req_read_writen => cpu_mem_req.read_writen,
        mem_mem_req_request     => cpu_mem_req.request,
        mem_mem_req_tag         => cpu_mem_req.tag,
        mem_mem_req_wdata       => cpu_mem_req.data,
        mem_mem_resp_dack_tag   => cpu_mem_resp.dack_tag,
        mem_mem_resp_data       => cpu_mem_resp.data,
        mem_mem_resp_rack_tag   => cpu_mem_resp.rack_tag,

        pio1_export             => pio1_export,
        pio2_export             => pio2_export,
        pio3_export             => pio3_export,

        sys_clock_clk           => sys_clock,
        sys_reset_reset_n       => not sys_reset,

        uart_rxd                => prim_uart_rxd,
        uart_txd                => prim_uart_txd,
        uart_cts_n              => prim_uart_cts_n,
        uart_rts_n              => prim_uart_rts_n
    );

    UART_TXD      <= prim_uart_txd and io_uart_txd;
    prim_uart_rxd <= UART_RXD;
    io_uart_rxd   <= UART_RXD;
    
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
    

    i_pwm0: entity work.sigma_delta_dac --delta_sigma_2to5
    generic map (
        g_divider => 10,
        g_left_shift => 0,
        g_width => audio_speaker'length )
    port map (
        clock   => sys_clock,
        reset   => sys_reset,
        
        dac_in  => audio_speaker,
    
        dac_out => SPEAKER_DATA );

    audio_speaker(15 downto 8) <= signed(audio_out_data(15 downto 8));
    audio_speaker( 7 downto 0) <= signed(audio_out_data(23 downto 16));

    process(jtag_write_vector, pio3_export, sys_count, pll_locked, por_n)
    begin
        case jtag_write_vector(6 downto 5) is
        when "00" =>
            LED_MOTORn <= sys_count(sys_count'high);
            LED_DISKn  <= pll_locked; -- if pll_locked = 0, led is on
            LED_CARTn  <= por_n; -- if por_n is 0, led is on
            LED_SDACTn <= '0';
        when "01" =>
            LED_MOTORn <= not jtag_write_vector(0);
            LED_DISKn  <= not jtag_write_vector(1);
            LED_CARTn  <= not jtag_write_vector(2);
            LED_SDACTn <= not jtag_write_vector(3);
        when "10" =>
            LED_MOTORn <= not pio3_export(0);
            LED_DISKn  <= not pio3_export(1);
            LED_CARTn  <= not pio3_export(2);
            LED_SDACTn <= not pio3_export(3);
        when others =>
            LED_MOTORn <= '1';
            LED_DISKn  <= '1';
            LED_CARTn  <= '1';
            LED_SDACTn <= '1';
        end case;                            
    end process;

    ULPI_RESET <= por_n;

    b_audio: block
        signal stream_out_data  : std_logic_vector(23 downto 0);
        signal stream_out_tag   : std_logic_vector(0 downto 0);
        signal stream_out_valid : std_logic;
        signal stream_in_data   : std_logic_vector(23 downto 0);
        signal stream_in_tag    : std_logic_vector(0 downto 0);
        signal stream_in_ready  : std_logic;
        signal audio_out_full   : std_logic;
    begin
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
            rx_data(24)  => audio_in_data(0),
            rx_data(23 downto 16) => audio_in_data(15 downto 8),
            rx_data(15 downto 8) => audio_in_data(23 downto 16),
            rx_data(7 downto 0) => audio_in_data(31 downto 24)
        );

        i2s: entity work.i2s_serializer_old
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

    pio1_export(31 downto 0)  <= slot_test_vector(31 downto 0);
    pio2_export(15 downto 0)  <= slot_test_vector(47 downto 32);
    pio2_export(18 downto 16) <= not BUTTON;

    slot_test_vector <=  CAS_MOTOR   & 
                         CAS_SENSE   & 
                         CAS_READ    &
                         CAS_WRITE   &
                         IEC_ATN     &
                         IEC_DATA    &
                         IEC_CLOCK   &
                         IEC_RESET   &
                         IEC_SRQ_IN  &
                         SLOT_PHI2   &
                         SLOT_DOTCLK &
                         SLOT_RSTn   &
                         SLOT_RWn    &
                         SLOT_BA     &
                         SLOT_DMAn   &
                         SLOT_EXROMn &
                         SLOT_GAMEn  &
                         SLOT_ROMHn  &
                         SLOT_ROMLn  &
                         SLOT_IO1n   &
                         SLOT_IO2n   &
                         SLOT_IRQn   &
                         SLOT_NMIn   &
                         SLOT_VCC    &
                         SLOT_DATA   &
                         SLOT_ADDR;

    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            if sys_reset = '1' then
                test_shift <= (0 => '1', 1 => '1', 2 => '1', others => '0');
            else
                test_shift <= test_shift(test_shift'high-1 downto 0) & test_shift(test_shift'high);
            end if;    
        end if;
    end process;

    CAS_MOTOR   <= test_shift(47);
    CAS_SENSE   <= test_shift(46);
    CAS_READ    <= test_shift(45);
    CAS_WRITE   <= test_shift(44);
    IEC_ATN     <= test_shift(43);
    IEC_DATA    <= test_shift(42);
    IEC_CLOCK   <= test_shift(41); 
    IEC_RESET   <= test_shift(40);
    IEC_SRQ_IN  <= test_shift(39);
--    SLOT_PHI2   <= test_shift(38);
--    SLOT_DOTCLK <= test_shift(37);
    SLOT_RSTn   <= test_shift(36);
    SLOT_RWn    <= test_shift(35);
--    SLOT_BA     <= test_shift(34);
    SLOT_DMAn   <= test_shift(33);
    SLOT_EXROMn <= test_shift(32);
    SLOT_GAMEn  <= test_shift(31);
    SLOT_ROMHn  <= test_shift(30);
    SLOT_ROMLn  <= test_shift(29);
    SLOT_IO1n   <= test_shift(28);
    SLOT_IO2n   <= test_shift(27);
    SLOT_IRQn   <= test_shift(26);
    SLOT_NMIn   <= test_shift(25);
--    SLOT_VCC    <= test_shift(24);
    SLOT_DATA   <= test_shift(23 downto 16);
    SLOT_ADDR   <= test_shift(15 downto 0);

end architecture;
