-------------------------------------------------------------------------------
-- Title      : u2p_cia_test
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Toplevel for u2p_cia_test.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    
entity u2p_cia_test is
port (
    -- slot side
    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_BA          : in    std_logic;
    SLOT_RSTn        : inout std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : inout std_logic;
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
    IEC_ATN     : in    std_logic;
    IEC_DATA    : in    std_logic;
    IEC_CLOCK   : in    std_logic;
    IEC_RESET   : in    std_logic;
    IEC_SRQ_IN  : in    std_logic;
    
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
    CAS_SENSE   : in    std_logic := 'Z';
    CAS_READ    : in    std_logic := 'Z';
    CAS_WRITE   : in    std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;

architecture rtl of u2p_cia_test is
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
    signal io_req_cia       : t_io_req;
    signal io_resp_cia      : t_io_resp;
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

    signal rising, falling  : std_logic;
    signal cia_write   : std_logic;
    signal cia_read    : std_logic;

    signal cia_addr    : std_logic_vector(3 downto 0);
    signal cia_cs_n    : std_logic;
    signal cia_cs2     : std_logic;
    signal cia_reset_n : std_logic;
    signal cia_rw_n    : std_logic;
    signal cia_phi2    : std_logic;
    signal cia_irq_n   : std_logic;
    signal db_to_cia   : std_logic_vector(7 downto 0);
    signal db_from_cia : std_logic_vector(7 downto 0);
    signal db_drive    : std_logic;
    signal pa_to_cia   : std_logic_vector(7 downto 0);
    signal pa_from_cia : std_logic_vector(7 downto 0);
    signal pa_drive    : std_logic_vector(7 downto 0);
    signal pb_to_cia   : std_logic_vector(7 downto 0);
    signal pb_from_cia : std_logic_vector(7 downto 0);
    signal pb_drive    : std_logic_vector(7 downto 0);
    signal hs_to_cia   : std_logic_vector(4 downto 0);
    signal hs_from_cia : std_logic_vector(4 downto 0);
    signal hs_drive    : std_logic_vector(4 downto 0);

    signal pa_from_my_cia   : std_logic_vector(7 downto 0);
    signal pb_from_my_cia   : std_logic_vector(7 downto 0);
    signal db_from_my_cia   : std_logic_vector(7 downto 0);
    signal hs_from_my_cia   : std_logic_vector(4 downto 0);
    signal irq_from_my_cia  : std_logic;
    
    signal cia2_sp_o        : std_logic; 
    signal cia2_sp_i        : std_logic; 
    signal cia2_sp_t        : std_logic; 
    signal cia2_cnt_o       : std_logic; 
    signal cia2_cnt_i       : std_logic; 
    signal cia2_cnt_t       : std_logic; 
    signal cia2_pc_o        : std_logic;
    signal cia2_port_a_o    : std_logic_vector(7 downto 0);
    signal cia2_port_a_t    : std_logic_vector(7 downto 0);
    signal cia2_port_b_o    : std_logic_vector(7 downto 0);
    signal cia2_port_b_t    : std_logic_vector(7 downto 0);

    signal spi_c, spi_d : std_logic_vector(0 to 3);

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
        g_ports    => 4
    )
    port map (
        clock      => sys_clock,
        req        => io_u2p_req,
        resp       => io_u2p_resp,
        reqs(0)    => io_req_new_io,
        reqs(1)    => io_req_ddr2,
        reqs(2)    => io_req_remote,
        reqs(3)    => io_req_cia,
        resps(0)   => io_resp_new_io,
        resps(1)   => io_resp_ddr2,
        resps(2)   => io_resp_remote,
        resps(3)   => io_resp_cia
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

    i_cia_test: entity work.u2p_cia_io
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        io_req      => io_req_cia,
        io_resp     => io_resp_cia,
        addr        => cia_addr,
        cs_n        => cia_cs_n,
        cs2         => cia_cs2,
        reset_n     => cia_reset_n,
        rw_n        => cia_rw_n,
        phi2        => cia_phi2,
        irq_n       => cia_irq_n,
        rising      => rising,
        falling     => falling,
        db_to_cia   => db_to_cia,
        db_from_cia => db_from_cia,
        db_drive    => db_drive,
        pa_to_cia   => pa_to_cia,
        pa_from_cia => pa_from_cia,
        pa_drive    => pa_drive,
        pb_to_cia   => pb_to_cia,
        pb_from_cia => pb_from_cia,
        pb_drive    => pb_drive,
        hs_to_cia   => hs_to_cia,
        hs_from_cia => hs_from_cia,
        hs_drive    => hs_drive,

        pb_from_my_cia => pb_from_my_cia,
        db_from_my_cia => db_from_my_cia,
        hs_from_my_cia => hs_from_my_cia,
        irq_from_my_cia => irq_from_my_cia
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

    pio1_export(31 downto 0)  <= slot_test_vector(31 downto 0);
    pio2_export(15 downto 0)  <= slot_test_vector(47 downto 32);
    pio2_export(18 downto 16) <= not BUTTON;
    pio2_export(19) <= '1' when spi_d = "0000" else '0';
    
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            spi_c <= CAS_MOTOR & CAS_SENSE & CAS_READ & CAS_WRITE;
            spi_d <= spi_c;
        end if;
    end process;

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

    SLOT_ADDR(5 downto 2) <= cia_addr;    
    SLOT_RSTn             <= cia_cs_n;    
    -- cia_cs2     
    SLOT_ADDR(6)          <= cia_reset_n; 
    SLOT_ROMHn            <= cia_rw_n;    
    SLOT_ADDR(15)         <= cia_phi2;    

    cia_irq_n             <= SLOT_PHI2;
       
    SLOT_ADDR(14 downto 7) <= db_to_cia when db_drive = '1' else "ZZZZZZZZ";   
    db_from_cia            <= SLOT_ADDR(14 downto 7); 

    r_pa: for i in 0 to 7 generate
        SLOT_DATA(i)           <= pa_to_cia(i) when pa_drive(i) = '1' else 'Z';
        pa_from_cia(i)         <= SLOT_DATA(i);    
    end generate;
    
    SLOT_DMAn           <= pb_to_cia(0) when pb_drive(0) = '1' else 'Z';
    -- SLOT_BA             <= pb_to_cia(1) when pb_drive(1) = '1' else 'Z';
    SLOT_ROMLn          <= pb_to_cia(2) when pb_drive(2) = '1' else 'Z';
    SLOT_IO2n           <= pb_to_cia(3) when pb_drive(3) = '1' else 'Z';
    SLOT_EXROMn         <= pb_to_cia(4) when pb_drive(4) = '1' else 'Z';
    SLOT_GAMEn          <= pb_to_cia(5) when pb_drive(5) = '1' else 'Z';
    SLOT_IO1n           <= pb_to_cia(6) when pb_drive(6) = '1' else 'Z';
    -- SLOT_DOTCLK         <= pb_to_cia(7) when pb_drive(7) = '1' else 'Z';
    
    pb_from_cia(0) <= SLOT_DMAn;
    pb_from_cia(1) <= SLOT_BA;
    pb_from_cia(2) <= SLOT_ROMLn;
    pb_from_cia(3) <= SLOT_IO2n;
    pb_from_cia(4) <= SLOT_EXROMn;
    pb_from_cia(5) <= SLOT_GAMEn;
    pb_from_cia(6) <= SLOT_IO1n;
    pb_from_cia(7) <= SLOT_DOTCLK;
   
    SLOT_RWn       <= hs_to_cia(0) when hs_drive(0) = '1' else 'Z'; -- pin 18 (PC#)
    SLOT_IRQn      <= hs_to_cia(1) when hs_drive(1) = '1' else 'Z'; -- pin 19 (TOD)
    SLOT_ADDR(0)   <= hs_to_cia(2) when hs_drive(2) = '1' else 'Z'; -- pin 40 (CNT)
    SLOT_ADDR(1)   <= hs_to_cia(3) when hs_drive(3) = '1' else 'Z'; -- pin 39 (SP)
    SLOT_NMIn      <= hs_to_cia(4) when hs_drive(4) = '1' else 'Z'; -- pin 24 (FLAG#)

    -- for 6526, hs_drive should be 10010

    hs_from_cia(0) <= SLOT_RWn;
    hs_from_cia(1) <= SLOT_IRQn;
    hs_from_cia(2) <= SLOT_ADDR(0);
    hs_from_cia(3) <= SLOT_ADDR(1);
    hs_from_cia(4) <= SLOT_NMIn;


-- Wire mapping:
-- 21 SLOT_DATA[0]  CIA  2 PA[0]
-- 20 SLOT_DATA[1]  CIA  3 PA[1]
-- 19 SLOT_DATA[2]  CIA  4 PA[2]
-- 18 SLOT_DATA[3]  CIA  5 PA[3]
-- 17 SLOT_DATA[4]  CIA  6 PA[4]
-- 16 SLOT_DATA[5]  CIA  7 PA[5]
-- 15 SLOT_DATA[6]  CIA  8 PA[6]
-- 14 SLOT_DATA[7]  CIA  9 PA[7]
-- 13 SLOT_DMA      CIA 10 PB[0]
-- 12 SLOT_BA       CIA 11 PB[1]
-- 11 SLOT_ROMLn    CIA 12 PB[2]
-- 10 SLOT_IO2n     CIA 13 PB[3]
--  9 SLOT_EXROMn   CIA 14 PB[4]
--  8 SLOT_GAMEn    CIA 15 PB[5]
--  7 SLOT_IO1n     CIA 16 PB[6]
--  6 SLOT_DOTCLK   CIA 17 PB[7]
--  5 SLOT_RWn      CIA 18 PC#
--  4 SLOT_IRQn     CIA 19 TOD
                    
--  E SLOT_PHI2     CIA 21 IRQ#
--  B SLOT_ROMHn    CIA 22 R/W#
--  C SLOT_RSTn     CIA 23 CS#
--  D SLOT_NMIn     CIA 24 FLAG#
--  F SLOT_ADDR[15] CIA 25 PHI2
--  H SLOT_ADDR[14] CIA 26 DB[7]
--  J SLOT_ADDR[13] CIA 27 DB[6]
--  K SLOT_ADDR[12] CIA 28 DB[5]
--  L SLOT_ADDR[11] CIA 29 DB[4]
--  M SLOT_ADDR[10] CIA 30 DB[3]
--  N SLOT_ADDR[9]  CIA 31 DB[2]
--  P SLOT_ADDR[8]  CIA 32 DB[1]
--  R SLOT_ADDR[7]  CIA 33 DB[0]
--  S SLOT_ADDR[6]  CIA 34 RESET#
--  T SLOT_ADDR[5]  CIA 35 RS[3]
--  U SLOT_ADDR[4]  CIA 36 RS[2]
--  V SLOT_ADDR[3]  CIA 37 RS[1]
--  W SLOT_ADDR[2]  CIA 38 RS[0]
--  X SLOT_ADDR[1]  CIA 39 SP
--  Y SLOT_ADDR[0]  CIA 40 CNT
-- 
    cia_write <= not cia_cs_n and not cia_rw_n;
    cia_read  <= not cia_cs_n and     cia_rw_n;

    i_my_cia: entity work.cia_registers
        generic map (
            g_unit_name => "CIA_2" )
        port map (
            clock       => sys_clock,
            rising      => rising,
            falling     => falling,
            reset       => not cia_reset_n,
            
            addr        => unsigned(cia_addr),
            wen         => cia_write,
            ren         => cia_read,
            data_out    => db_from_my_cia,
            data_in     => db_to_cia,
        
            -- pio --
            port_a_o    => cia2_port_a_o,
            port_a_t    => cia2_port_a_t,
            port_a_i    => pa_from_my_cia,
            
            port_b_o    => cia2_port_b_o,
            port_b_t    => cia2_port_b_t,
            port_b_i    => pb_from_my_cia,
        
            -- serial pin
            sp_o        => cia2_sp_o,
            sp_i        => cia2_sp_i,
            sp_t        => cia2_sp_t,
            
            cnt_i       => cia2_cnt_i,
            cnt_o       => cia2_cnt_o,
            cnt_t       => cia2_cnt_t,
            
            tod_pin     => SLOT_IRQn,
            pc_o        => cia2_pc_o,
            flag_i      => SLOT_NMIn,
            
            irq         => irq_from_my_cia );

    
    hs_from_my_cia(0) <= cia2_pc_o;
    hs_from_my_cia(1) <= SLOT_IRQn;
    hs_from_my_cia(2) <= cia2_cnt_i;
    hs_from_my_cia(3) <= cia2_sp_i; 
    hs_from_my_cia(4) <= SLOT_NMIn;

    pa_from_my_cia <= cia2_port_a_o or not cia2_port_a_t;
    pb_from_my_cia <= cia2_port_b_o or not cia2_port_b_t;

    cia2_cnt_i <= (cia2_cnt_o or not cia2_cnt_t) and (hs_to_cia(2) or not hs_drive(2));
    cia2_sp_i  <= (cia2_sp_o  or not cia2_sp_t)  and (hs_to_cia(3) or not hs_drive(3));

end architecture;
