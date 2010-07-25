
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity ultimate_1541_250e is
generic (
	g_version		: unsigned(7 downto 0) := X"13";
    g_simulation    : boolean := false;
    g_clock_freq    : natural := 50_000_000;
    g_baud_rate     : natural := 115_200;
    g_timer_rate    : natural := 200_000;
    g_uart          : boolean := true;
    g_drive_1541    : boolean := true; --
    g_hardware_gcr  : boolean := true;
    g_ram_expansion : boolean := true; --
    g_hardware_iec  : boolean := true; --
    g_iec_prog_tim  : boolean := false;
    g_c2n_streamer  : boolean := false;
    g_cartridge     : boolean := true; --
    g_drive_sound   : boolean := false );
port (
    CLOCK       : in    std_logic;
    
    -- slot side
    PHI2        : in    std_logic;
    DOTCLK      : in    std_logic;
    RSTn        : inout std_logic;

    BUFFER_ENn  : out   std_logic;

    SLOT_ADDR   : inout std_logic_vector(15 downto 0);
    SLOT_DATA   : inout std_logic_vector(7 downto 0);
    RWn         : inout std_logic;
    BA          : in    std_logic;
    DMAn        : out   std_logic;
    
    EXROMn      : inout std_logic;
    GAMEn       : inout std_logic;
    
    ROMHn       : in    std_logic;
    ROMLn       : in    std_logic;
    IO1n        : in    std_logic;
    IO2n        : in    std_logic;

    IRQn        : inout std_logic;
    NMIn        : inout std_logic;
    
    -- local bus side
    LB_ADDR     : out   std_logic_vector(21 downto 0); -- DRAM A 14...0
    LB_DATA     : inout std_logic_vector(7 downto 0);
    
    FLASH_CSn   : out   std_logic;
    SRAM_CSn    : out   std_logic;
    
    MEM_WEn     : out   std_logic;
    MEM_OEn     : out   std_logic;

    SDRAM_CSn   : out   std_logic;
    SDRAM_RASn  : out   std_logic;
    SDRAM_CASn  : out   std_logic;
    SDRAM_WEn   : out   std_logic;
    SDRAM_DQM   : out   std_logic;
	SDRAM_CKE	: out   std_logic;
	SDRAM_CLK	: out   std_logic;
	 
    -- PWM outputs (for audio)
    PWM_OUT     : out   std_logic_vector(1 downto 0) := "11";

    -- IEC bus
    IEC_ATN     : inout std_logic;
    IEC_DATA    : inout std_logic;
    IEC_CLOCK   : inout std_logic;
    IEC_RESET   : in    std_logic;
    IEC_SRQ_IN  : inout std_logic;
    
    DISK_ACTn   : out   std_logic; -- activity LED
	CART_LEDn	: out   std_logic;
	SDACT_LEDn	: out   std_logic;
    MOTOR_LEDn  : out   std_logic;
	
	-- Debug UART
	UART_TXD	: out   std_logic;
	UART_RXD	: in    std_logic;
	
    -- SD Card Interface
    SD_SSn      : out   std_logic;
    SD_CLK      : out   std_logic;
    SD_MOSI     : out   std_logic;
    SD_MISO     : in    std_logic;
    SD_CARDDETn : in    std_logic;
    
    -- Cassette Interface
    CAS_MOTOR   : in  std_logic := '0';
    CAS_SENSE   : out std_logic := 'Z';
    CAS_READ    : out std_logic := 'Z';
    CAS_WRITE   : in  std_logic := '0';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

--    attribute iob : string;
--    attribute iob of CAS_SENSE : signal is "false";
	
end ultimate_1541_250e;


architecture structural of ultimate_1541_250e is

    attribute IFD_DELAY_VALUE : string;
    attribute IFD_DELAY_VALUE of LB_DATA: signal is "0";

	signal reset_in		: std_logic;
	signal dcm_lock		: std_logic;
	signal sys_clock	: std_logic;
	signal sys_reset	: std_logic;
    signal sys_clock_2x : std_logic;
    signal sys_shifted  : std_logic;
	signal drv_clock_en	: std_logic;
	signal cpu_clock_en : std_logic;

	-- Memory interface
    signal memctrl_inhibit  : std_logic;
    signal mem_req_cpu      : t_mem_req := c_mem_req_init;
    signal mem_resp_cpu     : t_mem_resp;
    signal mem_req_1541     : t_mem_req := c_mem_req_init;
    signal mem_resp_1541    : t_mem_resp;
    signal mem_req_cart     : t_mem_req := c_mem_req_init;
    signal mem_resp_cart    : t_mem_resp;
    signal mem_req          : t_mem_req := c_mem_req_init;
    signal mem_resp         : t_mem_resp;
    
    -- IO Bus
    signal io_req           : t_io_req;
    signal io_resp          : t_io_resp := c_io_resp_init;
    signal io_req_1541      : t_io_req;
    signal io_resp_1541     : t_io_resp := c_io_resp_init;
    signal io_req_itu       : t_io_req;
    signal io_resp_itu      : t_io_resp := c_io_resp_init;
    signal io_req_cart      : t_io_req;
    signal io_resp_cart     : t_io_resp := c_io_resp_init;
    signal io_req_io        : t_io_req;
    signal io_resp_io       : t_io_resp := c_io_resp_init;
    signal io_req_sd        : t_io_req;
    signal io_resp_sd       : t_io_resp := c_io_resp_init;
    signal io_req_rtc       : t_io_req;
    signal io_resp_rtc      : t_io_resp := c_io_resp_init;
    signal io_req_rtc_tmr   : t_io_req;
    signal io_resp_rtc_tmr  : t_io_resp := c_io_resp_init;
    signal io_req_gcr_dec   : t_io_req;
    signal io_resp_gcr_dec  : t_io_resp := c_io_resp_init;
    signal io_req_flash     : t_io_req;
    signal io_resp_flash    : t_io_resp := c_io_resp_init;
    signal io_req_iec       : t_io_req;
    signal io_resp_iec      : t_io_resp := c_io_resp_init;
    signal io_req_usb       : t_io_req;
    signal io_resp_usb      : t_io_resp := c_io_resp_init;
    signal io_req_c2n       : t_io_req;
    signal io_resp_c2n      : t_io_resp := c_io_resp_init;
    
    -- Audio routing
    signal drive_sample     : unsigned(12 downto 0);
    signal sample_out       : std_logic_vector(13 downto 0);
    signal pwm              : std_logic;
    
    -- IEC signal routing
    signal iec_reset_o      : std_logic;

    signal atn_o, atn_i     : std_logic := '1';
    signal clk_o, clk_i     : std_logic := '1';
    signal data_o, data_i   : std_logic := '1';
    signal auto_o, srq_i    : std_logic := '1';
	
	signal iec_atn_o		: std_logic := '1';
	signal iec_clk_o	    : std_logic := '1';
	signal iec_data_o		: std_logic := '1';
    signal iec_srq_o        : std_logic := '1';
    
    -- miscellaneous interconnect
    signal irq_i            : std_logic := '0';
    signal c64_irq_n        : std_logic;
    signal c64_irq          : std_logic;
    signal button_i         : std_logic_vector(2 downto 0);
    signal phi2_tick        : std_logic;
    signal c64_stopped		: std_logic;
    signal c2n_sense        : std_logic := '0';
    signal c2n_out			: std_logic := 'Z';
	signal sd_busy			: std_logic;
	signal usb_busy			: std_logic;
	signal sd_act_stretched : std_logic;
	
    -- debug
    signal scale_cnt        : unsigned(11 downto 0) := X"000";
    attribute iob : string;
    attribute iob of scale_cnt : signal is "false";
begin
	reset_in <= '1' when BUTTON="000" else '0'; -- all 3 buttons pressed
    button_i <= not BUTTON;

    clkgen: entity work.s3e_clockgen
    port map (
        clk_50       => CLOCK,
        reset_in     => reset_in,

        dcm_lock     => dcm_lock,
        
        drive_stop   => c64_stopped,
        sys_clock    => sys_clock,    -- 50 MHz
        sys_reset    => sys_reset,
        sys_shifted  => sys_shifted,
--        sys_clock_2x => sys_clock_2x,
        drv_clock_en => drv_clock_en, -- 1/12.5 (4 MHz)
        cpu_clock_en => cpu_clock_en, -- 1/50 (1 MHz)

        eth_clock    => open,
        
        iec_reset_n  => IEC_RESET,
        iec_reset_o  => iec_reset_o );


    i_cpu: entity work.cpu_wrapper_zpu
    generic map (
        g_mem_tag         => X"20",
        g_internal_prg    => true,
        g_simulation      => g_simulation )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        irq_i       => irq_i,
        break_o     => open,
        
        -- memory interface
        mem_req     => mem_req_cpu,
        mem_resp    => mem_resp_cpu,
        
        io_req      => io_req,
        io_resp     => io_resp );


    i_itu: entity work.itu
    generic map (
		g_version	 => g_version,
        g_uart       => g_uart,
        g_frequency  => g_clock_freq,
        g_edge_init  => "00000001",
        g_edge_write => false,
        g_baudrate   => g_baud_rate,
        g_timer_rate => g_timer_rate)
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req_itu,
        io_resp     => io_resp_itu,
    
        irq_in(7)   => button_i(2),
        irq_in(6)   => button_i(1),
        irq_in(5)   => button_i(0),
        irq_in(4)   => c64_irq,
        irq_in(3)   => '0',
        irq_in(2)   => '0',
        irq_out     => irq_i,
        
        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );


    r_drive: if g_drive_1541 generate
        i_drive: entity work.c1541_drive
        generic map (
            g_audio         => g_drive_sound,
            g_audio_div     => (g_clock_freq / 22500),
            g_audio_base    => X"0F50000",
            g_ram_base      => X"0F60000" )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            cpu_clock_en    => cpu_clock_en,
            drv_clock_en    => drv_clock_en,
            
            -- slave port on io bus
            io_req          => io_req_1541,
            io_resp         => io_resp_1541,
                        
            -- master port on memory bus
            mem_req         => mem_req_1541,
            mem_resp        => mem_resp_1541,
            
            -- serial bus pins
            atn_o           => atn_o, -- open drain
            atn_i           => atn_i,
        
            clk_o           => clk_o, -- open drain
            clk_i           => clk_i,              
        
            data_o          => data_o, -- open drain
            data_i          => data_i,              
            
            -- LED
            act_led_n       => DISK_ACTn,
            motor_led_n     => MOTOR_LEDn,

            -- audio out
            audio_sample    => drive_sample );

        sample_out <= '1' & std_logic_vector(drive_sample); -- use upper half of the range only
        
        r_pwm: if g_drive_sound generate
            i_pwm0: entity work.sigma_delta_dac
            generic map (
                width   => 14 )
            port map (
                clk     => sys_clock,
                reset   => sys_reset,
                
                dac_in  => sample_out,
            
                dac_out => pwm );
        end generate;
    end generate;

	PWM_OUT     <= c2n_out & pwm;
        
    r_cart: if g_cartridge generate
        i_slot_srv: entity work.slot_server_v4
        generic map (
            g_tag_slot      => X"08",
            g_tag_reu       => X"10",
            g_ram_base_reu  => X"1000000", -- should be on 16M boundary, or should be limited in size
            g_rom_base_cart => X"0F80000", -- should be on a 512K boundary
            g_ram_base_cart => X"0F70000", -- should be on a 64K boundary
            g_control_read  => true,
            g_ram_expansion => g_ram_expansion )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            
            -- Cartridge pins
            RSTn            => RSTn,
            IRQn            => IRQn,
            NMIn            => NMIn,
            PHI2            => PHI2,
            IO1n            => IO1n,
            IO2n            => IO2n,
            DMAn            => DMAn,
            BA              => BA,
            ROMLn           => ROMLn,
            ROMHn           => ROMHn,
            GAMEn           => GAMEn,
            EXROMn          => EXROMn,
            RWn             => RWn,
            ADDRESS         => SLOT_ADDR,
            DATA            => SLOT_DATA,
        
            -- other hardware pins
            BUFFER_ENn      => BUFFER_ENn,
        
			buttons_n		=> BUTTON,
            cart_led_n      => CART_LEDn,
            
            -- timing output
			c64_stopped		=> c64_stopped,
            phi2_tick       => phi2_tick,

            -- master on memory bus
            memctrl_inhibit => memctrl_inhibit,
            mem_req         => mem_req_cart,
            mem_resp        => mem_resp_cart,
            
            -- slave on io bus
            io_req          => io_req_cart,
            io_resp         => io_resp_cart );
    end generate;

    i_split1: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 17,
        g_range_hi  => 19,
        g_ports     => 6 )
    port map (
        clock    => sys_clock,
        
        req      => io_req,
        resp     => io_resp,
        
        reqs(0)  => io_req_itu,  -- 4000000
        reqs(1)  => io_req_1541, -- 4020000
        reqs(2)  => io_req_cart, -- 4040000
        reqs(3)  => io_req_io,   -- 4060000
        reqs(4)  => io_req_usb,  -- 4080000
        reqs(5)  => io_req_c2n,  -- 40A0000

        resps(0) => io_resp_itu,
        resps(1) => io_resp_1541,
        resps(2) => io_resp_cart,
        resps(3) => io_resp_io,
        resps(4) => io_resp_usb,
        resps(5) => io_resp_c2n );

