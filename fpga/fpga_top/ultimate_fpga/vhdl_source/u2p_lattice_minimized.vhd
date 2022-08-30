-------------------------------------------------------------------------------
-- Title      : u2p_riscv
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Toplevel based on the RiscV CPU core.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
        
library ECP5U;
use ECP5U.components.all;

entity u2p_lattice_minimized is
generic (
    g_jtag_debug     : boolean := true );
port (
    -- (Optional) Oscillator
    CLOCK_50         : in    std_logic := '0';
    
    -- slot side
    SLOT_ADDR_OEn    : out   std_logic;
    SLOT_ADDR_DIR    : out   std_logic;
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_RSTn        : inout std_logic;
    SLOT_BUFFER_EN   : out   std_logic;
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

    -- JTAG on-chip debugger interface (available if ON_CHIP_DEBUGGER_EN = true) --
    DEBUG_TRSTn : in    std_ulogic := '0'; -- low-active TAP reset (optional)
    DEBUG_TCK   : in    std_ulogic := '0'; -- serial clock
    DEBUG_TMS   : in    std_ulogic := '1'; -- mode select
    DEBUG_TDI   : in    std_ulogic := '1'; -- serial data input
    DEBUG_TDO   : out   std_ulogic;        -- serial data output
    DEBUG_SPARE : out   std_ulogic := '0';
    UNUSED_H3   : out   std_logic := '0';
    UNUSED_F4   : out   std_logic := '0';

    -- IEC bus
    IEC_ATN_O   : out   std_logic := '0';
    IEC_DATA_O  : out   std_logic := '0';
    IEC_CLOCK_O : out   std_logic := '0';
    IEC_RESET_O : out   std_logic := '0';
    IEC_SRQ_O   : out   std_logic := '0';

    IEC_ATN_I   : in    std_logic;
    IEC_DATA_I  : in    std_logic;
    IEC_CLOCK_I : in    std_logic;
    IEC_RESET_I : in    std_logic;
    IEC_SRQ_I   : in    std_logic;

    
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
    UART_TXD    : out   std_logic;
    UART_RXD    : in    std_logic;
    
    -- USB Experiment
    USB_DP      : inout std_logic;
    USB_DM      : inout std_logic;
    USB_PULLUP  : out   std_logic;
    
    -- I2C Interface for RTC, audio codec and usb hub
    I2C_SDA     : inout std_logic := 'Z';
    I2C_SCL     : inout std_logic := 'Z';
    I2C_SDA_18  : inout std_logic := 'Z';
    I2C_SCL_18  : inout std_logic := 'Z';

    -- Flash Interface
--    FLASH_CSn   : out   std_logic;
--    FLASH_MOSI  : out   std_logic;
--    FLASH_MISO  : in    std_logic;

    -- USB Interface (ULPI)
    ULPI_REFCLK : out   std_logic;
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
    CAS_SENSE   : inout std_logic;
    CAS_READ    : inout std_logic;
    CAS_WRITE   : inout std_logic;
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;


architecture rtl of u2p_lattice_minimized is

    component pll1
    port (
        CLKI: in  std_logic;
        CLKOP: out  std_logic; 
        CLKOS: out  std_logic;
        CLKOS2: out  std_logic; 
        CLKOS3: out  std_logic; 
        LOCK: out  std_logic);
    end component;

    signal flash_sck_o  : std_logic;
    signal flash_sck_t  : std_logic;
    signal por_n        : std_logic;
    signal pll_locked   : std_logic;
    signal ref_reset    : std_logic;
    signal por_count    : unsigned(15 downto 0) := (others => '0');
    signal led_n        : std_logic_vector(0 to 3);
    signal busy_led     : std_logic;

    signal mem_clock    : std_logic;
    signal ctrl_clock   : std_logic;
    signal toggle       : std_logic;
    signal eth_clock    : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal clock_24     : std_logic;
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
        
    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic := '0';
    signal is_idle          : std_logic;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;
    signal mem_req_2x       : t_mem_req_32;
    signal mem_resp_2x      : t_mem_resp_32;

    signal i2c_sda_i   : std_logic;
    signal i2c_sda_o   : std_logic;
    signal i2c_scl_i   : std_logic;
    signal i2c_scl_o   : std_logic;
    signal mdio_o      : std_logic;
    signal RSTn_out    : std_logic;
    signal button_i    : std_logic_vector(2 downto 0);

    -- io buses
    signal io_irq       : std_logic;
    signal io_req_riscv : t_io_req;
    signal io_resp_riscv: t_io_resp;
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    signal io_u2p_req   : t_io_req;
    signal io_u2p_resp  : t_io_resp;
    signal io_req_new_io    : t_io_req;
    signal io_resp_new_io   : t_io_resp;
    signal io_req_ddr2      : t_io_req;
    signal io_resp_ddr2     : t_io_resp;

    -- Timing
    signal tick_16MHz       : std_logic;
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

    function xor_reduce(a : std_logic_vector) return std_logic is
        variable r : std_logic := '0';
    begin
        for i in a'range loop
            r := r xor a(i);
        end loop;
        return r;
    end function;

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
    
    i_pll: pll1
    port map (
        -- CLKI   => CLOCK_50, -- 50 MHz oscillator
        CLKI   => RMII_REFCLK, -- 50 MHz
        CLKOP  => mem_clock,   -- 200 MHz
        CLKOS  => clock_24,   -- 24 MHz
        CLKOS2 => audio_clock, -- 12.245 MHz (47.831 kHz sample rate)
        CLKOS3 => sys_clock, -- 50 MHz
        LOCK   => pll_locked );

    --eth_clock <= sys_clock; -- same net
    eth_clock <= RMII_REFCLK; -- dedicated pin
    
    HUB_CLOCK <= clock_24;
    ULPI_REFCLK <= clock_24;
    
    sys_reset <= not pll_locked when rising_edge(sys_clock);

    i_riscv: entity work.neorv32_wrapper
    generic map (
        g_jtag_debug=> g_jtag_debug,
        g_frequency => 50_000_000,
        g_tag       => X"20"
    )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        cpu_reset   => '0',
        jtag_trst_i => '1', -- DEBUG_TRSTn,
        jtag_tck_i  => DEBUG_TCK,
        jtag_tdi_i  => DEBUG_TDI,
        jtag_tdo_o  => DEBUG_TDO,
        jtag_tms_i  => DEBUG_TMS,
        irq_i       => io_irq,
        irq_o       => open,
        io_req      => io_req_riscv,
        io_resp     => io_resp_riscv,
        io_busy     => open,
        mem_req     => mem_req,
        mem_resp    => mem_resp
    );
    
    i_u2p_io_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 20,
        g_range_hi => 20,
        g_ports    => 2
    )
    port map(
        clock      => sys_clock,
        req        => io_req_riscv,
        resp       => io_resp_riscv,
        reqs(0)    => io_req,
        reqs(1)    => io_u2p_req,
        resps(0)   => io_resp,
        resps(1)   => io_u2p_resp
    );

    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 8,
        g_range_hi => 9,
        g_ports    => 2
    )
    port map (
        clock      => sys_clock,
        req        => io_u2p_req,
        resp       => io_u2p_resp,
        reqs(0)    => io_req_new_io,
        reqs(1)    => io_req_ddr2,
        resps(0)   => io_resp_new_io,
        resps(1)   => io_resp_ddr2
    );


    i_timing: entity work.fractional_div
    generic map ( 
        g_numerator   => 8,  -- 16 MHz = 8/25 * 50 MHz
        g_denominator => 25
    )
    port map(
        clock         => sys_clock,
        tick          => tick_16MHz,
        tick_by_4     => tick_4MHz,
        tick_by_16    => tick_1MHz,
        one_16000     => tick_1kHz
    );

    i_itu: entity work.itu
    generic map (
		g_version	    => X"77",
        g_capabilities  => X"00000000",
        g_uart          => true,
        g_uart_rx       => false,
        g_edge_init     => "10000101",
        g_edge_write    => false,
        g_baudrate      => 115200 )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req,
        io_resp     => io_resp,
    
        tick_4MHz   => tick_4MHz,
        tick_1us    => tick_1MHz,
        tick_1ms    => tick_1kHz,
        buttons     => button_i,

        irq_out     => io_irq,
        
        busy_led    => busy_led,

        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );

