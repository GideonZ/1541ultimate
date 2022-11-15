library ieee;
use ieee.std_logic_1164.all;

entity tb_top is
end entity;

architecture tb of tb_top is
    -- in
    signal CLOCK_50        : std_logic := '0';
    signal SLOT_PHI2       : std_logic := '0';
    signal SLOT_DOTCLK     : std_logic := '0';
    signal SLOT_BA         : std_logic := '0';
    signal SLOT_ROMHn      : std_logic := '0';
    signal SLOT_ROMLn      : std_logic := '0';
    signal SLOT_IO1n       : std_logic := '0';
    signal SLOT_IO2n       : std_logic := '0';
    signal SLOT_VCC        : std_logic := '0';
    signal AUDIO_SDI       : std_logic := '0';
    signal DEBUG_TRSTn     : std_logic := '0'; -- low-active TAP reset (optional)
    signal DEBUG_TCK       : std_logic := '0'; -- serial clock
    signal DEBUG_TMS       : std_logic := '1'; -- mode select
    signal DEBUG_TDI       : std_logic := '1'; -- serial data input
    signal IEC_ATN_I       : std_logic := '0';
    signal IEC_DATA_I      : std_logic := '0';
    signal IEC_CLOCK_I     : std_logic := '0';
    signal IEC_RESET_I     : std_logic := '0';
    signal IEC_SRQ_I       : std_logic := '0';
    signal ETH_IRQn        : std_logic := '1';
    signal RMII_REFCLK     : std_logic := '0';
    signal RMII_CRS_DV     : std_logic := '0';
    signal RMII_RX_ER      : std_logic := '0';
    signal RMII_RX_DATA    : std_logic_vector(1 downto 0) := "00";
    signal UART_RXD        : std_logic := '1';
    signal FLASH_MISO      : std_logic := '1';
    signal ULPI_CLOCK      : std_logic := '1';
    signal ULPI_NXT        : std_logic := '1';
    signal ULPI_DIR        : std_logic := '1';
    signal BOARD_REVn      : std_logic_vector(4 downto 0) := "01010";
    signal CAS_MOTOR       : std_logic := '0';
    signal BUTTON          : std_logic_vector(2 downto 0) := "111";
    -- inout
    signal SLOT_RSTn       : std_logic;
    signal SLOT_ADDR       : std_logic_vector(15 downto 0);
    signal SLOT_DATA       : std_logic_vector(7 downto 0);
    signal SLOT_RWn        : std_logic;
    signal SLOT_EXROMn     : std_logic;
    signal SLOT_GAMEn      : std_logic;
    signal SLOT_IRQn       : std_logic;
    signal SLOT_NMIn       : std_logic;
    signal SDRAM_DQ        : std_logic_vector(7 downto 0);
    signal SDRAM_DM        : std_logic;
    signal SDRAM_DQS       : std_logic;
    signal MDIO_DATA       : std_logic := 'Z';
    signal USB_DP          : std_logic;
    signal USB_DM          : std_logic;
    signal I2C_SDA         : std_logic := 'Z';
    signal I2C_SCL         : std_logic := 'Z';
    signal I2C_SDA_18      : std_logic := 'Z';
    signal I2C_SCL_18      : std_logic := 'Z';
    signal ULPI_DATA       : std_logic_vector(7 downto 0);
    signal CAS_SENSE       : std_logic;
    signal CAS_READ        : std_logic;
    signal CAS_WRITE       : std_logic;
    --out
    signal SDRAM_CLK       : std_logic;
    signal SDRAM_CLKn      : std_logic;
    signal SLOT_ADDR_OEn   : std_logic;
    signal SLOT_ADDR_DIR   : std_logic;
    signal SLOT_BUFFER_EN  : std_logic;
    signal SLOT_DMAn       : std_logic;
    signal SLOT_DRV_RST    : std_logic := '0';
    signal SDRAM_A         : std_logic_vector(13 downto 0); -- DRAM A
    signal SDRAM_BA        : std_logic_vector(2 downto 0) := (others => '0');
    signal SDRAM_CSn       : std_logic;
    signal SDRAM_RASn      : std_logic;
    signal SDRAM_CASn      : std_logic;
    signal SDRAM_WEn       : std_logic;
    signal SDRAM_CKE       : std_logic;
    signal SDRAM_ODT       : std_logic;
    signal AUDIO_MCLK      : std_logic := '0';
    signal AUDIO_BCLK      : std_logic := '0';
    signal AUDIO_LRCLK     : std_logic := '0';
    signal AUDIO_SDO       : std_logic := '0';
    signal DEBUG_TDO       : std_ulogic;        -- serial data output
    signal DEBUG_SPARE     : std_ulogic := '0';
    signal UNUSED_G12      : std_logic; -- not on test pad
    signal UNUSED_H3       : std_logic;
    signal UNUSED_F4       : std_logic;
    signal IEC_ATN_O       : std_logic;
    signal IEC_DATA_O      : std_logic;
    signal IEC_CLOCK_O     : std_logic;
    signal IEC_RESET_O     : std_logic;
    signal IEC_SRQ_O       : std_logic;
    signal LED_DISKn       : std_logic; -- activity LED
    signal LED_CARTn       : std_logic;
    signal LED_SDACTn      : std_logic;
    signal LED_MOTORn      : std_logic;
    signal ETH_RESETn      : std_logic := '1';
    signal RMII_TX_DATA    : std_logic_vector(1 downto 0);
    signal RMII_TX_EN      : std_logic;
    signal MDIO_CLK        : std_logic := '0';
    signal SPEAKER_DATA    : std_logic := '0';
    signal SPEAKER_ENABLE  : std_logic := '0';
    signal UART_TXD        : std_logic;
    signal USB_PULLUP      : std_logic;
    signal FLASH_CSn       : std_logic;
    signal FLASH_MOSI      : std_logic;
    signal ULPI_REFCLK     : std_logic;
    signal ULPI_RESET      : std_logic;
    signal ULPI_STP        : std_logic;
    signal HUB_RESETn      : std_logic := '1';
    signal HUB_CLOCK       : std_logic := '0';
    signal PURNET          : std_logic;