--    process(sys_clock)
--    begin
--        if rising_edge(clock) then
--            io_resp_usb
--        end if;
--    end process;

    i_split2: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 8,
        g_range_hi  => 10,
        g_ports     => 6 )
    port map (
        clock    => sys_clock,
        
        req      => io_req_io,
        resp     => io_resp_io,
        
        reqs(0)  => io_req_sd,      -- 4060000 
        reqs(1)  => io_req_rtc,     -- 4060100 
        reqs(2)  => io_req_flash,   -- 4060200 
        reqs(3)  => io_req_iec,     -- 4060300 
        reqs(4)  => io_req_rtc_tmr, -- 4060400
        reqs(5)  => io_req_gcr_dec, -- 4060500
        
        resps(0) => io_resp_sd,
        resps(1) => io_resp_rtc,
        resps(2) => io_resp_flash,
        resps(3) => io_resp_iec,
        resps(4) => io_resp_rtc_tmr,
        resps(5) => io_resp_gcr_dec );


    i_sd: entity work.spi_peripheral_io
    generic map (
        g_fixed_rate => false,
        g_init_rate  => 500,
        g_crc        => true )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req_sd,
        io_resp     => io_resp_sd,
            
		busy		=> sd_busy,
		
        SD_DETECTn  => SD_CARDDETn,
        SD_WRPROTn  => '1', --SD_WRPROTn,
        SPI_SSn     => SD_SSn,
        SPI_CLK     => SD_CLK,
        SPI_MOSI    => SD_MOSI,
        SPI_MISO    => SD_MISO );

	i_stretch: entity work.pulse_stretch
	generic map ( g_clock_freq / 200) -- 5 ms
	port map (
		clock		=> sys_clock,
		reset		=> sys_reset,
		pulse_in	=> sd_busy,
		pulse_out	=> sd_act_stretched );

	SDACT_LEDn <= not (sd_act_stretched or usb_busy);

    r_gcr_codec: if g_hardware_gcr generate
        i_gcr_codec: entity work.gcr_codec
        port map (
            clock       => sys_clock,    
            reset       => sys_reset,
            
            req         => io_req_gcr_dec,
            resp        => io_resp_gcr_dec );
    end generate;

    r_iec: if g_hardware_iec generate
        i_iec: entity work.iec_interface_io
        generic map (
            g_state_readback    => false,
            programmable_timing => g_iec_prog_tim )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            iec_atn_i       => atn_i,
            iec_atn_o       => iec_atn_o,
            iec_clk_i       => clk_i,
            iec_clk_o       => iec_clk_o,
            iec_data_i      => data_i,
            iec_data_o      => iec_data_o,
        
            io_req          => io_req_iec,
            io_resp         => io_resp_iec );
    end generate;

    r_c2n: if g_c2n_streamer generate
        i_iec: entity work.c2n_playback_io
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            req             => io_req_c2n,
            resp            => io_resp_c2n,

			c64_stopped		=> c64_stopped,
            phi2_tick       => phi2_tick,
            c2n_sense       => c2n_sense,
            c2n_motor       => CAS_MOTOR,
            c2n_out         => c2n_out );
    end generate;
    
	CAS_SENSE <= '0' when c2n_sense='1' else 'Z';
	CAS_READ  <= c2n_out;
	
    i_mem_arb: entity work.mem_bus_arbiter_pri
    generic map (
        g_ports      => 3,
        g_registered => false )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        reqs(0)     => mem_req_cart,
        reqs(1)     => mem_req_1541,
        reqs(2)     => mem_req_cpu,

        resps(0)    => mem_resp_cart,
        resps(1)    => mem_resp_1541,
        resps(2)    => mem_resp_cpu,
        
        req         => mem_req,
        resp        => mem_resp );        
    

	i_memctrl: entity work.ext_mem_ctrl_v4_250e
    generic map (
        g_simulation => g_simulation,
    	A_Width	     => 22 )
		
    port map (
        clock       => sys_clock,
        clk_shifted => sys_shifted,
        reset       => sys_reset,

        inhibit     => memctrl_inhibit,
        is_idle     => open, --memctrl_idle,
        
        req         => mem_req,
        resp        => mem_resp,
        
        FLASH_CSn   => FLASH_CSn,
        SRAM_CSn    => SRAM_CSn,
    
        MEM_WEn     => MEM_WEn,
        MEM_OEn     => MEM_OEn,

		SDRAM_CSn   => SDRAM_CSn,	
	    SDRAM_RASn  => SDRAM_RASn,
	    SDRAM_CASn  => SDRAM_CASn,
	    SDRAM_WEn   => SDRAM_WEn,
		SDRAM_CKE	=> SDRAM_CKE,
		SDRAM_CLK	=> SDRAM_CLK,

        MEM_A       => LB_ADDR,
        MEM_D       => LB_DATA );

    IEC_ATN    <= '0' when atn_o='0'  or iec_atn_o='0'  else 'Z'; -- open drain
    IEC_CLOCK  <= '0' when clk_o='0'  or iec_clk_o='0'  else 'Z'; -- open drain
    IEC_DATA   <= '0' when data_o='0' or iec_data_o='0' else 'Z'; -- open drain
    IEC_SRQ_IN <= '0' when               iec_srq_o='0'  else 'Z'; -- open drain
    
    filt1: entity work.spike_filter generic map (10) port map(sys_clock, IEC_ATN,    atn_i);
    filt2: entity work.spike_filter generic map (10) port map(sys_clock, IEC_CLOCK,  clk_i);
    filt3: entity work.spike_filter generic map (10) port map(sys_clock, IEC_DATA,   data_i);
    filt4: entity work.spike_filter generic map (10) port map(sys_clock, IEC_SRQ_IN, srq_i);
    filt5: entity work.spike_filter port map(sys_clock, IRQn, c64_irq_n);
    c64_irq <= not c64_irq_n;

    -- tie offs
    SDRAM_DQM  <= '0';

    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            scale_cnt <= scale_cnt + 1;
        end if;
    end process;

end structural;
