-------------------------------------------------------------------------------
-- Title      : u2p_nios
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Toplevel based on the "solo" nios; without Altera DDR2 ctrl.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    use work.my_math_pkg.all;
    use work.audio_type_pkg.all;
        
entity u2p_nios_solo is
port (
    -- slot side
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_RSTn        : inout std_logic;
    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_ADDR        : inout unsigned(15 downto 0);
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
    SLOT_DRV_RST     : out   std_logic := '0';
    
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

architecture rtl of u2p_nios_solo is
    component nios_solo is
        port (
            clk_clk            : in  std_logic                     := 'X';             -- clk
            io_ack             : in  std_logic                     := 'X';             -- ack
            io_rdata           : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
            io_read            : out std_logic;                                        -- read
            io_wdata           : out std_logic_vector(7 downto 0);                     -- wdata
            io_write           : out std_logic;                                        -- write
            io_address         : out std_logic_vector(19 downto 0);                    -- address
            io_irq             : in  std_logic                     := 'X';             -- irq
            io_u2p_ack         : in  std_logic                     := 'X';             -- ack
            io_u2p_rdata       : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
            io_u2p_read        : out std_logic;                                        -- read
            io_u2p_wdata       : out std_logic_vector(7 downto 0);                     -- wdata
            io_u2p_write       : out std_logic;                                        -- write
            io_u2p_address     : out std_logic_vector(19 downto 0);                    -- address
            io_u2p_irq         : in  std_logic                     := 'X';             -- irq
            mem_mem_req_address     : out std_logic_vector(25 downto 0);                    -- mem_req_address
            mem_mem_req_byte_en     : out std_logic_vector(3 downto 0);                     -- mem_req_byte_en
            mem_mem_req_read_writen : out std_logic;                                        -- mem_req_read_writen
            mem_mem_req_request     : out std_logic;                                        -- mem_req_request
            mem_mem_req_tag         : out std_logic_vector(7 downto 0);                     -- mem_req_tag
            mem_mem_req_wdata       : out std_logic_vector(31 downto 0);                    -- mem_req_wdata
            mem_mem_resp_dack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_dack_tag
            mem_mem_resp_data       : in  std_logic_vector(31 downto 0) := (others => 'X'); -- mem_resp_data
            mem_mem_resp_rack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_rack_tag
            reset_reset_n      : in  std_logic                     := 'X'              -- reset_n
        );
    end component nios_solo;

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
    signal por_count    : unsigned(15 downto 0) := (others => '0');
    signal led_n        : std_logic_vector(0 to 3);
    signal RSTn_out     : std_logic;
    signal irq_oc, nmi_oc, rst_oc, dma_oc, exrom_oc, game_oc    : std_logic;
    signal slot_addr_o  : unsigned(15 downto 0);
    signal slot_addr_tl : std_logic;
    signal slot_addr_th : std_logic;
    signal slot_data_o  : std_logic_vector(7 downto 0);
    signal slot_data_t  : std_logic;
    signal slot_rwn_o   : std_logic;
    
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal eth_reset    : std_logic;
    signal ulpi_reset_req : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);
    signal buffer_en    : std_logic;
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    
    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic;
    signal is_idle          : std_logic;
    signal cpu_mem_req      : t_mem_req_32;
    signal cpu_mem_resp     : t_mem_resp_32;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;

    signal uart_txd_from_logic  : std_logic;
    signal i2c_sda_i   : std_logic;
    signal i2c_sda_o   : std_logic;
    signal i2c_scl_i   : std_logic;
    signal i2c_scl_o   : std_logic;
    signal mdio_o      : std_logic;

    signal sw_trigger     : std_logic;
    signal trigger     : std_logic;
        
    -- IEC open drain
    signal iec_atn_o   : std_logic;
    signal iec_data_o  : std_logic;
    signal iec_clock_o : std_logic;
    signal iec_srq_o   : std_logic;
    signal sw_iec_o    : std_logic_vector(3 downto 0);
    signal sw_iec_i    : std_logic_vector(3 downto 0);
    
    -- Cassette
    signal c2n_read_in      : std_logic;
    signal c2n_write_in     : std_logic;
    signal c2n_read_out     : std_logic;
    signal c2n_write_out    : std_logic;
    signal c2n_read_en      : std_logic;
    signal c2n_write_en     : std_logic;
    signal c2n_sense_in     : std_logic;
    signal c2n_sense_out    : std_logic;
    signal c2n_motor_in     : std_logic;
    signal c2n_motor_out    : std_logic;

    -- io buses
    signal io_irq       : std_logic;
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    signal io_u2p_req   : t_io_req;
    signal io_u2p_resp  : t_io_resp;
    signal io_u2p_req_small : t_io_req;
    signal io_u2p_resp_small: t_io_resp;
    signal io_u2p_req_big   : t_io_req;
    signal io_u2p_resp_big  : t_io_resp;
    signal io_req_new_io    : t_io_req;
    signal io_resp_new_io   : t_io_resp;
    signal io_req_remote    : t_io_req;
    signal io_resp_remote   : t_io_resp;
    signal io_req_ddr2      : t_io_req;
    signal io_resp_ddr2     : t_io_resp;

    signal io_req_mixer     : t_io_req;
    signal io_resp_mixer    : t_io_resp;
    signal io_req_debug     : t_io_req;
    signal io_resp_debug    : t_io_resp;

    -- Parallel cable connection
    signal drv_track_is_0       : std_logic;
    signal drv_via1_port_a_o    : std_logic_vector(7 downto 0);
    signal drv_via1_port_a_i    : std_logic_vector(7 downto 0);
    signal drv_via1_port_a_t    : std_logic_vector(7 downto 0);
    signal drv_via1_ca2_o       : std_logic;
    signal drv_via1_ca2_i       : std_logic;
    signal drv_via1_ca2_t       : std_logic;
    signal drv_via1_cb1_o       : std_logic;
    signal drv_via1_cb1_i       : std_logic;
    signal drv_via1_cb1_t       : std_logic;
    
    -- audio
    signal audio_speaker    : signed(12 downto 0);
    signal speaker_vol      : std_logic_vector(3 downto 0);

    signal ult_drive1       : signed(17 downto 0);
    signal ult_drive2       : signed(17 downto 0);
    signal ult_tape_r       : signed(17 downto 0);
    signal ult_tape_w       : signed(17 downto 0);
    signal ult_samp_l       : signed(17 downto 0);
    signal ult_samp_r       : signed(17 downto 0);
    signal ult_sid_1        : signed(17 downto 0);
    signal ult_sid_2        : signed(17 downto 0);

    signal c64_debug_select : std_logic_vector(2 downto 0);
    signal c64_debug_data   : std_logic_vector(31 downto 0);
    signal c64_debug_valid  : std_logic;
    signal drv_debug_data   : std_logic_vector(31 downto 0);
    signal drv_debug_valid  : std_logic;
    
    signal eth_tx_data   : std_logic_vector(7 downto 0);
    signal eth_tx_last   : std_logic;
    signal eth_tx_valid  : std_logic;
    signal eth_tx_ready  : std_logic := '1';

    signal eth_u2p_data  : std_logic_vector(7 downto 0);
    signal eth_u2p_last  : std_logic;
    signal eth_u2p_valid : std_logic;
    signal eth_u2p_ready : std_logic := '1';

    signal eth_rx_data   : std_logic_vector(7 downto 0);
    signal eth_rx_sof    : std_logic;
    signal eth_rx_eof    : std_logic;
    signal eth_rx_valid  : std_logic;
