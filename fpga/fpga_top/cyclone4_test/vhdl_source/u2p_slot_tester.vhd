-------------------------------------------------------------------------------
-- Title      : u2p_slot_tester
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Toplevel for u2p_slot_tester.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
entity u2p_slot_tester is
port (
    -- slot side
    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_BA          : in    std_logic;
    SLOT_RSTn        : in    std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : inout std_logic;
    SLOT_DMAn        : inout std_logic;
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
    SDRAM_A     : out   std_logic_vector(13 downto 0) := (others => '0'); -- DRAM A
    SDRAM_BA    : out   std_logic_vector(2 downto 0) := (others => '0');
    SDRAM_DQ    : inout std_logic_vector(7 downto 0) := (others => 'Z');
    SDRAM_DM    : inout std_logic := 'Z';
    SDRAM_CSn   : out   std_logic := '1';
    SDRAM_RASn  : out   std_logic := '1';
    SDRAM_CASn  : out   std_logic := '1';
    SDRAM_WEn   : out   std_logic := '1';
    SDRAM_CKE   : out   std_logic := '1';
    SDRAM_CLK   : out   std_logic := '1';
    SDRAM_CLKn  : out   std_logic := '1';
    SDRAM_ODT   : out   std_logic := '1';
    SDRAM_DQS   : inout std_logic := 'Z';
     
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
    RMII_TX_DATA    : out std_logic_vector(1 downto 0) := "00";
    RMII_TX_EN      : out std_logic := '0';

    MDIO_CLK    : out   std_logic := '0';
    MDIO_DATA   : inout std_logic := 'Z';

    -- Speaker data
    SPEAKER_DATA    : out std_logic := '0';
    SPEAKER_ENABLE  : out std_logic := '0';

    -- Debug UART
    UART_TXD    : out   std_logic := '1';
    UART_RXD    : in    std_logic;
    
    -- I2C Interface for RTC, audio codec and usb hub
    I2C_SDA     : inout std_logic := 'Z';
    I2C_SCL     : inout std_logic := 'Z';
    I2C_SDA_18  : inout std_logic := 'Z';
    I2C_SCL_18  : inout std_logic := 'Z';

    -- Flash Interface
    FLASH_CSn   : out   std_logic := '1';
    FLASH_SCK   : out   std_logic := '1';
    FLASH_MOSI  : out   std_logic := '1';
    FLASH_MISO  : in    std_logic;
    FLASH_SEL   : out   std_logic := '0';
    FLASH_SELCK : out   std_logic := '0';

    -- USB Interface (ULPI)
    ULPI_RESET  : out   std_logic := '1';
    ULPI_CLOCK  : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic := '1';
    ULPI_DIR    : in    std_logic;
    ULPI_DATA   : inout std_logic_vector(7 downto 0) := (others => 'Z');

    HUB_RESETn  : out   std_logic := '1';
    HUB_CLOCK   : out   std_logic := '0';

    -- Misc
    BOARD_REVn  : in    std_logic_vector(4 downto 0);

    -- Cassette Interface
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : in    std_logic := '0';
    CAS_READ    : in    std_logic := '0';
    CAS_WRITE   : in    std_logic := '0';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;

architecture rtl of u2p_slot_tester is

    signal por_n        : std_logic;
    signal ref_reset    : std_logic;
    signal por_count    : unsigned(19 downto 0) := (others => '0');
begin
    process(RMII_REFCLK)
    begin
        if rising_edge(RMII_REFCLK) then
            if por_count = X"FFFFF" then
                por_n <= '1';
            else
                por_count <= por_count + 1;
                por_n <= '0';
            end if;
        end if;
    end process;
    ref_reset <= not por_n;
    
    i_tester: entity work.u64test_remote
    port map(
        arst      => ref_reset,
        
        BUTTON    => BUTTON,
        
        PHI2      => SLOT_PHI2,
        DOTCLK    => SLOT_DOTCLK,
        IO1n      => SLOT_IO1n,
        IO2n      => SLOT_IO2n,
        ROMLn     => SLOT_ROMLn,
        ROMHn     => SLOT_ROMHn,
        BA        => SLOT_BA,
        RWn       => SLOT_RWn,
        GAMEn     => SLOT_GAMEn,
        EXROMn    => SLOT_EXROMn,
        DMAn      => SLOT_DMAn,
        IRQn      => SLOT_IRQn,
        NMIn      => SLOT_NMIn,
        RESETn    => SLOT_RSTn,
        D         => SLOT_DATA,
        A         => SLOT_ADDR,

        LED_DISKn  => LED_DISKn, 
        LED_CARTn  => LED_CARTn,
        LED_SDACTn => LED_SDACTn,
        LED_MOTORn => LED_MOTORn,

        IEC_SRQ   => IEC_SRQ_IN,
        IEC_RST   => IEC_RESET,
        IEC_CLK   => IEC_CLOCK,
        IEC_DATA  => IEC_DATA,
        IEC_ATN   => IEC_ATN,

        CAS_READ  => CAS_READ,
        CAS_WRITE => CAS_WRITE,
        CAS_MOTOR => CAS_MOTOR,
        CAS_SENSE => CAS_SENSE
    );

    SLOT_BUFFER_ENn <= '0';
end architecture;
