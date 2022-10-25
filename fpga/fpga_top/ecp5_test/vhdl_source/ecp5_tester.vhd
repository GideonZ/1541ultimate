-------------------------------------------------------------------------------
-- Title      : u2p_riscv
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Toplevel based on the RiscV CPU core.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity div is
generic (
    div: natural := 5
);
port (
    clock : in  std_logic;
    sig   : inout std_logic 
);
end entity;

architecture divider of div is
    signal counter : natural range 0 to div-1 := 0;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if counter = div-1 then
                counter <= 0;
                sig <= '1';
            elsif counter = (div/2)-1 then
                sig <= '0';
                counter <= counter + 1;
            else
                sig <= 'Z';
                counter <= counter + 1;
            end if;
        end if;
    end process;
end architecture;

-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
        
library ECP5U;
use ECP5U.components.all;

entity ecp5_tester is
port (
    -- Oscillator
    CLOCK_25         : in    std_logic := '0';
    
    -- slot side
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
    
    -- memory
    SDRAM_A     : out   std_logic_vector(10 downto 0); -- DRAM A
    SDRAM_BA    : out   std_logic_vector(1 downto 0) := (others => '0');
    SDRAM_DQ    : inout std_logic_vector(31 downto 0);
    --SDRAM_CSn   : out   std_logic; -- GND!
    SDRAM_RASn  : out   std_logic;
    SDRAM_CASn  : out   std_logic;
    SDRAM_WEn   : out   std_logic;
    -- SDRAM_CKE   : out   std_logic; -- VCC
    SDRAM_CLK   : out   std_logic;
     
    -- LEDs
    LED_GREENn  : out   std_logic;
    LED_ORANGEn : out   std_logic;
    LED_YELLOWn : out   std_logic;
    LED_MODULE  : out   std_logic;
    
    -- Ethernet RGMII
    PHYS_MDC    : out   std_logic;    
    PHYS_MDIO   : inout std_logic; 
    PHYS_RESET  : out   std_logic;  

    PHY0_GTXCLK : out   std_logic;   
    PHY0_TXD    : out   std_logic_vector(3 downto 0);
    PHY0_TX_EN  : out   std_logic;  
    PHY0_RXC    : in    std_logic;
    PHY0_RXD    : in    std_logic_vector(3 downto 0);
    PHY0_RX_DV  : in    std_logic;

    PHY1_GTXCLK : out   std_logic;   
    PHY1_TXD    : out   std_logic_vector(3 downto 0);
    PHY1_TX_EN  : out   std_logic;  
    PHY1_RXC    : in    std_logic;
    PHY1_RXD    : in    std_logic_vector(3 downto 0);
    PHY1_RX_DV  : in    std_logic;  

    -- Debug UART
    UART_TXD    : out   std_logic;
    UART_RXD    : in    std_logic;
    
    -- Flash Interface
    FLASH_CSn   : out   std_logic;
    FLASH_MOSI  : out   std_logic;
    FLASH_MISO  : in    std_logic;

    -- I2C Interface for ADC
    DUT_SDA     : inout std_logic := 'Z';
    DUT_SCL     : inout std_logic := 'Z';

    -- Other DUT pins
    DUT_TXD     : in  std_logic;
    DUT_RXD     : in  std_logic; -- used to measure clocks!
    DUT_SPK_A   : in  std_logic;
    DUT_SPK_B   : in  std_logic;
    DUT_CARTPWR : out std_logic;
    DUT_USBPWR  : out std_logic;
    DUT_REFCLK  : in  std_logic;

    -- Cassette Interface
    DUT_MOTOR   : inout std_logic;
    DUT_SENSE   : inout std_logic;
    DUT_READ    : inout std_logic;
    DUT_WRITE   : inout std_logic );

end entity;


