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

entity ecp5_dut is
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
    UNUSED_G12  : out   std_logic := '0';
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
    FLASH_CSn   : out   std_logic;
    FLASH_MOSI  : out   std_logic;
    FLASH_MISO  : in    std_logic;

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


architecture rtl of ecp5_dut is

    signal flash_sck_o  : std_logic;
    signal flash_sck_t  : std_logic;
    signal led_n        : std_logic_vector(0 to 3);
    signal busy_led     : std_logic;

    signal start_clock  : std_logic;
    signal start_reset  : std_logic;
    signal half_clock   : std_logic;
    signal mem_clock    : std_logic;
    signal mem_ready    : std_logic;
    signal ctrl_clock   : std_logic;
    signal ctrl_reset   : std_logic;
    signal toggle       : std_logic;
    signal eth_clock    : std_logic;
    signal eth_reset    : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal clock_24     : std_logic;
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal toggle_check : std_logic;
    signal toggle_reset : std_logic;
        
    -- memory controller interconnect
    signal ctrl_reset_pulse : std_logic;
    signal phase_sel        : std_logic_vector(1 downto 0);
    signal phase_dir        : std_logic;
    signal phase_step       : std_logic;
    signal phase_loadreg    : std_logic;
    signal is_idle          : std_logic;
    signal mem_req_cpu      : t_mem_req_32;
    signal mem_resp_cpu     : t_mem_resp_32;
    signal mem_req_jtag     : t_mem_req_32;
    signal mem_resp_jtag    : t_mem_resp_32;
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
    signal ulpi_reset_req   : std_logic;
    signal ulpi_reset_i     : std_logic;

    -- io buses
    signal io_irq           : std_logic;
    signal io_req_jtag      : t_io_req;
    signal io_resp_jtag     : t_io_resp;
    signal io_req_riscv     : t_io_req;
    signal io_resp_riscv    : t_io_resp;
    signal io_req           : t_io_req;
    signal io_resp          : t_io_resp;
    signal io_req_legacy    : t_io_req;
    signal io_resp_legacy   : t_io_resp;
    signal io_req_u2p       : t_io_req;
    signal io_resp_u2p      : t_io_resp;
    signal io_req_new_io    : t_io_req;
    signal io_resp_new_io   : t_io_resp;
    signal io_req_ddr2      : t_io_req;
    signal io_resp_ddr2     : t_io_resp;

    -- Timing
    signal tick_16MHz       : std_logic;
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

    signal write_vector     : std_logic_vector(7 downto 0);

    function xor_reduce(a : std_logic_vector) return std_logic is
        variable r : std_logic := '0';
    begin
        for i in a'range loop
            r := r xor a(i);
        end loop;
        return r;
    end function;