--    i_double_freq_bridge: entity work.memreq_halfrate
--    generic map (
--        g_reg_in    => true
--    )
--    port map(
--        clock_1x    => sys_clock,
--        clock_2x    => ctrl_clock,
--        reset_1x    => sys_clock,
--        mem_req_1x  => mem_req,
--        mem_resp_1x => mem_resp,
--        mem_req_2x  => mem_req_2x,
--        mem_resp_2x => mem_resp_2x
--    );
--    i_double_freq_bridge: entity work.mem_bus_bridge
--    port map (
--        a_clock    => sys_clock,
--        a_reset    => sys_reset,
--        a_mem_req  => mem_req,
--        a_mem_resp => mem_resp,
--        b_clock    => ctrl_clock,
--        b_reset    => sys_reset,
--        b_mem_req  => mem_req_2x,
--        b_mem_resp => mem_resp_2x
--    );

    i_double_freq_bridge: entity work.mem_bus_bridge
    port map (
        a_clock    => sys_clock,
        a_reset    => sys_reset,
        a_mem_req  => mem_req,
        a_mem_resp => mem_resp,
        b_clock    => ctrl_clock,
        b_reset    => sys_reset,
        b_mem_req  => mem_req_2x,
        b_mem_resp => mem_resp_2x
    );

    i_memctrl: entity work.ddr2_ctrl
    port map (
        start_clock       => sys_clock,
        start_reset       => sys_reset,

        mem_clock         => mem_clock,
        reset             => sys_reset,

        io_clock          => sys_clock,
        io_reset          => sys_reset,
        io_req            => io_req_ddr2,
        io_resp           => io_resp_ddr2,
        inhibit           => memctrl_inhibit,
        is_idle           => is_idle,
        mem_clock_half    => ctrl_clock,

        req               => mem_req_2x,
        resp              => mem_resp_2x,
        
        SDRAM_TEST1       => UNUSED_H3,
        SDRAM_TEST2       => UNUSED_F4,
        SDRAM_CLK         => SDRAM_CLK,
        SDRAM_CLKn        => SDRAM_CLKn,
        SDRAM_CKE         => SDRAM_CKE,
        SDRAM_ODT         => SDRAM_ODT,
        SDRAM_CSn         => SDRAM_CSn,
        SDRAM_RASn        => SDRAM_RASn,
        SDRAM_CASn        => SDRAM_CASn,
        SDRAM_WEn         => SDRAM_WEn,
        SDRAM_A           => SDRAM_A,
        SDRAM_BA          => SDRAM_BA,
        SDRAM_DM          => SDRAM_DM,
        SDRAM_DQ          => SDRAM_DQ,
        SDRAM_DQS         => SDRAM_DQS
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
        board_rev  => not BOARD_REVn,
        eth_irq_i  => ETH_IRQn,
        speaker_en => SPEAKER_ENABLE,
        hub_reset_n=> HUB_RESETn
    );

    i2c_scl_i   <= I2C_SCL and I2C_SCL_18;
    i2c_sda_i   <= I2C_SDA and I2C_SDA_18;
    I2C_SCL     <= '0' when i2c_scl_o = '0' else 'Z';
    I2C_SDA     <= '0' when i2c_sda_o = '0' else 'Z';
    I2C_SCL_18  <= '0' when i2c_scl_o = '0' else 'Z';
    I2C_SDA_18  <= '0' when i2c_sda_o = '0' else 'Z';
    MDIO_DATA   <= '0' when mdio_o = '0' else 'Z';

    
    u1: USRMCLK
    port map (
        USRMCLKI => flash_sck_o,
        USRMCLKTS => flash_sck_t
    );

    RSTn_out <= '1';
    button_i <= not BUTTON;
    
    SLOT_RSTn <= '0' when RSTn_out = '0' else 'Z';
    SLOT_DRV_RST <= not RSTn_out when rising_edge(sys_clock); -- Drive this pin HIGH when we want to reset the C64 (uses NFET on Rev.E boards)
    
    SLOT_ADDR <= (others => 'Z');
    SLOT_DATA <= (others => 'Z');
    SLOT_RWn  <= 'Z';

    LED_MOTORn <= led_n(0) xor sys_reset;
    LED_DISKn  <= not sys_reset; --'1';--led_n(1) xor sys_reset;
    LED_CARTn  <= '1';--led_n(2) xor sys_reset;
    LED_SDACTn <= not busy_led;

    ULPI_RESET <= por_n;

    toggle <= not toggle when rising_edge(sys_clock);
    USB_DP      <= 'Z' when toggle='1' else '0';
    USB_DM      <= 'Z' when toggle='1' else '1';
    USB_PULLUP  <= '1';

    ULPI_STP    <= xor_reduce(    
        SLOT_PHI2        &
        SLOT_DOTCLK      &
        SLOT_RSTn        &
        SLOT_ADDR        &
        SLOT_DATA        &
        SLOT_RWn         &
        SLOT_BA          &
        SLOT_EXROMn      &
        SLOT_GAMEn       &
        SLOT_ROMHn       &
        SLOT_ROMLn       &
        SLOT_IO1n        &
        SLOT_IO2n        &
        SLOT_IRQn        &
        SLOT_NMIn        &
        SLOT_VCC         &
        AUDIO_SDI        &
        IEC_ATN_I        &
        IEC_DATA_I       &
        IEC_CLOCK_I      &
        IEC_RESET_I      &
        IEC_SRQ_I        &
        ETH_IRQn         &
        RMII_REFCLK      &
        RMII_CRS_DV      &
        RMII_RX_ER       &
        RMII_RX_DATA     &
        UART_RXD         &
--        FLASH_MISO       &
        ULPI_NXT         &
        ULPI_DIR         &
        ULPI_DATA        &
        CAS_MOTOR        &
        CAS_SENSE        &
        CAS_READ         &
        CAS_WRITE        &
        '0' ) when rising_edge(ULPI_CLOCK);
    
    SLOT_ADDR_OEn    <= '1';
    SLOT_ADDR_DIR    <= DEBUG_TRSTn and RMII_RX_ER and UART_RXD and SLOT_DOTCLK and IEC_RESET_I and CAS_SENSE and CAS_MOTOR when rising_edge(CLOCK_50);
    DEBUG_SPARE      <= '0';
    flash_sck_t      <= sys_reset; -- 0 when not in reset = enabled
    
    process(ctrl_clock)
        variable cnt : unsigned(23 downto 0) := (others => '0');
    begin
        if rising_edge(ctrl_clock) then
            cnt := cnt + 1;
            led_n(0) <= cnt(cnt'high);
        end if;
    end process;

    b_latency: block    
        signal rq, rq_d, ack    : std_logic;
        signal count            : natural range 0 to 255;
    begin
        rq <= '1' when cpu_mem_req.request='1' and cpu_mem_req.tag = X"20" and cpu_mem_req.read_writen = '1' else '0';
        rq_d <= rq when rising_edge(sys_clock);
        ack <= '1' when cpu_mem_resp.dack_tag = X"20" else '0';
    
        process(sys_clock)
        begin
            if rising_edge(sys_clock) then
                if rq = '1' and rq_d = '0' then
                    count <= 1;
                else
                    count <= count + 1;
                end if;
                if ack = '1' then
                    last_count <= to_unsigned(count, last_count'length);
                    count_sum  <= count_sum + count;
                end if;
            end if;
        end process;
    end block;
    
end architecture;
