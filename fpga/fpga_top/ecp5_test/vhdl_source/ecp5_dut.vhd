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
    SLOT_ADDR_OEn    : out std_logic;
    SLOT_ADDR_DIR    : out std_logic;
    SLOT_BUFFER_EN   : out std_logic;

    SLOT_PHI2        : inout std_logic;
    SLOT_DOTCLK      : inout std_logic;
    SLOT_RSTn        : inout std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : inout std_logic;
    SLOT_BA          : inout std_logic;
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
    SDRAM_CLK   : out   std_logic;
    SDRAM_CLKn  : out   std_logic;
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
    UART_RXD    : out   std_logic;
    
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
    CAS_MOTOR   : inout std_logic := '0';
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
    signal burstdet     : std_logic;
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
    signal cpu_reset    : std_logic;
    signal clock_24     : std_logic;
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal toggle_check : std_logic;
    signal toggle_reset : std_logic;
        
    -- Audio
    signal audio_pulse  : std_logic;
    signal left_in      : std_logic_vector(23 downto 0);
    signal right_in     : std_logic_vector(23 downto 0);
    signal left_out     : std_logic_vector(23 downto 0);
    signal right_out    : std_logic_vector(23 downto 0);

    -- memory controller interconnect
    signal ctrl_reset_pulse : std_logic;
    signal phase_sel        : std_logic_vector(1 downto 0);
    signal phase_dir        : std_logic;
    signal phase_step       : std_logic;
    signal phase_loadreg    : std_logic;
    signal is_idle          : std_logic;
    signal mem_req_cpu      : t_mem_req_32;
    signal mem_resp_cpu     : t_mem_resp_32;
    signal mem_req_io       : t_mem_req_32;
    signal mem_resp_io      : t_mem_resp_32;
    signal mem_req_dma      : t_mem_req_32;
    signal mem_resp_dma     : t_mem_resp_32;
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
    signal button_i    : std_logic_vector(2 downto 0);
    signal ulpi_reset_req   : std_logic;
    signal ulpi_reset_i     : std_logic;
    signal pio_i            : std_logic_vector(63 downto 0) := (others => '0');
    signal pio_o            : std_logic_vector(63 downto 0);
    signal pio_t            : std_logic_vector(63 downto 0);

    -- Ethernet
    signal rmii_tx_en_i     : std_logic;
    signal rmii_tx_data_i   : std_logic_vector(1 downto 0);
    signal rmii_crs_dv_i    : std_logic;
    signal rmii_rx_data_i   : std_logic_vector(1 downto 0);
    signal rmii_crs_dv_d    : std_logic;
    signal rmii_rx_data_d   : std_logic_vector(1 downto 0);

    signal eth_tx_data   : std_logic_vector(7 downto 0);
    signal eth_tx_last   : std_logic;
    signal eth_tx_valid  : std_logic;
    signal eth_tx_ready  : std_logic := '1';

    signal eth_rx_data   : std_logic_vector(7 downto 0);
    signal eth_rx_sof    : std_logic;
    signal eth_rx_eof    : std_logic;
    signal eth_rx_valid  : std_logic;

    -- USB
    signal ulpi_data_o      : std_logic_vector(7 downto 0);
    signal ulpi_data_t      : std_logic;
    signal ulpi_data_i      : std_logic_vector(7 downto 0);
    signal ulpi_nxt_i       : std_logic;
    signal ulpi_dir_i       : std_logic;

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
    signal io_req_dma       : t_io_req := c_io_req_init;
    signal io_resp_dma      : t_io_resp;
    signal io_req_pio       : t_io_req;
    signal io_resp_pio      : t_io_resp;

    -- Timing
    signal tick_16MHz       : std_logic;
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

    signal write_vector     : std_logic_vector(7 downto 0);
    signal console_data     : std_logic_vector(7 downto 0);
    signal console_valid    : std_logic;
    signal console_count    : unsigned(7 downto 0) := X"00";
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
        generic map (
            g_sample_width    => pio_i'length
        )
        port map (
            sys_clock         => sys_clock,
            sys_reset         => sys_reset,
            mem_req           => mem_req_jtag,
            mem_resp          => mem_resp_jtag,
            io_req            => io_req_jtag,
            io_resp           => io_resp_jtag,

            console_data      => console_data,
            console_valid     => console_valid,

            sample_vector     => pio_i,
            write_vector      => write_vector
        );

    -- snoop writes to the UART
    console_data <= io_req.data;
    console_valid <= io_req.write when io_req.address = 16 else '0';
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            if console_valid = '1' then
                console_count <= console_count + 1;
            end if;
        end if;
    end process;

    i_mem_arb: entity work.mem_bus_arbiter_pri_32
        generic map (
            g_registered => false,
            g_ports      => 4
        )
        port map (
            clock   => sys_clock,
            reset   => sys_reset,
            reqs(0) => mem_req_jtag,
            reqs(1) => mem_req_io,
            reqs(2) => mem_req_dma,
            reqs(3) => mem_req_cpu,
            resps(0)=> mem_resp_jtag,
            resps(1)=> mem_resp_io,
            resps(2)=> mem_resp_dma,
            resps(3)=> mem_resp_cpu,
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

    cpu_reset <= write_vector(7) when rising_edge(sys_clock);

    i_riscv: entity work.neorv32_wrapper
    generic map (
        g_jtag_debug=> g_jtag_debug,
        g_frequency => 50_000_000,
        g_tag       => X"20"
    )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        cpu_reset   => cpu_reset,
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
            reqs(0) => io_req_riscv,
            reqs(1) => io_req_jtag,
            resps(0)=> io_resp_riscv,
            resps(1)=> io_resp_jtag,
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
        g_range_hi => 10,
        g_ports    => 4
    )
    port map (
        clock      => sys_clock,
        req        => io_req_u2p,
        resp       => io_resp_u2p,
        reqs(0)    => io_req_new_io,
        reqs(1)    => io_req_ddr2,
        reqs(2)    => io_req_dma,
        reqs(3)    => io_req_pio,
        resps(0)   => io_resp_new_io,
        resps(1)   => io_resp_ddr2,
        resps(2)   => io_resp_dma,
        resps(3)   => io_resp_pio
    );

    i_basic_io: entity work.basic_io
        generic map (
            g_version     => X"77",
            g_numerator   => 8,
            g_denominator => 25,
            g_baud_rate   => 115_200
        )
        port map (
            sys_clock    => sys_clock,
            sys_reset    => sys_reset,
            io_req       => io_req_legacy,
            io_resp      => io_resp_legacy,
            io_irq       => io_irq,
            mem_req      => mem_req_io,
            mem_resp     => mem_resp_io,
            UART_TXD     => UART_TXD,
            UART_RXD     => '1',

            FLASH_CSn    => FLASH_CSn,
            FLASH_SCK    => flash_sck_o,
            FLASH_MOSI   => FLASH_MOSI,
            FLASH_MISO   => FLASH_MISO,

            ulpi_clock   => ulpi_clock,
            ulpi_reset   => ulpi_reset_i,
            ULPI_NXT     => ULPI_NXT,
            ULPI_STP     => ULPI_STP,
            ULPI_DIR     => ULPI_DIR,
            ULPI_DATA_I  => ulpi_data_i,
            ULPI_DATA_O  => ulpi_data_o,
            ULPI_DATA_T  => ulpi_data_t,

            eth_clock    => eth_clock,
            eth_reset    => eth_reset,
            eth_tx_data  => eth_tx_data,
            eth_tx_eof   => eth_tx_last,
            eth_tx_valid => eth_tx_valid,
            eth_tx_ready => eth_tx_ready,
            eth_rx_data  => eth_rx_data,
            eth_rx_sof   => eth_rx_sof,
            eth_rx_eof   => eth_rx_eof,
            eth_rx_valid => eth_rx_valid,

            button       => button_i
        );

    -- i_double_freq_bridge: entity work.memreq_halfrate
    -- port map(
    --     phase_out   => toggle_check,
    --     toggle_r_2x => toggle_reset,
    --     clock_1x    => sys_clock,
    --     clock_2x    => ctrl_clock,
    --     reset_1x    => sys_reset,
    --     inhibit_1x  => '0',
    --     mem_req_1x  => mem_req,
    --     mem_resp_1x => mem_resp,
    --     inhibit_2x  => open,
    --     mem_req_2x  => mem_req_2x,
    --     mem_resp_2x => mem_resp_2x
    -- );

    i_mem_bridge: entity work.mem_bus_bridge
        port map (
            a_clock    => sys_clock,
            a_reset    => sys_reset,
            a_mem_req  => mem_req,
            a_mem_resp => mem_resp,
            b_clock    => ctrl_clock,
            b_reset    => ctrl_reset,
            b_mem_req  => mem_req_2x,
            b_mem_resp => mem_resp_2x
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
        SDRAM_TEST2       => burstdet,
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

    i_generic_pio: entity work.generic_pio
    port map (
        clock   => sys_clock,
        reset   => sys_reset,
        io_req  => io_req_pio,
        io_resp => io_resp_pio,
        pio_i   => pio_i,
        pio_o   => pio_o,
        pio_t   => pio_t
    );

    i2c_scl_i   <= I2C_SCL and I2C_SCL_18;
    i2c_sda_i   <= I2C_SDA and I2C_SDA_18;
    I2C_SCL     <= '0' when i2c_scl_o = '0' else 'Z';
    I2C_SDA     <= '0' when i2c_sda_o = '0' else 'Z';
    I2C_SCL_18  <= '0' when i2c_scl_o = '0' else 'Z';
    I2C_SDA_18  <= '0' when i2c_sda_o = '0' else 'Z';
    MDIO_DATA   <= '0' when mdio_o = '0' else 'Z';
    
    -- Audio Codec transceiver
    i2s: entity work.i2s_serializer
    port map (
        clock            => audio_clock,
        reset            => audio_reset,
        i2s_out          => AUDIO_SDO,
        i2s_in           => AUDIO_SDI,
        i2s_bclk         => AUDIO_BCLK,
        i2s_fs           => AUDIO_LRCLK,
        sample_pulse     => audio_pulse,
        
        left_sample_out  => left_in,
        right_sample_out => right_in,
        left_sample_in   => left_out,
        right_sample_in  => right_out );

    AUDIO_MCLK <= audio_clock;

    -- Audio DMA
    i_audio_dma: entity work.audio_dma
        port map (
            audio_clock => audio_clock,
            audio_reset => audio_reset,
            audio_pulse => audio_pulse,
            left_out    => left_out,
            right_out   => right_out,
            left_in     => left_in,
            right_in    => right_in,
            sys_clock   => sys_clock,
            sys_reset   => sys_reset,
            io_req      => io_req_dma,
            io_resp     => io_resp_dma,
            mem_req     => mem_req_dma,
            mem_resp    => mem_resp_dma
        );

    -- Ethernet Transceiver
    i_rmii: entity work.rmii_transceiver
    port map (
        clock           => eth_clock,
        reset           => eth_reset,
        rmii_crs_dv     => rmii_crs_dv_d, 
        rmii_rxd        => rmii_rx_data_d,
        rmii_tx_en      => rmii_tx_en_i,
        rmii_txd        => rmii_tx_data_i,
        
        eth_rx_data     => eth_rx_data,
        eth_rx_sof      => eth_rx_sof,
        eth_rx_eof      => eth_rx_eof,
        eth_rx_valid    => eth_rx_valid,

        eth_tx_data     => eth_tx_data,
        eth_tx_eof      => eth_tx_last,
        eth_tx_valid    => eth_tx_valid,
        eth_tx_ready    => eth_tx_ready,
        ten_meg_mode    => '0'   );

    -- I/O Delays
    i_delay_rmii_rxdv: DELAYG generic map (DEL_MODE => "SCLK_ZEROHOLD") port map (A => RMII_CRS_DV,     Z => rmii_crs_dv_i);
    i_delay_rmii_rxd0: DELAYG generic map (DEL_MODE => "SCLK_ZEROHOLD") port map (A => RMII_RX_DATA(0), Z => rmii_rx_data_i(0));
    i_delay_rmii_rxd1: DELAYG generic map (DEL_MODE => "SCLK_ZEROHOLD") port map (A => RMII_RX_DATA(1), Z => rmii_rx_data_i(1));

    process(eth_clock)
    begin
        if rising_edge(eth_clock) then
            rmii_crs_dv_d  <= rmii_crs_dv_i;
            rmii_rx_data_d <= rmii_rx_data_i;
            RMII_TX_EN     <= rmii_tx_en_i;
            RMII_TX_DATA   <= rmii_tx_data_i;
        end if;
    end process;

    flash_sck_t  <= sys_reset; -- 0 when not in reset = enabled
    u1: USRMCLK
    port map (
        USRMCLKI  => flash_sck_o,
        USRMCLKTS => flash_sck_t
    );

    button_i <= not BUTTON;

    LED_MOTORn <= not burstdet;--write_vector(0);
    LED_DISKn  <= not sys_reset; --write_vector(1);
    LED_CARTn  <= not mem_resp.rack; --sys_clock; -- write_vector(2);
    LED_SDACTn <= not mem_req.request; --write_vector(3);

    ULPI_RESET <= not sys_reset; --por_n;

    toggle <= not toggle when rising_edge(sys_clock);
    USB_DP      <= 'Z' when toggle='1' else '0';
    USB_DM      <= 'Z' when toggle='1' else '1';
    --USB_PULLUP  <= '1';

    USB_PULLUP    <= xor_reduce(    
        ETH_IRQn         &
--        RMII_REFCLK      &
--        RMII_CRS_DV      &
        RMII_RX_ER       &
--        RMII_RX_DATA     &
--        UART_RXD         &
--        FLASH_MISO       &
--        ULPI_NXT         &
--        ULPI_DIR         &
--        ULPI_DATA        &
        '0' ) when rising_edge(ULPI_CLOCK);
    
    SLOT_ADDR_OEn    <= '1';
    SLOT_ADDR_DIR    <= FLASH_MISO and DEBUG_TRSTn and RMII_RX_ER when rising_edge(CLOCK_50);
    DEBUG_SPARE      <= '0';

    process(ctrl_clock)
        variable cnt : unsigned(23 downto 0) := (others => '0');
    begin
        if rising_edge(ctrl_clock) then
            cnt := cnt + 1;
            led_n(0) <= cnt(cnt'high);
        end if;
    end process;

    UART_RXD <= RMII_REFCLK;

    ULPI_DATA <= ulpi_data_o when ulpi_data_t = '1' else "ZZZZZZZZ";
    r: for i in ULPI_DATA'range generate
        i_delay: DELAYG generic map (DEL_MODE => "SCLK_ZEROHOLD") port map (A => ULPI_DATA(i), Z => ulpi_data_i(i));
        --i_delay: DELAYG generic map (DEL_VALUE => "DELAY5") port map (A => ULPI_DATA(i), Z => ulpi_data_delayed(i));
    end generate;
    i_delay_ulpi_nxt: DELAYG generic map (DEL_MODE => "SCLK_ZEROHOLD") port map (A => ULPI_NXT, Z => ulpi_nxt_i);
    i_delay_ulpi_dir: DELAYG generic map (DEL_MODE => "USER_DEFINED", DEL_VALUE => 5) port map (A => ULPI_DIR, Z => ulpi_dir_i);

    r_a: for i in SLOT_ADDR'range generate
        SLOT_ADDR(i) <= pio_o(i) when pio_t(i) = '1' else 'Z';
    end generate;
    r_d: for i in SLOT_DATA'range generate
        SLOT_DATA(i) <= pio_o(i+16) when pio_t(i+16) = '1' else 'Z';
    end generate;

    SLOT_PHI2        <= pio_o(24) when pio_t(24) = '1' else 'Z';
    SLOT_DOTCLK      <= pio_o(25) when pio_t(25) = '1' else 'Z';
    SLOT_RSTn        <= pio_o(26) when pio_t(26) = '1' else 'Z';
    SLOT_RWn         <= pio_o(27) when pio_t(27) = '1' else 'Z';
    SLOT_BA          <= pio_o(28) when pio_t(28) = '1' else 'Z';
    SLOT_DMAn        <= pio_o(29) when pio_t(29) = '1' else 'Z';
    SLOT_EXROMn      <= pio_o(30) when pio_t(30) = '1' else 'Z';
    SLOT_GAMEn       <= pio_o(31) when pio_t(31) = '1' else 'Z';
    SLOT_ROMHn       <= pio_o(32) when pio_t(32) = '1' else 'Z';
    SLOT_ROMLn       <= pio_o(33) when pio_t(33) = '1' else 'Z';
    SLOT_IO1n        <= pio_o(34) when pio_t(34) = '1' else 'Z';
    SLOT_IO2n        <= pio_o(35) when pio_t(35) = '1' else 'Z';
    SLOT_IRQn        <= pio_o(36) when pio_t(36) = '1' else 'Z';
    SLOT_NMIn        <= pio_o(37) when pio_t(37) = '1' else 'Z';
    SLOT_DRV_RST     <= pio_o(38);
    CAS_MOTOR        <= pio_o(40) when pio_t(40) = '1' else 'Z';
    CAS_SENSE        <= pio_o(41) when pio_t(41) = '1' else 'Z';
    CAS_READ         <= pio_o(42) when pio_t(42) = '1' else 'Z';
    CAS_WRITE        <= pio_o(43) when pio_t(43) = '1' else 'Z';
    SLOT_BUFFER_EN   <= pio_o(47);
    IEC_ATN_O        <= pio_o(48);
    IEC_DATA_O       <= pio_o(49);
    IEC_CLOCK_O      <= pio_o(50);
    IEC_RESET_O      <= pio_o(51);
    IEC_SRQ_O        <= pio_o(52);

    pio_i(15 downto 0) <= SLOT_ADDR;
    pio_i(23 downto 16) <= SLOT_DATA;
    pio_i(24)        <= not SLOT_PHI2;
    pio_i(25)        <= SLOT_DOTCLK;
    pio_i(26)        <= SLOT_RSTn;
    pio_i(27)        <= SLOT_RWn;
    pio_i(28)        <= SLOT_BA;
    pio_i(29)        <= SLOT_DMAn;
    pio_i(30)        <= SLOT_EXROMn;
    pio_i(31)        <= SLOT_GAMEn;
    pio_i(32)        <= SLOT_ROMHn;
    pio_i(33)        <= SLOT_ROMLn;
    pio_i(34)        <= SLOT_IO1n;
    pio_i(35)        <= SLOT_IO2n;
    pio_i(36)        <= SLOT_IRQn;
    pio_i(37)        <= SLOT_NMIn;
    pio_i(39)        <= SLOT_VCC;
    pio_i(40)        <= CAS_MOTOR;
    pio_i(41)        <= CAS_SENSE;
    pio_i(42)        <= CAS_READ;
    pio_i(43)        <= CAS_WRITE;
    pio_i(48)        <= IEC_ATN_I;
    pio_i(49)        <= IEC_DATA_I;
    pio_i(50)        <= IEC_CLOCK_I;
    pio_i(51)        <= IEC_RESET_I;
    pio_i(52)        <= IEC_SRQ_I;

end architecture;