begin
    i_jtag: entity work.jtag_client_lattice
        port map (
            sys_clock         => sys_clock,
            sys_reset         => sys_reset,
            mem_req           => mem_req_jtag,
            mem_resp          => mem_resp_jtag,
            io_req            => io_req_jtag,
            io_resp           => io_resp_jtag,

            clock_1           => start_clock,
            clock_2           => RMII_REFCLK,
            --sample_vector     => sample_vector,
            write_vector      => write_vector
        );

    i_mem_arb: entity work.mem_bus_arbiter_pri_32
        generic map (
            g_registered => false,
            g_ports      => 2
        )
        port map (
            clock   => sys_clock,
            reset   => sys_reset,
            reqs(0) => mem_req_jtag,
            reqs(1) => mem_req_cpu,
            resps(0)=> mem_resp_jtag,
            resps(1)=> mem_resp_cpu,
            req     => mem_req,
            resp    => mem_resp
        );

    ctrl_clock  <= half_clock;
    HUB_CLOCK   <= clock_24;
    ULPI_REFCLK <= clock_24;

    i_ulpi_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => ulpi_clock,
        input       => ulpi_reset_req,
        input_c     => ulpi_reset_i  );

    i_startup: entity work.startup
    port map (
        ref_clock     => RMII_REFCLK,
        phase_sel     => phase_sel,
        phase_dir     => phase_dir,
        phase_step    => phase_step,
        phase_loadreg => phase_loadreg,
        start_clock   => start_clock,
        start_reset   => start_reset,
        mem_clock     => mem_clock,
        mem_ready     => mem_ready,
        ctrl_clock    => ctrl_clock,
        ctrl_reset    => ctrl_reset,
        restart       => ctrl_reset_pulse,
        button        => '0',
        sys_clock     => sys_clock,
        sys_reset     => sys_reset,
        audio_clock   => audio_clock,
        audio_reset   => audio_reset,
        eth_clock     => eth_clock,
        eth_reset     => eth_reset,
        clock_24      => clock_24
    );

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
        mem_req     => mem_req_cpu,
        mem_resp    => mem_resp_cpu
    );
    
    i_io_bus_arbiter: entity work.io_bus_arbiter_pri
        generic map (
            g_ports => 2
        )
        port map (
            clock   => sys_clock,
            reset   => sys_reset,
            reqs(0) => io_req_jtag,
            reqs(1) => io_req_riscv,
            resps(0)=> io_resp_jtag,
            resps(1)=> io_resp_riscv,
            req     => io_req,
            resp    => io_resp
        );

    i_u2p_io_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 20,
        g_range_hi => 20,
        g_ports    => 2
    )
    port map(
        clock      => sys_clock,
        req        => io_req,
        resp       => io_resp,
        reqs(0)    => io_req_legacy,
        reqs(1)    => io_req_u2p,
        resps(0)   => io_resp_legacy,
        resps(1)   => io_resp_u2p
    );

    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 8,
        g_range_hi => 9,
        g_ports    => 2
    )
    port map (
        clock      => sys_clock,
        req        => io_req_u2p,
        resp       => io_resp_u2p,
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
        
        io_req      => io_req_legacy,
        io_resp     => io_resp_legacy,
    
        tick_4MHz   => tick_4MHz,
        tick_1us    => tick_1MHz,
        tick_1ms    => tick_1kHz,
        buttons     => button_i,

        irq_out     => io_irq,
        
        busy_led    => busy_led,

        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );

    i_double_freq_bridge: entity work.memreq_halfrate
    port map(
        phase_out   => toggle_check,
        toggle_r_2x => toggle_reset,
        clock_1x    => sys_clock,
        clock_2x    => ctrl_clock,
        reset_1x    => sys_reset,
        inhibit_1x  => '0',
        mem_req_1x  => mem_req,
        mem_resp_1x => mem_resp,
        inhibit_2x  => open,
        mem_req_2x  => mem_req_2x,
        mem_resp_2x => mem_resp_2x
    );

    i_memctrl: entity work.ddr2_ctrl
    port map (
        start_clock       => start_clock,
        start_reset       => start_reset,
        start_ready       => mem_ready,
        mem_clock         => mem_clock,
        mem_clock_half    => half_clock,
        
        toggle_reset      => toggle_reset,
        toggle_check      => toggle_check,

        sys_clock         => sys_clock,
        ctrl_clock        => ctrl_clock,
        ctrl_reset        => ctrl_reset,
        reset_out         => ctrl_reset_pulse,

        inhibit           => '0',
        req               => mem_req_2x,
        resp              => mem_resp_2x,
        
        io_clock          => sys_clock,
        io_reset          => sys_reset,
        io_req            => io_req_ddr2,
        io_resp           => io_resp_ddr2,

        phase_sel         => phase_sel, 
        phase_dir         => phase_dir, 
        phase_step        => phase_step, 
        phase_loadreg     => phase_loadreg, 

        SDRAM_TEST1       => UNUSED_G12,
        SDRAM_TEST2       => open,
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
        ulpi_reset => ulpi_reset_req,
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

    LED_MOTORn <= not write_vector(0);
    LED_DISKn  <= not write_vector(1);
    LED_CARTn  <= not write_vector(2);
    LED_SDACTn <= not write_vector(3);

    ULPI_RESET <= not sys_reset; --por_n;

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
    SLOT_ADDR_DIR    <= FLASH_MISO and DEBUG_TRSTn and RMII_RX_ER and UART_RXD and SLOT_DOTCLK and IEC_RESET_I and CAS_SENSE and CAS_MOTOR when rising_edge(CLOCK_50);
    DEBUG_SPARE      <= '0';
    flash_sck_t      <= sys_reset; -- 0 when not in reset = enabled
    FLASH_CSn        <= '1';
    FLASH_MOSI       <= '1';

    process(ctrl_clock)
        variable cnt : unsigned(23 downto 0) := (others => '0');
    begin
        if rising_edge(ctrl_clock) then
            cnt := cnt + 1;
            led_n(0) <= cnt(cnt'high);
        end if;
    end process;

    SLOT_ADDR <= X"FFFF" when write_vector = X"BB" else (others => 'Z');
    SLOT_DATA <= X"FF" when write_vector = X"BB" else (others => 'Z');
    SLOT_IRQn <= '1'  when write_vector = X"BB" else 'Z';
    SLOT_NMIn <= '1'  when write_vector = X"BB" else 'Z';
    SLOT_BUFFER_EN <= '0';
    SLOT_DMAn <= '1';
    SLOT_GAMEn <= '1';
    SLOT_EXROMn <= '1';
end architecture;
