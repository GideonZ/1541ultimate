
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cyclone_test is
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
    SLOT_ROMHn       : out   std_logic;
    SLOT_ROMLn       : out   std_logic;
    SLOT_IO1n        : out   std_logic;
    SLOT_IO2n        : out   std_logic;
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
    SDRAM_DM    : out   std_logic := 'Z';
    SDRAM_CKE   : out   std_logic;
    SDRAM_CLK   : inout std_logic;
    SDRAM_CLKn  : inout std_logic;
    SDRAM_ODT   : out   std_logic := 'Z';
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
    
end cyclone_test;


architecture structural of cyclone_test is
    signal clock    : std_logic;
    signal counter : unsigned(23 downto 0) := (others => '0');
    signal pulses  : unsigned(23 downto 1) := (others => '0');
begin
    clock <= RMII_REFCLK;
    
    process(clock)
    begin
        if rising_edge(clock) then
            counter <= counter + 1;
            pulses <= counter(23 downto 1) and counter(22 downto 0);
        end if;
    end process;

    -- slot side
    -- BUFFER_ENn  <= '0';

    SLOT_ADDR   <= std_logic_vector(counter(23 downto 8));
    SLOT_DATA   <= std_logic_vector(counter(7 downto 0));

    -- top
    SLOT_DMAn        <= pulses(1);
    -- SLOT_BA          <= pulses(2);
    SLOT_ROMLn       <= pulses(3);
    SLOT_IO2n        <= pulses(4);
    SLOT_EXROMn      <= pulses(5);
    SLOT_GAMEn       <= pulses(6);
    SLOT_IO1n        <= pulses(7);
    -- SLOT_DOTCLK      <= pulses(8);
    SLOT_RWn         <= pulses(9);
    SLOT_IRQn        <= pulses(10);
    -- SLOT_PHI2        <= pulses(11);
    SLOT_NMIn        <= pulses(12);
    SLOT_RSTn        <= pulses(13);
    SLOT_ROMHn       <= pulses(14);
    
    -- Cassette Interface
    CAS_SENSE   <= pulses(16);
    CAS_READ    <= pulses(17);
    CAS_WRITE   <= pulses(18);
--    CAS_MOTOR   <= pulses(19);
    
    -- local bus side
    SDRAM_A     <= (others => 'Z');
    SDRAM_BA    <= (others => 'Z');
    SDRAM_CSn   <= 'Z';
    SDRAM_RASn  <= 'Z';
    SDRAM_CASn  <= 'Z';
    SDRAM_WEn   <= 'Z';
    SDRAM_DM    <= 'Z';
    SDRAM_CKE   <= 'Z';
    SDRAM_CLK   <= 'Z';
     
    -- PWM outputs (for audio)
    SPEAKER_DATA   <= '0';
    SPEAKER_ENABLE <= '0';

    -- IEC bus
    IEC_ATN     <= 'Z';
    IEC_DATA    <= 'Z';
    IEC_CLOCK   <= 'Z';
    IEC_SRQ_IN  <= 'Z';
    
    LED_DISKn   <= '0';
    LED_CARTn   <= '1';
    LED_SDACTn  <= '0';
    LED_MOTORn  <= '1';
    
    -- Debug UART
    UART_TXD    <= '1';
    
    -- Flash Interface
    FLASH_CSn   <= '1';
    FLASH_SCK   <= '1';
    FLASH_MOSI  <= '1';

    -- USB Interface (ULPI)
    ULPI_RESET  <= '0';
    ULPI_STP    <= '0';
    ULPI_DATA   <= (others => 'Z');

end structural;
