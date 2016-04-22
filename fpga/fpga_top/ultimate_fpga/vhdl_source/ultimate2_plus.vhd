
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity ultimate2_plus is
generic (
    g_version       : unsigned(7 downto 0) := X"04" );
port (
    -- CLOCK       : in    std_logic;
    
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
    SDRAM_BA    : out   std_logic_vector(2 downto 0);
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


architecture structural of ultimate2_plus is

component ddr2
    PORT (
        local_address   : IN STD_LOGIC_VECTOR (24 DOWNTO 0);
        local_write_req : IN STD_LOGIC;
        local_read_req  : IN STD_LOGIC;
        local_burstbegin    : IN STD_LOGIC;
        local_wdata : IN STD_LOGIC_VECTOR (15 DOWNTO 0);
        local_be    : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        local_size  : IN STD_LOGIC_VECTOR (2 DOWNTO 0);
        local_refresh_req   : IN STD_LOGIC;
        local_autopch_req   : IN STD_LOGIC;
        global_reset_n  : IN STD_LOGIC;
        pll_ref_clk : IN STD_LOGIC;
        soft_reset_n    : IN STD_LOGIC;
        local_ready : OUT STD_LOGIC;
        local_rdata : OUT STD_LOGIC_VECTOR (15 DOWNTO 0);
        local_rdata_valid   : OUT STD_LOGIC;
        local_refresh_ack   : OUT STD_LOGIC;
        --local_wdata_req : OUT STD_LOGIC;
        local_init_done : OUT STD_LOGIC;
        reset_phy_clk_n : OUT STD_LOGIC;
        mem_odt : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_cs_n    : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_cke : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_addr    : OUT STD_LOGIC_VECTOR (13 DOWNTO 0);
        mem_ba  : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
        mem_ras_n   : OUT STD_LOGIC;
        mem_cas_n   : OUT STD_LOGIC;
        mem_we_n    : OUT STD_LOGIC;
        mem_dm  : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        phy_clk : OUT STD_LOGIC;
        aux_full_rate_clk   : OUT STD_LOGIC;
        aux_half_rate_clk   : OUT STD_LOGIC;
        reset_request_n : OUT STD_LOGIC;
        mem_clk : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_clk_n   : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_dq  : INOUT STD_LOGIC_VECTOR (7 DOWNTO 0);
        mem_dqs : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0)
    );
end component;

component pll
    PORT
    (
        inclk0      : IN STD_LOGIC  := '0';
        c0      : OUT STD_LOGIC ;
        locked      : OUT STD_LOGIC 
    );
end component;

    signal reset_in     : std_logic;
    signal dcm_lock     : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal sys_clock_2x : std_logic;
--    signal sys_shifted  : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    
    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;

    -- IEC open drain
    signal iec_atn_o   : std_logic;
    signal iec_data_o  : std_logic;
    signal iec_clock_o : std_logic;
    signal iec_srq_o   : std_logic;

    signal por          : std_logic;
    signal por_count    : unsigned(27 downto 0) := (others => '0');

begin
    -- stub
    AUDIO_SDO <= AUDIO_SDI;

    RMII_TX_EN   <= RMII_RX_ER and RMII_REFCLK and RMII_CRS_DV and ETH_IRQn and SLOT_VCC;
    RMII_TX_DATA <= RMII_RX_DATA;

    reset_in <= '1' when BUTTON="000" else '0'; -- all 3 buttons pressed
    button_i <= not BUTTON;

    sys_clock <= RMII_REFCLK;
    sys_clock_2x <= RMII_REFCLK;
    sys_reset <= por; --reset_in;

    process(RMII_REFCLK)
    begin
        if rising_edge(RMII_REFCLK) then
            if por_count = X"FFFFFFF" then
                por <= '0';
            else
                por <= '1';
                por_count <= por_count + 1;
            end if;
        end if;
    end process;


    i_logic: entity work.ultimate_logic_32
    generic map (
        g_version       => g_version,
        g_simulation    => false,
        g_clock_freq    => 50_000_000,
        g_baud_rate     => 115_200,
        g_timer_rate    => 200_000,
        g_icap          => false,
        g_uart          => true,
        g_drive_1541    => true,
        g_drive_1541_2  => true,
        g_hardware_gcr  => true,
        g_ram_expansion => true,
        g_extended_reu  => false,
        g_stereo_sid    => true,
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
        g_analyzer      => false,
        g_profiler      => true )
    port map (
        -- globals
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
    
        ulpi_clock  => ulpi_clock,
        ulpi_reset  => ulpi_reset_i,
    
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
        --memctrl_idle    => memctrl_idle,
        mem_req     => mem_req,
        mem_resp    => mem_resp,
                 
        -- PWM outputs (for audio)
        PWM_OUT(0)  => SPEAKER_DATA,
        PWM_OUT(1)  => SPEAKER_ENABLE,
    
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
        UART_TXD    => UART_TXD,
        UART_RXD    => UART_RXD,
        
        -- SD Card Interface
        SD_SSn      => open,
        SD_CLK      => open,
        SD_MOSI     => open,
        SD_MISO     => '1',
        SD_CARDDETn => '0',
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

    process(ulpi_clock)
    begin
        if rising_edge(ulpi_clock) then
            ulpi_reset_i <= sys_reset;
        end if;
    end process;

    ULPI_RESET <= not por;

    i_ddr2: ddr2
    port map (
        local_address     => std_logic_vector(mem_req.address(24 downto 0)),
        local_write_req   => mem_req.read_writen,
        local_read_req    => mem_req.request,
        local_burstbegin  => mem_req.request,
        local_wdata       => mem_req.data(15 downto 0),
        local_be          => mem_req.byte_en(1 downto 0),
        local_size        => "001",
        local_refresh_req => '1',
        local_autopch_req => '1',
        global_reset_n    => not sys_reset,
        pll_ref_clk       => RMII_REFCLK,
        soft_reset_n      => '1',
        local_ready       => open,
        local_rdata       => mem_resp.data(15 downto 0),
        local_rdata_valid => mem_resp.dack_tag(0),
        local_refresh_ack => mem_resp.rack,
        --local_wdata_req   => open,
        local_init_done   => open,
        reset_phy_clk_n   => open,
        mem_odt(0)        => SDRAM_ODT,
        mem_cs_n(0)       => SDRAM_CSn,
        mem_cke(0)        => SDRAM_CKE,
        mem_addr          => SDRAM_A,
        mem_ba            => SDRAM_BA(1 downto 0),
        mem_ras_n         => SDRAM_RASn,
        mem_cas_n         => SDRAM_CASn,
        mem_we_n          => SDRAM_WEn,
        mem_dm(0)         => SDRAM_DM,
        phy_clk           => open,
        aux_full_rate_clk => open,
        aux_half_rate_clk => open,
        reset_request_n   => open,
        mem_clk(0)        => SDRAM_CLK,
        mem_clk_n(0)      => SDRAM_CLKn,
        mem_dq            => SDRAM_DQ,
        mem_dqs(0)        => SDRAM_DQS );

    SDRAM_BA(2) <= '0';    


    i_pll: pll port map (
        inclk0  => RMII_REFCLK,
        c0      => HUB_CLOCK,
        locked  => open );
            

end structural;