begin
    process(RMII_REFCLK)
    begin
        if rising_edge(RMII_REFCLK) then
            if por_count = X"FFFF" then
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

    i_nios: nios_solo
    port map (
        clk_clk            => sys_clock,
        reset_reset_n      => not sys_reset,

        io_ack             => io_resp.ack,
        io_rdata           => io_resp.data,
        io_read            => io_req.read,
        io_wdata           => io_req.data,
        io_write           => io_req.write,
        unsigned(io_address) => io_req.address,
        io_irq             => io_irq,

        io_u2p_ack         => io_u2p_resp.ack,
        io_u2p_rdata       => io_u2p_resp.data,
        io_u2p_read        => io_u2p_req.read,
        io_u2p_wdata       => io_u2p_req.data,
        io_u2p_write       => io_u2p_req.write,
        unsigned(io_u2p_address) => io_u2p_req.address,
        io_u2p_irq         => '0',
        
        unsigned(mem_mem_req_address) => cpu_mem_req.address,
        mem_mem_req_byte_en     => cpu_mem_req.byte_en,
        mem_mem_req_read_writen => cpu_mem_req.read_writen,
        mem_mem_req_request     => cpu_mem_req.request,
        mem_mem_req_tag         => cpu_mem_req.tag,
        mem_mem_req_wdata       => cpu_mem_req.data,
        mem_mem_resp_dack_tag   => cpu_mem_resp.dack_tag,
        mem_mem_resp_data       => cpu_mem_resp.data,
        mem_mem_resp_rack_tag   => cpu_mem_resp.rack_tag
    );

    i_split_u2p: entity work.io_bus_splitter
    generic map (
        g_range_lo => 16,
        g_range_hi => 16,
        g_ports    => 2
    )
    port map (
        clock      => sys_clock,
        req        => io_u2p_req,
        resp       => io_u2p_resp,
        reqs(0)    => io_u2p_req_small,
        reqs(1)    => io_u2p_req_big,
        resps(0)   => io_u2p_resp_small,
        resps(1)   => io_u2p_resp_big
    );

    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 8,
        g_range_hi => 9,
        g_ports    => 3
    )
    port map (
        clock      => sys_clock,
        req        => io_u2p_req_small,
        resp       => io_u2p_resp_small,
        reqs(0)    => io_req_new_io,
        reqs(1)    => io_req_ddr2,
        reqs(2)    => io_req_remote,
        resps(0)   => io_resp_new_io,
        resps(1)   => io_resp_ddr2,
        resps(2)   => io_resp_remote
    );

    i_split2: entity work.io_bus_splitter
    generic map (
        g_range_lo => 12,
        g_range_hi => 12,
        g_ports    => 2
    )
    port map (
        clock      => sys_clock,
        req        => io_u2p_req_big,
        resp       => io_u2p_resp_big,
        reqs(0)    => io_req_mixer,
        reqs(1)    => io_req_debug,
        resps(0)   => io_resp_mixer,
        resps(1)   => io_resp_debug
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
        inhibit           => memctrl_inhibit,
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
        iec_i      => sw_iec_i,
        iec_o      => sw_iec_o,
        board_rev  => not BOARD_REVn,
        eth_irq_i  => ETH_IRQn,
        speaker_en => SPEAKER_ENABLE,
	    speaker_vol=> speaker_vol,
        hub_reset_n=> HUB_RESETn,
        ulpi_reset => ulpi_reset_req,
        buffer_en  => buffer_en
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
        g_version       => X"1A",
        g_simulation    => false,
        g_ultimate2plus => true,
        g_clock_freq    => 62_500_000,
        g_numerator     => 32,
        g_denominator   => 125,
        g_baud_rate     => 115_200,
        g_timer_rate    => 200_000,
        g_microblaze    => false,
        g_big_endian    => false,
        g_icap          => false,
        g_uart          => true,
        g_drive_1541    => true,
        g_drive_1541_2  => true,
        g_hardware_gcr  => true,
        g_ram_expansion => true,
        g_extended_reu  => false,
        g_stereo_sid    => true,
        g_8voices       => true,
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
        g_sampler       => true,
        g_acia          => true,
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
        
        -- slot side
        BUFFER_ENn  => open,
        VCC         => SLOT_VCC,

        phi2_i      => SLOT_PHI2,
        dotclk_i    => SLOT_DOTCLK,
        rstn_i      => SLOT_RSTn,
        rstn_o      => RSTn_out,
                                   
        slot_addr_o => slot_addr_o,
        slot_addr_i => SLOT_ADDR,
        slot_addr_tl=> slot_addr_tl,
        slot_addr_th=> slot_addr_th,
        slot_data_o => slot_data_o,
        slot_data_i => SLOT_DATA,
        slot_data_t => slot_data_t,
        rwn_i       => SLOT_RWn,
        rwn_o       => slot_rwn_o,
        exromn_i    => SLOT_EXROMn,
        exromn_o    => exrom_oc,
        gamen_i     => SLOT_GAMEn,
        gamen_o     => game_oc,
        irqn_i      => SLOT_IRQn,
        irqn_o      => irq_oc,
        nmin_i      => SLOT_NMIn,
        nmin_o      => nmi_oc,
        ba_i        => SLOT_BA,
        dman_o      => dma_oc,
        romhn_i     => SLOT_ROMHn,
        romln_i     => SLOT_ROMLn,
        io1n_i      => SLOT_IO1n,
        io2n_i      => SLOT_IO2n,
                
        -- local bus side
        mem_inhibit => memctrl_inhibit,
        mem_req     => mem_req,
        mem_resp    => mem_resp,
                 
        -- Audio outputs
        audio_speaker   => audio_speaker,
        speaker_vol     => speaker_vol,

        aud_drive1      => ult_drive1, 
        aud_drive2      => ult_drive2, 
        aud_tape_r      => ult_tape_r, 
        aud_tape_w      => ult_tape_w, 
        aud_samp_l      => ult_samp_l, 
        aud_samp_r      => ult_samp_r, 
        aud_sid_1       => ult_sid_1,
        aud_sid_2       => ult_sid_2,
        
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
                                    
        MOTOR_LEDn  => led_n(0),
        DISK_ACTn   => led_n(1),
        CART_LEDn   => led_n(2),
        SDACT_LEDn  => led_n(3),

        -- Parallel cable pins
        drv_track_is_0      => drv_track_is_0,
        drv_via1_port_a_o   => drv_via1_port_a_o,
        drv_via1_port_a_i   => drv_via1_port_a_i,
        drv_via1_port_a_t   => drv_via1_port_a_t,
        drv_via1_ca2_o      => drv_via1_ca2_o,
        drv_via1_ca2_i      => drv_via1_ca2_i,
        drv_via1_ca2_t      => drv_via1_ca2_t,
        drv_via1_cb1_o      => drv_via1_cb1_o,
        drv_via1_cb1_i      => drv_via1_cb1_i,
        drv_via1_cb1_t      => drv_via1_cb1_t,

        -- Debug UART
        UART_TXD    => uart_txd_from_logic,
        UART_RXD    => UART_RXD,
        
        -- Debug buses
        drv_debug_data   => drv_debug_data,
        drv_debug_valid  => drv_debug_valid,
        c64_debug_data   => c64_debug_data,
        c64_debug_valid  => c64_debug_valid,
        c64_debug_select => c64_debug_select,                    
        
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
        c2n_read_in    => c2n_read_in, 
        c2n_write_in   => c2n_write_in, 
        c2n_read_out   => c2n_read_out, 
        c2n_write_out  => c2n_write_out, 
        c2n_read_en    => c2n_read_en, 
        c2n_write_en   => c2n_write_en, 
        c2n_sense_in   => c2n_sense_in, 
        c2n_sense_out  => c2n_sense_out, 
        c2n_motor_in   => c2n_motor_in, 
        c2n_motor_out  => c2n_motor_out, 
        
        -- Ethernet Interface (RMII)
        eth_clock    => RMII_REFCLK, 
        eth_reset    => eth_reset,
        eth_rx_data  => eth_rx_data,
        eth_rx_sof   => eth_rx_sof,
        eth_rx_eof   => eth_rx_eof,
        eth_rx_valid => eth_rx_valid,
        eth_tx_data  => eth_u2p_data,
        eth_tx_eof   => eth_u2p_last,
        eth_tx_valid => eth_u2p_valid,
        eth_tx_ready => eth_u2p_ready,

        -- Buttons
        sw_trigger  => sw_trigger,
        trigger     => sw_trigger,
        BUTTON      => button_i );

    -- Parallel cable not implemented. This is the way to stub it...
    drv_via1_port_a_i(7 downto 1) <= drv_via1_port_a_o(7 downto 1) or not drv_via1_port_a_t(7 downto 1);
    drv_via1_port_a_i(0)          <= drv_track_is_0; -- for 1541C
    drv_via1_ca2_i    <= drv_via1_ca2_o    or not drv_via1_ca2_t;
    drv_via1_cb1_i    <= drv_via1_cb1_o    or not drv_via1_cb1_t;


    process(sys_clock)
        variable c, d  : std_logic := '0';
    begin
        if rising_edge(sys_clock) then
            trigger <= d;
            d := c;
            c := button_i(0);
        end if;
    end process;
    
    SLOT_RSTn <= '0' when RSTn_out = '0' else 'Z';
    SLOT_DRV_RST <= not RSTn_out when rising_edge(sys_clock); -- Drive this pin HIGH when we want to reset the C64 (uses NFET on Rev.E boards)
    
    SLOT_ADDR(15 downto 12) <= slot_addr_o(15 downto 12) when slot_addr_th = '1' else (others => 'Z');
    SLOT_ADDR(11 downto 00) <= slot_addr_o(11 downto 00) when slot_addr_tl = '1' else (others => 'Z');
    SLOT_DATA <= slot_data_o when slot_data_t = '1' else (others => 'Z');
    SLOT_RWn  <= slot_rwn_o  when slot_addr_tl = '1' else 'Z';

    irq_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => irq_oc, oc_out => SLOT_IRQn);
    nmi_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => nmi_oc, oc_out => SLOT_NMIn);
    dma_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => dma_oc, oc_out => SLOT_DMAn);
    exr_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => exrom_oc, oc_out => SLOT_EXROMn);
    gam_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => game_oc, oc_out => SLOT_GAMEn);
    
    LED_MOTORn <= led_n(0) xor sys_reset;
    LED_DISKn  <= led_n(1) xor sys_reset;
    LED_CARTn  <= led_n(2) xor sys_reset;
    LED_SDACTn <= led_n(3) xor sys_reset;

    IEC_SRQ_IN <= '0' when iec_srq_o   = '0' or sw_iec_o(3) = '0' else 'Z';
    IEC_ATN    <= '0' when iec_atn_o   = '0' or sw_iec_o(2) = '0' else 'Z';
    IEC_DATA   <= '0' when iec_data_o  = '0' or sw_iec_o(1) = '0' else 'Z';
    IEC_CLOCK  <= '0' when iec_clock_o = '0' or sw_iec_o(0) = '0' else 'Z';

    sw_iec_i <= IEC_SRQ_IN & IEC_ATN & IEC_DATA & IEC_CLOCK;

    button_i <= not BUTTON;

    ULPI_RESET <= por_n;
    UART_TXD <= uart_txd_from_logic; -- and uart_txd_from_qsys;

    -- Tape
    c2n_motor_in <= CAS_MOTOR;
    CAS_SENSE    <= '0' when c2n_sense_out = '1' else 'Z';
    c2n_sense_in <= not CAS_SENSE;
    CAS_READ     <= c2n_read_out when c2n_read_en = '1' else 'Z';
    c2n_read_in  <= CAS_READ;
    CAS_WRITE    <= c2n_write_out when c2n_write_en = '1' else 'Z';
    c2n_write_in <= CAS_WRITE;


    i_pwm0: entity work.sigma_delta_dac --delta_sigma_2to5
    generic map (
        g_left_shift => 2,
        g_divider => 10,
        g_width => audio_speaker'length )
    port map (
        clock   => sys_clock,
        reset   => sys_reset,
        
        dac_in  => audio_speaker,
    
        dac_out => SPEAKER_DATA );

    b_audio: block        
        signal aud_drive1       : signed(17 downto 0);
        signal aud_drive2       : signed(17 downto 0);
        signal aud_tape_r       : signed(17 downto 0);
        signal aud_tape_w       : signed(17 downto 0);
        signal aud_samp_l       : signed(17 downto 0);
        signal aud_samp_r       : signed(17 downto 0);
        signal aud_sid_1        : signed(17 downto 0);
        signal aud_sid_2        : signed(17 downto 0);
        signal audio_sid1       : std_logic_vector(17 downto 0);
        signal audio_sid2       : std_logic_vector(17 downto 0);
        signal codec_left_in    : std_logic_vector(23 downto 0);
        signal codec_right_in   : std_logic_vector(23 downto 0);
        signal codec_left_out   : std_logic_vector(23 downto 0);
        signal codec_right_out  : std_logic_vector(23 downto 0);
        signal audio_get_sample : std_logic;
        signal sys_get_sample       : std_logic;
        signal inputs               : t_audio_array(0 to 9);
    begin
        -- the SID sound from the socket comes in from the codec
        i2s: entity work.i2s_serializer
        port map (
            clock            => audio_clock,
            reset            => audio_reset,
            i2s_out          => AUDIO_SDO,
            i2s_in           => AUDIO_SDI,
            i2s_bclk         => AUDIO_BCLK,
            i2s_fs           => AUDIO_LRCLK,
            sample_pulse     => audio_get_sample,
            
            left_sample_out  => codec_left_in,
            right_sample_out => codec_right_in,
            left_sample_in   => codec_left_out,
            right_sample_in  => codec_right_out );

        AUDIO_MCLK <= audio_clock;

        i_sync_get: entity work.pulse_synchronizer
        port map (
            clock_in  => audio_clock,
            pulse_in  => audio_get_sample,
            clock_out => sys_clock,
            pulse_out => sys_get_sample
        );

        i_ultfilt1: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_drive1, audio_clock, aud_drive1 );
        i_ultfilt2: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_drive2, audio_clock, aud_drive2 );
        i_ultfilt3: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_tape_r, audio_clock, aud_tape_r );
        i_ultfilt4: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_tape_w, audio_clock, aud_tape_w );
        i_ultfilt5: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_samp_l, audio_clock, aud_samp_l );
        i_ultfilt6: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_samp_r, audio_clock, aud_samp_r );
        i_ultfilt7: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_sid_1,  audio_clock, aud_sid_1 );
        i_ultfilt8: entity work.sys_to_aud port map(sys_clock, sys_reset, sys_get_sample, ult_sid_2,  audio_clock, aud_sid_2 );
        
        inputs(0) <= aud_sid_1;
        inputs(1) <= aud_sid_2;
        inputs(2) <= signed(codec_left_in(23 downto 6));
        inputs(3) <= signed(codec_right_in(23 downto 6));
        inputs(4) <= aud_samp_l;
        inputs(5) <= aud_samp_r;
        inputs(6) <= aud_drive1;
        inputs(7) <= aud_drive2;
        inputs(8) <= aud_tape_r;
        inputs(9) <= aud_tape_w;

        -- Now we have ten sources, all in audio domain, let's do some mixing
        i_mixer: entity work.generic_mixer
        generic map(
            g_num_sources => 10
        )
        port map(
            clock         => audio_clock,
            reset         => audio_reset,
            start         => audio_get_sample,
            sys_clock     => sys_clock,
            req           => io_req_mixer,
            resp          => io_resp_mixer,
            inputs        => inputs,
            out_L         => codec_left_out,
            out_R         => codec_right_out
        );

    end block;
    
    SLOT_BUFFER_ENn <= not buffer_en;

    i_debug_eth: entity work.eth_debug_stream
    port map (
        eth_clock     => RMII_REFCLK,
        eth_reset     => eth_reset,

        eth_u2p_data  => eth_u2p_data,
        eth_u2p_last  => eth_u2p_last,
        eth_u2p_valid => eth_u2p_valid,
        eth_u2p_ready => eth_u2p_ready,

        eth_tx_data   => eth_tx_data,
        eth_tx_last   => eth_tx_last,
        eth_tx_valid  => eth_tx_valid,
        eth_tx_ready  => eth_tx_ready,

        sys_clock     => sys_clock,
        sys_reset     => sys_reset,
        io_req        => io_req_debug,
        io_resp       => io_resp_debug,

        c64_debug_select    => c64_debug_select,
        c64_debug_data      => c64_debug_data,
        c64_debug_valid     => c64_debug_valid,
        drv_debug_data      => drv_debug_data,
        drv_debug_valid     => drv_debug_valid,
    
        IEC_ATN             => IEC_ATN,
        IEC_CLOCK           => IEC_CLOCK,
        IEC_DATA            => IEC_DATA
    );

    -- Transceiver
    i_rmii: entity work.rmii_transceiver
    port map (
        clock           => RMII_REFCLK,
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

end architecture;