begin
    PUR_INST: entity work.P port map (PUR => PURNET);
    PURNET <= '0', '1' after 50 ns;
    
    SLOT_RSTn   <= 'H';
    SLOT_ADDR   <= (others => 'H');
    SLOT_DATA   <= (others => 'H');
    SLOT_RWn    <= 'H';
    SLOT_EXROMn <= 'H';
    SLOT_GAMEn  <= 'H';
    SLOT_IRQn   <= 'H';
    SLOT_NMIn   <= 'H';
    SDRAM_DQ    <= (others => 'H');
    SDRAM_DM    <= 'H';
    SDRAM_DQS   <= 'H';
    MDIO_DATA   <= 'H';
    USB_DP      <= 'H';
    USB_DM      <= 'H';
    I2C_SDA     <= 'H';
    I2C_SCL     <= 'H';
    I2C_SDA_18  <= 'H';
    I2C_SCL_18  <= 'H';
    ULPI_DATA   <= (others => 'H');
    CAS_SENSE   <= 'H';
    CAS_READ    <= 'H';
    CAS_WRITE   <= 'H';  

    RMII_REFCLK <= not RMII_REFCLK after 10 ns;

    i_dut: entity work.u2p_riscv_lattice
    port map (
        CLOCK_50       => CLOCK_50,
        SLOT_ADDR_OEn  => SLOT_ADDR_OEn,
        SLOT_ADDR_DIR  => SLOT_ADDR_DIR,
        SLOT_PHI2      => SLOT_PHI2,
        SLOT_DOTCLK    => SLOT_DOTCLK,
        SLOT_RSTn      => SLOT_RSTn,
        SLOT_BUFFER_EN => SLOT_BUFFER_EN,
        SLOT_ADDR      => SLOT_ADDR,
        SLOT_DATA      => SLOT_DATA,
        SLOT_RWn       => SLOT_RWn,
        SLOT_BA        => SLOT_BA,
        SLOT_DMAn      => SLOT_DMAn,
        SLOT_EXROMn    => SLOT_EXROMn,
        SLOT_GAMEn     => SLOT_GAMEn,
        SLOT_ROMHn     => SLOT_ROMHn,
        SLOT_ROMLn     => SLOT_ROMLn,
        SLOT_IO1n      => SLOT_IO1n,
        SLOT_IO2n      => SLOT_IO2n,
        SLOT_IRQn      => SLOT_IRQn,
        SLOT_NMIn      => SLOT_NMIn,
        SLOT_VCC       => SLOT_VCC,
        SLOT_DRV_RST   => SLOT_DRV_RST,
        SDRAM_A        => SDRAM_A,
        SDRAM_BA       => SDRAM_BA,
        SDRAM_DQ       => SDRAM_DQ,
        SDRAM_DM       => SDRAM_DM,
        SDRAM_CSn      => SDRAM_CSn,
        SDRAM_RASn     => SDRAM_RASn,
        SDRAM_CASn     => SDRAM_CASn,
        SDRAM_WEn      => SDRAM_WEn,
        SDRAM_CKE      => SDRAM_CKE,
        SDRAM_CLK      => SDRAM_CLK,
        SDRAM_CLKn     => SDRAM_CLKn,
        SDRAM_ODT      => SDRAM_ODT,
        SDRAM_DQS      => SDRAM_DQS,
        AUDIO_MCLK     => AUDIO_MCLK,
        AUDIO_BCLK     => AUDIO_BCLK,
        AUDIO_LRCLK    => AUDIO_LRCLK,
        AUDIO_SDO      => AUDIO_SDO,
        AUDIO_SDI      => AUDIO_SDI,
        DEBUG_TRSTn    => DEBUG_TRSTn,
        DEBUG_TCK      => DEBUG_TCK,
        DEBUG_TMS      => DEBUG_TMS,
        DEBUG_TDI      => DEBUG_TDI,
        DEBUG_TDO      => DEBUG_TDO,
        DEBUG_SPARE    => DEBUG_SPARE,
        UNUSED_G12     => UNUSED_G12,
        UNUSED_H3      => UNUSED_H3,
        UNUSED_F4      => UNUSED_F4,
        IEC_ATN_O      => IEC_ATN_O,
        IEC_DATA_O     => IEC_DATA_O,
        IEC_CLOCK_O    => IEC_CLOCK_O,
        IEC_RESET_O    => IEC_RESET_O,
        IEC_SRQ_O      => IEC_SRQ_O,
        IEC_ATN_I      => IEC_ATN_I,
        IEC_DATA_I     => IEC_DATA_I,
        IEC_CLOCK_I    => IEC_CLOCK_I,
        IEC_RESET_I    => IEC_RESET_I,
        IEC_SRQ_I      => IEC_SRQ_I,
        LED_DISKn      => LED_DISKn,
        LED_CARTn      => LED_CARTn,
        LED_SDACTn     => LED_SDACTn,
        LED_MOTORn     => LED_MOTORn,
        ETH_RESETn     => ETH_RESETn,
        ETH_IRQn       => ETH_IRQn,
        RMII_REFCLK    => RMII_REFCLK,
        RMII_CRS_DV    => RMII_CRS_DV,
        RMII_RX_ER     => RMII_RX_ER,
        RMII_RX_DATA   => RMII_RX_DATA,
        RMII_TX_DATA   => RMII_TX_DATA,
        RMII_TX_EN     => RMII_TX_EN,
        MDIO_CLK       => MDIO_CLK,
        MDIO_DATA      => MDIO_DATA,
        SPEAKER_DATA   => SPEAKER_DATA,
        SPEAKER_ENABLE => SPEAKER_ENABLE,
        UART_TXD       => UART_TXD,
        UART_RXD       => UART_RXD,
        USB_DP         => USB_DP,
        USB_DM         => USB_DM,
        USB_PULLUP     => USB_PULLUP,
        I2C_SDA        => I2C_SDA,
        I2C_SCL        => I2C_SCL,
        I2C_SDA_18     => I2C_SDA_18,
        I2C_SCL_18     => I2C_SCL_18,
        FLASH_CSn      => FLASH_CSn,
        FLASH_MOSI     => FLASH_MOSI,
        FLASH_MISO     => FLASH_MISO,
        ULPI_REFCLK    => ULPI_REFCLK,
        ULPI_RESET     => ULPI_RESET,
        ULPI_CLOCK     => ULPI_CLOCK,
        ULPI_NXT       => ULPI_NXT,
        ULPI_STP       => ULPI_STP,
        ULPI_DIR       => ULPI_DIR,
        ULPI_DATA      => ULPI_DATA,
        HUB_RESETn     => HUB_RESETn,
        HUB_CLOCK      => HUB_CLOCK,
        BOARD_REVn     => BOARD_REVn,
        CAS_MOTOR      => CAS_MOTOR,
        CAS_SENSE      => CAS_SENSE,
        CAS_READ       => CAS_READ,
        CAS_WRITE      => CAS_WRITE,
        BUTTON         => BUTTON
    );
    
end architecture;

