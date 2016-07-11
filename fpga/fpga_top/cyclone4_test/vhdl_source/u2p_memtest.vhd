-------------------------------------------------------------------------------
-- Title      : u2p_memtest
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@technolution.eu>
-------------------------------------------------------------------------------
-- Description: Toplevel with just the alt-mem phy. Testing and experimenting
--              with memory latency. 
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
entity u2p_memtest is
port (
    -- slot side
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_RSTn        : inout std_logic;
    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : inout std_logic;
    SLOT_BA          : in    std_logic;
    SLOT_DMAn        : out   std_logic;
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

    -- Cassette Interface
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;

architecture rtl of u2p_memtest is

    component pll
        PORT
        (
            inclk0      : IN STD_LOGIC  := '0';
            c0          : OUT STD_LOGIC ;
            c1          : OUT STD_LOGIC ;
            locked      : OUT STD_LOGIC 
        );
    end component;

    component memphy
    port (
        pll_ref_clk : IN STD_LOGIC;
        global_reset_n  : IN STD_LOGIC;
        soft_reset_n    : IN STD_LOGIC;
        ctl_dqs_burst   : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_wdata_valid : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_wdata   : IN STD_LOGIC_VECTOR (31 DOWNTO 0);
        ctl_dm  : IN STD_LOGIC_VECTOR (3 DOWNTO 0);
        ctl_addr    : IN STD_LOGIC_VECTOR (27 DOWNTO 0);
        ctl_ba  : IN STD_LOGIC_VECTOR (3 DOWNTO 0);
        ctl_cas_n   : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_cke : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_cs_n    : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_odt : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_ras_n   : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_we_n    : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_rst_n   : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_mem_clk_disable : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        ctl_doing_rd    : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_cal_req : IN STD_LOGIC;
        ctl_cal_byte_lane_sel_n : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        dbg_clk : IN STD_LOGIC;
        dbg_reset_n : IN STD_LOGIC;
        dbg_addr    : IN STD_LOGIC_VECTOR (12 DOWNTO 0);
        dbg_wr  : IN STD_LOGIC;
        dbg_rd  : IN STD_LOGIC;
        dbg_cs  : IN STD_LOGIC;
        dbg_wr_data : IN STD_LOGIC_VECTOR (31 DOWNTO 0);
        reset_request_n : OUT STD_LOGIC;
        ctl_clk : OUT STD_LOGIC;
        ctl_reset_n : OUT STD_LOGIC;
        ctl_wlat    : OUT STD_LOGIC_VECTOR (4 DOWNTO 0);
        ctl_rdata   : OUT STD_LOGIC_VECTOR (31 DOWNTO 0);
        ctl_rdata_valid : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
        ctl_rlat    : OUT STD_LOGIC_VECTOR (4 DOWNTO 0);
        ctl_cal_success : OUT STD_LOGIC;
        ctl_cal_fail    : OUT STD_LOGIC;
        ctl_cal_warning : OUT STD_LOGIC;
        mem_addr    : OUT STD_LOGIC_VECTOR (13 DOWNTO 0);
        mem_ba  : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
        mem_cas_n   : OUT STD_LOGIC;
        mem_cke : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_cs_n    : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_dm  : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_odt : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_ras_n   : OUT STD_LOGIC;
        mem_we_n    : OUT STD_LOGIC;
        mem_reset_n : OUT STD_LOGIC;
        dbg_rd_data : OUT STD_LOGIC_VECTOR (31 DOWNTO 0);
        dbg_waitrequest : OUT STD_LOGIC;
        aux_half_rate_clk   : OUT STD_LOGIC;
        aux_full_rate_clk   : OUT STD_LOGIC;
        mem_clk : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_clk_n   : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_dq  : INOUT STD_LOGIC_VECTOR (7 DOWNTO 0);
        mem_dqs : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        mem_dqs_n   : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0)
    );
    end component;

    signal init_done    : std_logic;
    signal por_n        : std_logic;
    signal por_count    : unsigned(23 downto 0) := (others => '0');
    signal led_n        : std_logic_vector(0 to 3);
    
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal sys_reset_n  : std_logic;
    signal eth_reset    : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    signal reset_request_n  : std_logic := '1';

