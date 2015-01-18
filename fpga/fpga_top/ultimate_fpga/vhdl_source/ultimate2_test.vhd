
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ultimate2_test is
port (
    CLOCK       : in    std_logic;
    
    -- slot side
    PHI2        : out std_logic;
    DOTCLK      : out std_logic;
    RSTn        : out std_logic;
    BUFFER_ENn  : out std_logic;
    SLOT_ADDR   : out std_logic_vector(15 downto 0);
    SLOT_DATA   : out std_logic_vector(7 downto 0);
    RWn         : out std_logic;
    BA          : in  std_logic;
    DMAn        : out std_logic;
    EXROMn      : out std_logic;
    GAMEn       : out std_logic;
    ROMHn       : out std_logic;
    ROMLn       : out std_logic;
    IO1n        : in  std_logic;
    IO2n        : out std_logic;
    IRQn        : out std_logic;
    NMIn        : out std_logic;
    
    -- local bus side
    LB_ADDR     : out   std_logic_vector(14 downto 0); -- DRAM A
    LB_DATA     : inout std_logic_vector(7 downto 0);
    
    SDRAM_CSn   : out   std_logic;
    SDRAM_RASn  : out   std_logic;
    SDRAM_CASn  : out   std_logic;
    SDRAM_WEn   : out   std_logic;
    SDRAM_DQM   : out   std_logic;
    SDRAM_CKE   : out   std_logic;
    SDRAM_CLK   : out   std_logic;
     
    -- PWM outputs (for audio)
    PWM_OUT     : out   std_logic_vector(1 downto 0) := "11";

    -- IEC bus
    IEC_ATN     : inout std_logic;
    IEC_DATA    : inout std_logic;
    IEC_CLOCK   : inout std_logic;
    IEC_RESET   : in    std_logic;
    IEC_SRQ_IN  : inout std_logic;
    
    DISK_ACTn   : out   std_logic; -- activity LED
    CART_LEDn   : out   std_logic;
    SDACT_LEDn  : out   std_logic;
    MOTOR_LEDn  : out   std_logic;
    
    -- Debug UART
    UART_TXD    : out   std_logic;
    UART_RXD    : in    std_logic;
    
    -- SD Card Interface
    SD_SSn      : out   std_logic;
    SD_CLK      : out   std_logic;
    SD_MOSI     : out   std_logic;
    SD_MISO     : in    std_logic;
    SD_CARDDETn : in    std_logic;
    SD_DATA     : inout std_logic_vector(2 downto 1);
    
    -- RTC Interface
    RTC_CS      : out   std_logic;
    RTC_SCK     : out   std_logic;
    RTC_MOSI    : out   std_logic;
    RTC_MISO    : in    std_logic;

    -- Flash Interface
    FLASH_CSn   : out   std_logic;
    FLASH_SCK   : out   std_logic;
    FLASH_MOSI  : out   std_logic;
    FLASH_MISO  : in    std_logic;

    -- USB Interface (ULPI)
    ULPI_RESET  : out   std_logic;
    ULPI_CLOCK  : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;
    ULPI_DIR    : in    std_logic;
    ULPI_DATA   : inout std_logic_vector(7 downto 0);

    -- Cassette Interface
    CAS_MOTOR   : out std_logic := '0';
    CAS_SENSE   : out std_logic := 'Z';
    CAS_READ    : out std_logic := 'Z';
    CAS_WRITE   : out std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));
    
end ultimate2_test;


architecture structural of ultimate2_test is
    signal counter : unsigned(23 downto 0) := (others => '0');
    signal pulses  : unsigned(23 downto 1) := (others => '0');
begin
    process(CLOCK)
    begin
        if rising_edge(CLOCK) then
            counter <= counter + 1;
            pulses <= counter(23 downto 1) and counter(22 downto 0);
        end if;
    end process;

    -- slot side
    BUFFER_ENn  <= '0';

    SLOT_ADDR   <= std_logic_vector(counter(23 downto 8));
    SLOT_DATA   <= std_logic_vector(counter(7 downto 0));

    -- top
    DMAn        <= pulses(1);
    --BA          <= pulses(2);
    ROMLn       <= pulses(3);
    IO2n        <= pulses(4);
    EXROMn      <= pulses(5);
    GAMEn       <= pulses(6);
    --IO1n        <= pulses(7);
    DOTCLK      <= pulses(8);
    RWn         <= pulses(9);
    IRQn        <= pulses(10);

    PHI2        <= pulses(11);
    NMIn        <= pulses(12);
    RSTn        <= pulses(13);
    ROMHn       <= pulses(14);
    
    -- Cassette Interface
    CAS_SENSE   <= pulses(16);
    CAS_READ    <= pulses(17);
    CAS_WRITE   <= pulses(18);
    CAS_MOTOR   <= pulses(19);
    
    -- local bus side
    LB_ADDR     <= (others => 'Z');
    LB_DATA     <= (others => 'Z');
    
    SDRAM_CSn   <= 'Z';
    SDRAM_RASn  <= 'Z';
    SDRAM_CASn  <= 'Z';
    SDRAM_WEn   <= 'Z';
    SDRAM_DQM   <= 'Z';
    SDRAM_CKE   <= 'Z';
    SDRAM_CLK   <= 'Z';
     
    -- PWM outputs (for audio)
    PWM_OUT     <= "ZZ";

    -- IEC bus
    IEC_ATN     <= 'Z';
    IEC_DATA    <= 'Z';
    IEC_CLOCK   <= 'Z';
    IEC_SRQ_IN  <= 'Z';
    
    DISK_ACTn   <= '0';
    CART_LEDn   <= '1';
    SDACT_LEDn  <= '0';
    MOTOR_LEDn  <= '1';
    
    -- Debug UART
    UART_TXD    <= '1';
    
    -- SD Card Interface
    SD_SSn      <= 'Z';
    SD_CLK      <= 'Z';
    SD_MOSI     <= 'Z';
    SD_DATA     <= "ZZ";
    
    -- RTC Interface
    RTC_CS      <= '0';
    RTC_SCK     <= '0';
    RTC_MOSI    <= '0';

    -- Flash Interface
    FLASH_CSn   <= '1';
    FLASH_SCK   <= '1';
    FLASH_MOSI  <= '1';

    -- USB Interface (ULPI)
    ULPI_RESET  <= '0';
    ULPI_STP    <= '0';
    ULPI_DATA   <= (others => 'Z');

end structural;