architecture rtl of ecp5_tester is

    signal flash_sck_o  : std_logic;
    signal flash_sck_t  : std_logic;

    signal start_clock  : std_logic;
    signal start_reset  : std_logic;
    signal toggle       : std_logic;
    signal eth_clock    : std_logic;
    signal eth_reset    : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal cpu_reset    : std_logic;
        
    -- memory controller interconnect
    signal mem_req_cpu      : t_mem_req_32;
    signal mem_resp_cpu     : t_mem_resp_32;
    signal mem_req_dma      : t_mem_req_32;
    signal mem_resp_dma     : t_mem_resp_32;
    signal mem_req_jtag     : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_jtag    : t_mem_resp_32;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;

    signal i2c_sda_i   : std_logic;
    signal i2c_sda_o   : std_logic;
    signal i2c_scl_i   : std_logic;
    signal i2c_scl_o   : std_logic;
    signal mdio_o      : std_logic;
    signal mdio_i      : std_logic;
    signal mdc         : std_logic;

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
    signal io_req_pio       : t_io_req;
    signal io_resp_pio      : t_io_resp;
    signal io_req_dma       : t_io_req;
    signal io_resp_dma      : t_io_resp;
    signal io_req_meas      : t_io_req;
    signal io_resp_meas     : t_io_resp;

    -- Timing
    signal tick_16MHz       : std_logic;
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

    signal pio_i            : std_logic_vector(63 downto 0) := (others => '0');
    signal pio_o            : std_logic_vector(63 downto 0);
    signal pio_t            : std_logic_vector(63 downto 0);
    signal pio_t_reg        : std_logic_vector(63 downto 0);
    signal power_detect     : std_logic;

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
            g_ports      => 3
        )
        port map (
            clock   => sys_clock,
            reset   => sys_reset,
            reqs(0) => mem_req_jtag,
            reqs(1) => mem_req_dma,
            reqs(2) => mem_req_cpu,
            resps(0)=> mem_resp_jtag,
            resps(1)=> mem_resp_dma,
            resps(2)=> mem_resp_cpu,
            req     => mem_req,
            resp    => mem_resp
        );

    -- i_sdram: entity work.ext_mem_ctrl_sdr_32
    --     generic map (
    --         g_simulation      => false,
    --         A_Width           => 11
    --     )
    --     port map (
    --         clock      => sys_clock,
    --         reset      => sys_reset,
    --         inhibit    => '0',
    --         req        => mem_req,
    --         resp       => mem_resp,

    --         SDRAM_CLK  => SDRAM_CLK,
    --         --SDRAM_CKE  => SDRAM_CKE,
    --         --SDRAM_CSn  => SDRAM_CSn,
    --         SDRAM_RASn => SDRAM_RASn,
    --         SDRAM_CASn => SDRAM_CASn,
    --         SDRAM_WEn  => SDRAM_WEn,
    --         --SDRAM_DQM  => SDRAM_DQM,
    --         MEM_A      => SDRAM_A,
    --         MEM_BA     => SDRAM_BA,
    --         MEM_D      => SDRAM_DQ
    --     );
    SDRAM_CLK <= '0';
    SDRAM_DQ <= (others => 'Z');

    mem_bus_ram_inst: entity work.mem_bus_ram
        generic map (16) -- 64K
        port map (
            clock => sys_clock,
            req   => mem_req,
            resp  => mem_resp
        );

    i_startup: entity work.startup_colorlight
    port map (
        ref_clock     => CLOCK_25,
        start_clock   => start_clock,
        start_reset   => start_reset,
        sys_clock     => sys_clock,
        sys_reset     => sys_reset,
        eth_clock     => eth_clock,
        eth_reset     => eth_reset
    );

    cpu_reset <= write_vector(7) when rising_edge(sys_clock);

    i_riscv: entity work.neorv32_wrapper
    generic map (
        g_jtag_debug=> false,
        g_frequency => 50_000_000,
        g_tag       => X"20"
    )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        cpu_reset   => cpu_reset,
        jtag_trst_i => '1', -- DEBUG_TRSTn,
        jtag_tck_i  => '0',
        jtag_tdi_i  => '0',
        jtag_tdo_o  => open,
        jtag_tms_i  => '0',
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
        g_range_hi => 9,
        g_ports    => 4
    )
    port map (
        clock      => sys_clock,
        req        => io_req_u2p,
        resp       => io_resp_u2p,
        reqs(0)    => io_req_new_io,
        reqs(1)    => io_req_meas,
        reqs(2)    => io_req_dma,
        reqs(3)    => io_req_pio,
        resps(0)   => io_resp_new_io,
        resps(1)   => io_resp_meas,
        resps(2)   => io_resp_dma,
        resps(3)   => io_resp_pio
    );

    i_generic_pio: entity work.generic_pio
    port map (
        clock   => sys_clock,
        reset   => sys_reset,
        io_req  => io_req_pio,
        io_resp => io_resp_pio,
        pio_i   => pio_i,
        pio_o   => pio_o,
        pio_t   => pio_t_reg
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
        buttons     => "000",

        irq_out     => io_irq,
        
        busy_led    => open,

        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );

    i_u2p_io: entity work.u2p_io
    port map (
        clock      => sys_clock,
        reset      => sys_reset,
        io_req     => io_req_new_io,
        io_resp    => io_resp_new_io,
        mdc        => mdc,
        mdio_i     => mdio_i,
        mdio_o     => mdio_o,
        i2c_scl_i  => i2c_scl_i,
        i2c_scl_o  => i2c_scl_o,
        i2c_sda_i  => i2c_sda_i,
        i2c_sda_o  => i2c_sda_o,
        board_rev  => "10101",
        eth_irq_i  => '1',
        speaker_en => open,
        ulpi_reset => open,
        hub_reset_n=> open
    );

    b_audio: block
        signal class_d_sampled  : std_logic_vector(1 downto 0);
        signal class_d_data     : signed(17 downto 0);
        signal class_d_filtered : signed(17 downto 0);
        signal class_d_std      : std_logic_vector(23 downto 0) := (others => '0');
        constant c_divider      : natural := 50_024_000 / 48_000;
        signal div              : natural range 0 to c_divider-1;
        signal audio_pulse      : std_logic;
    begin
        process(sys_clock)
        begin
            if rising_edge(sys_clock) then
                audio_pulse <= '0';
                if sys_reset = '1' then
                    div <= c_divider-1;
                elsif div = 0 then
                    audio_pulse <= '1';
                    div <= c_divider-1;
                else
                    div <= div - 1;
                end if;

                class_d_sampled <= DUT_SPK_B & DUT_SPK_A;
                if class_d_sampled = "10" then
                    class_d_data <= to_signed(110000, 18);
                elsif class_d_sampled = "01" then
                    class_d_data <= to_signed(-110000, 18);
                else
                    class_d_data <= to_signed(0, 18);
                end if;
            end if;
        end process;
        class_d_std(23 downto 6) <= std_logic_vector(class_d_filtered);

        i_filt_left: entity work.lp_filter
        port map (
            clock     => sys_clock,
            reset     => sys_reset,
            signal_in => class_d_data,
            low_pass  => class_d_filtered );

        -- Audio DMA
        i_audio_dma: entity work.audio_dma
        generic map (
            g_mono      => true
        )
        port map (
            audio_clock => sys_clock,
            audio_reset => sys_reset,
            audio_pulse => audio_pulse,
            left_out    => open,
            right_out   => open,
            left_in     => class_d_std,
            right_in    => X"000000",
            sys_clock   => sys_clock,
            sys_reset   => sys_reset,
            io_req      => io_req_dma,
            io_resp     => io_resp_dma,
            mem_req     => mem_req_dma,
            mem_resp    => mem_resp_dma
        );

    end block;

    i2c_scl_i   <= DUT_SCL;
    i2c_sda_i   <= DUT_SDA;
    DUT_SCL     <= '0' when i2c_scl_o = '0' else 'Z';
    DUT_SDA     <= '0' when i2c_sda_o = '0' else 'Z';
    PHYS_MDC    <= mdc;
    mdio_i      <= PHYS_MDIO;
    PHYS_MDIO   <= '0' when mdio_o = '0' else 'Z';

    u1: USRMCLK
    port map (
        USRMCLKI => flash_sck_o,
        USRMCLKTS => flash_sck_t
    );

    toggle <= not toggle when rising_edge(sys_clock);

    LED_GREENn <= xor_reduce(    
        UART_RXD         &
        FLASH_MISO       &
        DUT_TXD          &
        DUT_REFCLK       &
        PHY0_RXC         &
        PHY0_RX_DV       &
        PHY0_RXD         &
        PHY1_RXC         &
        PHY1_RX_DV       &
        PHY1_RXD         &
        SDRAM_DQ         &
        '0' ) when rising_edge(sys_clock);
    
    PHYS_RESET <= not sys_reset;

    flash_sck_t      <= sys_reset; -- 0 when not in reset = enabled
    FLASH_CSn        <= '1';
    FLASH_MOSI       <= '1';

    process(sys_clock)
        variable cnt : unsigned(23 downto 0) := (others => '0');
    begin
        if rising_edge(sys_clock) then
            cnt := cnt + 1;
            LED_MODULE <= cnt(cnt'high);
        end if;
    end process;

    --DUT_RXD     <= 'Z';
    DUT_CARTPWR <= write_vector(4);
    DUT_USBPWR  <= write_vector(5);

    LED_ORANGEn <= write_vector(6); --'1';
    LED_YELLOWn <= write_vector(7);
    
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            if write_vector(4) = '0' and write_vector(5) = '0' then
                power_detect <= '0';
            elsif DUT_TXD = '1' then
                power_detect <= '1';
            end if;
        end if;
    end process;
    pio_t <= pio_t_reg when power_detect = '1' else (others => '0');

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
    DUT_MOTOR        <= pio_o(40) when pio_t(40) = '1' else 'Z';
    DUT_SENSE        <= pio_o(41) when pio_t(41) = '1' else 'Z';
    DUT_READ         <= pio_o(42) when pio_t(42) = '1' else 'Z';
    DUT_WRITE        <= pio_o(43) when pio_t(43) = '1' else 'Z';

    pio_i(15 downto 0) <= SLOT_ADDR;
    pio_i(23 downto 16) <= SLOT_DATA;
    pio_i(24)        <= SLOT_PHI2;
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
    pio_i(40)        <= DUT_MOTOR;
    pio_i(41)        <= DUT_SENSE;
    pio_i(42)        <= DUT_READ;
    pio_i(43)        <= DUT_WRITE;

    i_clock_measure: entity work.clock_measure
    port map (
        sys_clock  => sys_clock,
        sys_reset  => sys_reset,
        io_req     => io_req_meas,
        io_resp    => io_resp_meas,
        meas_clock => DUT_RXD
    );    

end architecture;