begin
    process(RMII_REFCLK, reset_request_n)
    begin
        if reset_request_n = '0' then
            por_count <= (others => '0');
        elsif rising_edge(RMII_REFCLK) then
            if por_count = X"FFFFFF" then
                por_n <= '1';
            else
                por_n <= '0';
                por_count <= por_count + 1;
            end if;
        end if;
    end process;

    sys_reset <= not sys_reset_n;
    
    i_pll: pll port map (
        inclk0  => RMII_REFCLK, -- 50 MHz
        c0      => HUB_CLOCK, -- 24 MHz
        c1      => audio_clock, -- 12.245 MHz (47.831 kHz sample rate)
        locked  => open );

    i_audio_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => audio_clock,
        input       => not sys_reset_n,
        input_c     => audio_reset  );
    
    i_ulpi_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => ulpi_clock,
        input       => sys_reset,
        input_c     => ulpi_reset_i  );

    i_eth_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => RMII_REFCLK,
        input       => sys_reset,
        input_c     => eth_reset  );

    i_memphy: memphy
    port map (
        pll_ref_clk             => RMII_REFCLK,
        global_reset_n          => por_n,
        soft_reset_n            => '1',
        reset_request_n         => reset_request_n,
        aux_half_rate_clk       => open,
        aux_full_rate_clk       => open,

        ctl_clk                 => sys_clock,
        ctl_reset_n             => sys_reset_n,
        ctl_dqs_burst           => ctl_dqs_burst,
        ctl_wdata_valid         => ctl_wdata_valid,
        ctl_wdata               => ctl_wdata,
        ctl_dm                  => ctl_dm,
        ctl_addr                => ctl_addr,
        ctl_ba                  => ctl_ba,
        ctl_cas_n               => ctl_cas_n,
        ctl_cke                 => ctl_cke,
        ctl_cs_n                => ctl_cs_n,
        ctl_odt                 => ctl_odt,
        ctl_ras_n               => ctl_ras_n,
        ctl_we_n                => ctl_we_n,
        ctl_rst_n               => ctl_rst_n,
        ctl_mem_clk_disable     => ctl_mem_clk_disable,
        ctl_doing_rd            => ctl_doing_rd,
        ctl_cal_req             => ctl_cal_req,
        ctl_cal_byte_lane_sel_n => ctl_cal_byte_lane_sel_n,
        ctl_wlat                => ctl_wlat,
        ctl_rdata               => ctl_rdata,
        ctl_rdata_valid         => ctl_rdata_valid,
        ctl_rlat                => ctl_rlat,

        ctl_cal_success         => ctl_cal_success,
        ctl_cal_fail            => ctl_cal_fail,
        ctl_cal_warning         => ctl_cal_warning,

        dbg_clk                 => sys_clock,
        dbg_reset_n             => sys_reset_n,
        dbg_addr                => (others => '0'),
        dbg_wr                  => '0',
        dbg_rd                  => '0',
        dbg_cs                  => '0',
        dbg_wr_data             => (others => '0'),
        dbg_rd_data             => open,
        dbg_waitrequest         => open,

        mem_addr                => SDRAM_A,
        mem_ba                  => SDRAM_BA(1 downto 0),
        mem_cas_n               => SDRAM_CASn,
        mem_cke(0)              => SDRAM_CKE,
        mem_cs_n(0)             => SDRAM_CSn,
        mem_dm(0)               => SDRAM_DM,
        mem_odt(0)              => SDRAM_ODT,
        mem_ras_n               => SDRAM_RASn,
        mem_we_n                => SDRAM_WEn,
        mem_reset_n             => open,
        mem_clk(0)              => SDRAM_CLK,
        mem_clk_n(0)            => SDRAM_CLKn,
        mem_dq                  => SDRAM_DQ,
        mem_dqs(0)              => SDRAM_DQS,
        mem_dqs_n               => mem_dqs_n
    );
    

    MDIO_CLK       <= 'Z';
    MDIO_DATA      <= 'Z';
    ETH_RESETn     <= '1';
    HUB_RESETn     <= eth_reset;
    SPEAKER_ENABLE <= '0';

    SLOT_ADDR   <= (others => 'Z');
    SLOT_DATA   <= (others => 'Z');

    -- top
    SLOT_DMAn        <= 'Z'; 
    SLOT_ROMLn       <= 'Z'; 
    SLOT_IO2n        <= 'Z'; 
    SLOT_EXROMn      <= 'Z'; 
    SLOT_GAMEn       <= 'Z'; 
    SLOT_IO1n        <= 'Z'; 
    SLOT_RWn         <= 'Z'; 
    SLOT_IRQn        <= 'Z'; 
    SLOT_NMIn        <= 'Z'; 
    SLOT_RSTn        <= 'Z'; 
    SLOT_ROMHn       <= 'Z'; 
    
    -- Cassette Interface
    CAS_SENSE   <= '0'; 
    CAS_READ    <= '0'; 
    CAS_WRITE   <= '0'; 

    LED_MOTORn <= init_done;
    LED_DISKn  <= sys_reset;
    LED_CARTn  <= button_i(0) xor button_i(1) xor button_i(2);
    LED_SDACTn <= SLOT_BA xor SLOT_DOTCLK xor SLOT_PHI2 xor CAS_MOTOR xor SLOT_VCC;

    button_i <= not BUTTON;

    SLOT_BUFFER_ENn <= '1'; -- we don't connect to a C64
    
    -- Debug UART
    UART_TXD    <= '1';
    
    -- Flash Interface
    FLASH_SEL    <= '0';
    FLASH_SELCK  <= '0';
    FLASH_CSn    <= '1';
    FLASH_SCK    <= '1';
    FLASH_MOSI   <= '1';

    -- USB Interface (ULPI)
    ULPI_RESET <= por_n;
    ULPI_STP    <= '0';
    ULPI_DATA   <= (others => 'Z');

end architecture;
