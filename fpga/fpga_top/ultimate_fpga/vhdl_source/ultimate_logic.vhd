
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity ultimate_logic is
generic (
	g_version		: unsigned(7 downto 0) := X"FF";
    g_simulation    : boolean := true;
    g_clock_freq    : natural := 50_000_000;
    g_baud_rate     : natural := 115_200;
    g_timer_rate    : natural := 200_000;
    g_fpga_type     : natural := 0;
    g_boot_rom      : boolean := false;
    g_video_overlay : boolean := false;
    g_icap          : boolean := false;
    g_uart          : boolean := false;
    g_drive_1541    : boolean := false;
    g_drive_1541_2  : boolean := false;
    g_hardware_gcr  : boolean := false;
    g_cartridge     : boolean := false;
    g_command_intf  : boolean := false;
    g_stereo_sid    : boolean := false;
    g_ram_expansion : boolean := false;
    g_extended_reu  : boolean := false;
    g_hardware_iec  : boolean := false;
    g_iec_prog_tim  : boolean := false;
    g_c2n_streamer  : boolean := false;
    g_c2n_recorder  : boolean := false;
    g_drive_sound   : boolean := false;
    g_rtc_chip      : boolean := false;
    g_rtc_timer     : boolean := false;
    g_usb_host      : boolean := false;
    g_spi_flash     : boolean := false;
    g_vic_copper    : boolean := false;
    g_sampler       : boolean := false;
    g_analyzer      : boolean := false );
port (
    -- globals
    sys_clock   : in    std_logic;
    sys_reset   : in    std_logic;

    ulpi_clock  : in    std_logic;
    ulpi_reset  : in    std_logic;

    -- slot side
    PHI2        : in    std_logic;
    DOTCLK      : in    std_logic;
    RSTn        : inout std_logic := '1';

    BUFFER_ENn  : out   std_logic := '1';

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
    mem_inhibit : out   std_logic;
    mem_req     : out   t_mem_req;
    mem_resp    : in    t_mem_resp;
    
    -- PWM outputs (for audio)
    PWM_OUT     : out   std_logic_vector(1 downto 0) := "11";

    -- IEC bus
    -- actual levels of the pins --
    iec_reset_i : in    std_logic;
    iec_atn_i   : in    std_logic;
    iec_data_i  : in    std_logic;
    iec_clock_i : in    std_logic;
    iec_srq_i   : in    std_logic;
    
    iec_reset_o : out   std_logic := '1';
    iec_atn_o   : out   std_logic;
    iec_data_o  : out   std_logic;
    iec_clock_o : out   std_logic;
    iec_srq_o   : out   std_logic;

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
    SD_DATA     : inout std_logic_vector(2 downto 1) := "ZZ";
    
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
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;
    ULPI_DIR    : in    std_logic;
    ULPI_DATA   : inout std_logic_vector(7 downto 0) := "ZZZZZZZZ";

    -- Cassette Interface
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Interface to other graphical output (Full HD of course and in 3D!) ;-)
    vid_clock   : in    std_logic := '0';
    vid_reset   : in    std_logic := '0';
    vid_h_count : in    unsigned(11 downto 0) := (others => '0');
    vid_v_count : in    unsigned(11 downto 0) := (others => '0');
    vid_active  : out   std_logic;
    vid_opaque  : out   std_logic;
    vid_data    : out   unsigned(3 downto 0);
    overlay_on  : out   std_logic;
    keyb_row    : in    std_logic_vector(7 downto 0) := (others => '0');
    keyb_col    : inout std_logic_vector(7 downto 0) := (others => '0');

    -- Buttons
    button      : in  std_logic_vector(2 downto 0);
    
    -- Simulation port
    sim_io_req  : in  t_io_req := c_io_req_init;
    sim_io_resp : out t_io_resp );
	
end ultimate_logic;


architecture logic of ultimate_logic is

    function to_std(b : boolean) return std_logic is
    begin
        if b then
            return '1';
        end if;
        return '0';
    end function;
    
    impure function create_capabilities return std_logic_vector is
        variable cap : std_logic_vector(31 downto 0) := (others => '0');
    begin
        cap(00) := to_std(g_uart);
        cap(01) := to_std(g_drive_1541);
        cap(02) := to_std(g_drive_1541_2);
        cap(03) := to_std(g_drive_sound);
        cap(04) := to_std(g_hardware_gcr);
        cap(05) := to_std(g_hardware_iec);
        cap(06) := to_std(g_iec_prog_tim);
        cap(07) := to_std(g_c2n_streamer);
        cap(08) := to_std(g_c2n_recorder);
        cap(09) := to_std(g_cartridge);
        cap(10) := to_std(g_ram_expansion);
        cap(11) := to_std(g_usb_host);
        cap(12) := to_std(g_rtc_chip);
        cap(13) := to_std(g_rtc_timer);
        cap(14) := to_std(g_spi_flash);
        cap(15) := to_std(g_icap);
        cap(16) := to_std(g_extended_reu);
        cap(17) := to_std(g_stereo_sid);
        cap(18) := to_std(g_command_intf);
        cap(19) := to_std(g_vic_copper);
        cap(20) := to_std(g_video_overlay);
        cap(21) := to_std(g_sampler);
        cap(22) := to_std(g_analyzer);
        cap(29 downto 28) := std_logic_vector(to_unsigned(g_fpga_type, 2));
        cap(30) := to_std(g_boot_rom);
        cap(31) := to_std(g_simulation);
        return cap;
    end function;

    constant c_capabilities      : std_logic_vector(31 downto 0) := create_capabilities;

    constant c_tag_1541_cpu_1    : std_logic_vector(7 downto 0) := X"01";
    constant c_tag_1541_floppy_1 : std_logic_vector(7 downto 0) := X"02";
    constant c_tag_1541_audio_1  : std_logic_vector(7 downto 0) := X"03";
    constant c_tag_1541_cpu_2    : std_logic_vector(7 downto 0) := X"04";
    constant c_tag_1541_floppy_2 : std_logic_vector(7 downto 0) := X"05";
    constant c_tag_1541_audio_2  : std_logic_vector(7 downto 0) := X"06";    
    constant c_tag_cpu           : std_logic_vector(7 downto 0) := X"07";
    constant c_tag_slot          : std_logic_vector(7 downto 0) := X"08";
    constant c_tag_reu           : std_logic_vector(7 downto 0) := X"09";

	-- Memory interface
    signal mem_req_cpu      : t_mem_req := c_mem_req_init;
    signal mem_resp_cpu     : t_mem_resp := c_mem_resp_init;
    signal mem_req_1541     : t_mem_req := c_mem_req_init;
    signal mem_resp_1541    : t_mem_resp := c_mem_resp_init;
    signal mem_req_1541_2   : t_mem_req := c_mem_req_init;
    signal mem_resp_1541_2  : t_mem_resp := c_mem_resp_init;
    signal mem_req_cart     : t_mem_req := c_mem_req_init;
    signal mem_resp_cart    : t_mem_resp := c_mem_resp_init;
    signal mem_req_debug    : t_mem_req := c_mem_req_init;
    signal mem_resp_debug   : t_mem_resp := c_mem_resp_init;

    -- IO Bus
    signal cpu_io_req       : t_io_req;
    signal cpu_io_resp      : t_io_resp := c_io_resp_init;
    signal io_req           : t_io_req;
    signal io_resp          : t_io_resp := c_io_resp_init;
    signal io_req_1541      : t_io_req;
    signal io_resp_1541     : t_io_resp := c_io_resp_init;
    signal io_req_1541_1    : t_io_req;
    signal io_resp_1541_1   : t_io_resp := c_io_resp_init;
    signal io_req_1541_2    : t_io_req;
    signal io_resp_1541_2   : t_io_resp := c_io_resp_init;
    signal io_req_itu       : t_io_req;
    signal io_resp_itu      : t_io_resp := c_io_resp_init;
    signal io_req_cart      : t_io_req;
    signal io_resp_cart     : t_io_resp := c_io_resp_init;
    signal io_req_io        : t_io_req;
    signal io_resp_io       : t_io_resp := c_io_resp_init;
    signal io_req_big_io    : t_io_req;
    signal io_resp_big_io   : t_io_resp := c_io_resp_init;
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
    signal io_req_c2n_rec   : t_io_req;
    signal io_resp_c2n_rec  : t_io_resp := c_io_resp_init;
    signal io_req_icap      : t_io_req;
    signal io_resp_icap     : t_io_resp := c_io_resp_init;
    signal io_req_aud_sel   : t_io_req;
    signal io_resp_aud_sel  : t_io_resp := c_io_resp_init;
    signal io_req_debug     : t_io_req;
    signal io_resp_debug    : t_io_resp := c_io_resp_init;

    -- Audio routing
    signal pwm              : std_logic;
    signal pwm_2            : std_logic := '0';
    signal drive_sample     : signed(12 downto 0);
    signal drive_sample_2   : signed(12 downto 0);
    
    -- IEC signal routing
    signal atn_o, atn_i     : std_logic := '1';
    signal clk_o, clk_i     : std_logic := '1';
    signal data_o, data_i   : std_logic := '1';
    signal srq_i            : std_logic := '1';
	
    signal atn_o_2          : std_logic := '1';
    signal clk_o_2          : std_logic := '1';
    signal data_o_2         : std_logic := '1';

	signal hw_atn_o		    : std_logic := '1';
	signal hw_clk_o	        : std_logic := '1';
	signal hw_data_o		: std_logic := '1';
    signal hw_srq_o         : std_logic := '1';
    
    -- miscellaneous interconnect
    signal irq_i            : std_logic := '0';
    signal c64_irq_n        : std_logic;
    signal c64_irq          : std_logic;
    signal phi2_tick        : std_logic;
    signal c64_stopped		: std_logic;
    signal c2n_sense        : std_logic := '0';
    signal c2n_sense_in     : std_logic := '0';
    signal c2n_out_r		: std_logic := '1';
    signal c2n_out_w		: std_logic := '1';
	signal sd_busy			: std_logic;
	signal usb_busy			: std_logic;
	signal sd_act_stretched : std_logic;
	signal error			: std_logic;
	signal act_led_n		: std_logic := '1';
	signal motor_led_n		: std_logic := '1';
	signal cart_led_n		: std_logic := '1';
	signal c2n_pull_sense   : std_logic := '0';
    signal freezer_state    : std_logic_vector(1 downto 0);
    signal dirty_led_1_n    : std_logic := '1';
    signal dirty_led_2_n    : std_logic := '1';
    signal sid_pwm_left     : std_logic;
    signal sid_pwm_right    : std_logic;
    signal samp_pwm_left    : std_logic;
    signal samp_pwm_right   : std_logic;
    signal trigger_1        : std_logic;
    signal trigger_2        : std_logic;
begin

    i_cpu: entity work.cpu_wrapper_zpu
    generic map (
        g_mem_tag         => c_tag_cpu,
        g_internal_prg    => true,
        g_boot_rom        => g_boot_rom,
        g_simulation      => g_simulation )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        irq_i       => irq_i,
        break_o     => open,
        error		=> error,
		
        -- memory interface
        mem_req     => mem_req_cpu,
        mem_resp    => mem_resp_cpu,
        
        io_req      => cpu_io_req,
        io_resp     => cpu_io_resp );

		
    i_io_arb: entity work.io_bus_arbiter_pri
    generic map (
        g_ports     => 2 )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        reqs(0)     => sim_io_req,
        reqs(1)     => cpu_io_req,
        
        resps(0)    => sim_io_resp,
        resps(1)    => cpu_io_resp,
        
        req         => io_req,
        resp        => io_resp );


    i_itu: entity work.itu
    generic map (
		g_version	    => g_version,
        g_capabilities  => c_capabilities,
        g_uart          => g_uart,
        g_frequency     => g_clock_freq,
        g_edge_init     => "00000001",
        g_edge_write    => false,
        g_baudrate      => g_baud_rate,
        g_timer_rate    => g_timer_rate)
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req_itu,
        io_resp     => io_resp_itu,
    
        irq_in(7)   => button(2),
        irq_in(6)   => button(1),
        irq_in(5)   => button(0),
        irq_in(4)   => c64_irq,
        irq_in(3)   => '0',
        irq_in(2)   => '0',
        irq_out     => irq_i,
        
        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );


    r_drive: if g_drive_1541 generate
    begin
        i_drive: entity work.c1541_drive
        generic map (
            g_cpu_tag       => c_tag_1541_cpu_1,
            g_floppy_tag    => c_tag_1541_floppy_1,
            g_audio_tag     => c_tag_1541_audio_1,
            g_audio         => g_drive_sound,
            g_audio_div     => (g_clock_freq / 22500),
            g_audio_base    => X"0EC0000",
            g_ram_base      => X"0EE0000" )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            drive_stop      => c64_stopped,
            
            -- slave port on io bus
            io_req          => io_req_1541_1,
            io_resp         => io_resp_1541_1,
                        
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
            
            iec_reset_n     => iec_reset_i,
            c64_reset_n     => RSTn,
            
            -- LED
            act_led_n       => act_led_n,
            motor_led_n     => motor_led_n,
            dirty_led_n     => dirty_led_1_n,

            -- audio out
            audio_sample    => drive_sample );

        r_pwm: if g_drive_sound generate
            i_pwm0: entity work.sigma_delta_dac --delta_sigma_2to5
            generic map (
                g_left_shift => 2,
                g_width => drive_sample'length )
            port map (
                clock   => sys_clock,
                reset   => sys_reset,
                
                dac_in  => drive_sample,
            
                dac_out => pwm );
        end generate;
    end generate;

    r_drive_2: if g_drive_1541_2 generate
    begin
        i_drive: entity work.c1541_drive
        generic map (
            g_cpu_tag       => c_tag_1541_cpu_2,
            g_floppy_tag    => c_tag_1541_floppy_2,
            g_audio_tag     => c_tag_1541_audio_2,
            g_audio         => g_drive_sound,
            g_audio_div     => (g_clock_freq / 22500),
            g_audio_base    => X"0EC0000",
            g_ram_base      => X"0ED0000" )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            drive_stop      => c64_stopped,
            
            -- slave port on io bus
            io_req          => io_req_1541_2,
            io_resp         => io_resp_1541_2,
                        
            -- master port on memory bus
            mem_req         => mem_req_1541_2,
            mem_resp        => mem_resp_1541_2,
            
            -- serial bus pins
            atn_o           => atn_o_2, -- open drain
            atn_i           => atn_i,
        
            clk_o           => clk_o_2, -- open drain
            clk_i           => clk_i,              
        
            data_o          => data_o_2, -- open drain
            data_i          => data_i,              
            
            iec_reset_n     => iec_reset_i,
            c64_reset_n     => RSTn,

            -- LED
            act_led_n       => open, --DISK_ACTn,
            motor_led_n     => open, --MOTOR_LEDn,
            dirty_led_n     => dirty_led_2_n,

            -- audio out
            audio_sample    => drive_sample_2 );

        r_pwm: if g_drive_sound generate
            i_pwm0: entity work.sigma_delta_dac --delta_sigma_2to5
            generic map (
                g_left_shift => 2,
                g_width => drive_sample_2'length )
            port map (
                clock   => sys_clock,
                reset   => sys_reset,
                
                dac_in  => drive_sample_2,
            
                dac_out => pwm_2 );
        end generate;
    end generate;

    r_cart: if g_cartridge generate
        i_slot_srv: entity work.slot_server_v4
        generic map (
            g_tag_slot      => c_tag_slot,
            g_tag_reu       => c_tag_reu,
            g_ram_base_reu  => X"1000000", -- should be on 16M boundary, or should be limited in size
            g_rom_base_cart => X"0F00000", -- should be on a 1M boundary
            g_ram_base_cart => X"0EF0000", -- should be on a 64K boundary
            g_control_read  => true,
            g_ram_expansion => g_ram_expansion,
            g_extended_reu  => g_extended_reu,
            g_command_intf  => g_command_intf,
            g_sampler       => g_sampler,
            g_implement_sid => g_stereo_sid,
            g_sid_voices    => 16,
            g_vic_copper    => g_vic_copper )
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
        
			buttons 		=> BUTTON,
            cart_led_n      => cart_led_n,
            
            -- audio
            sid_pwm_left    => sid_pwm_left,
            sid_pwm_right   => sid_pwm_right,
            samp_pwm_left   => samp_pwm_left,
            samp_pwm_right  => samp_pwm_right,

            -- debug
            freezer_state   => freezer_state,
            trigger_1       => trigger_1,
            trigger_2       => trigger_2,
            
            -- timing output
			c64_stopped		=> c64_stopped,
            phi2_tick       => phi2_tick,

            -- master on memory bus
            memctrl_inhibit => mem_inhibit,
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
        g_ports     => 8 )
    port map (
        clock    => sys_clock,
        
        req      => io_req,
        resp     => io_resp,
        
        reqs(0)  => io_req_itu,     -- 4000000 ( 16 ... 400000F)
        reqs(1)  => io_req_1541,    -- 4020000 (  8K... 4021FFF) & 4024000 for drive B 
        reqs(2)  => io_req_cart,    -- 4040000 (128K... 405FFFF)
        reqs(3)  => io_req_io,      -- 4060000 (  2K... 4060FFF)
        reqs(4)  => io_req_usb,     -- 4080000 (  8K... 4081FFF)
        reqs(5)  => io_req_c2n,     -- 40A0000 (  4K... 40A0FFF)
        reqs(6)  => io_req_c2n_rec, -- 40C0000 (  4K... 40C0FFF)
        reqs(7)  => io_req_big_io,  -- 40E0000 (128K... 40FFFFF)

        resps(0) => io_resp_itu,
        resps(1) => io_resp_1541,
        resps(2) => io_resp_cart,
        resps(3) => io_resp_io,
        resps(4) => io_resp_usb,
        resps(5) => io_resp_c2n,
        resps(6) => io_resp_c2n_rec,
        resps(7) => io_resp_big_io );


    i_split2: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 14,
        g_range_hi  => 15,
        g_ports     => 3 )
    port map (
        clock    => sys_clock,
        
        req      => io_req_1541,
        resp     => io_resp_1541,
        
        reqs(0)  => io_req_1541_1,  -- 4020000
        reqs(1)  => io_req_1541_2,  -- 4024000
        reqs(2)  => io_req_iec,     -- 4028000
        
        resps(0) => io_resp_1541_1,
        resps(1) => io_resp_1541_2,
        resps(2) => io_resp_iec );

    i_split3: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 8,
        g_range_hi  => 11,
        g_ports     => 8 )
    port map (
        clock    => sys_clock,
        
        req      => io_req_io,
        resp     => io_resp_io,
        
        reqs(0)  => io_req_sd,       -- 4060000 
        reqs(1)  => io_req_rtc,      -- 4060100 
        reqs(2)  => io_req_flash,    -- 4060200 
        reqs(3)  => io_req_debug,    -- 4060300 
        reqs(4)  => io_req_rtc_tmr,  -- 4060400
        reqs(5)  => io_req_gcr_dec,  -- 4060500
        reqs(6)  => io_req_icap,     -- 4060600
        reqs(7)  => io_req_aud_sel,  -- 4060700
        
        resps(0) => io_resp_sd,
        resps(1) => io_resp_rtc,
        resps(2) => io_resp_flash,
        resps(3) => io_resp_debug,
        resps(4) => io_resp_rtc_tmr,
        resps(5) => io_resp_gcr_dec,
        resps(6) => io_resp_icap,
        resps(7) => io_resp_aud_sel );


    r_usb: if g_usb_host generate
        i_usb: entity work.usb_host_io 
        generic map (
            g_simulation => g_simulation )
        port map (
            ulpi_clock  => ULPI_CLOCK,
            ulpi_reset  => ulpi_reset,
        
            -- ULPI Interface
            ULPI_DATA   => ULPI_DATA,
            ULPI_DIR    => ULPI_DIR,
            ULPI_NXT    => ULPI_NXT,
            ULPI_STP    => ULPI_STP,
        
			usb_busy	=> usb_busy, -- LED interface
			
            -- register interface bus
            sys_clock   => sys_clock,
            sys_reset   => sys_reset,
            
            sys_io_req  => io_req_usb,
            sys_io_resp => io_resp_usb );
    end generate;

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

    r_spi_flash: if g_spi_flash generate
        i_spi_flash: entity work.spi_peripheral_io
        generic map (
            g_fixed_rate => true,
            g_init_rate  => 0,
            g_crc        => false )
        port map (
            clock       => sys_clock,
            reset       => sys_reset,
            
            io_req      => io_req_flash,
            io_resp     => io_resp_flash,
                
            SD_DETECTn  => '0',
            SD_WRPROTn  => '1',
            SPI_SSn     => FLASH_CSn,
            SPI_CLK     => FLASH_SCK,
            SPI_MOSI    => FLASH_MOSI,
            SPI_MISO    => FLASH_MISO );
    end generate;

    r_no_spi_flash: if not g_spi_flash generate
        i_flash_dummy: entity work.io_dummy
        port map (
            clock       => sys_clock,
            io_req      => io_req_flash,
            io_resp     => io_resp_flash );
    end generate;
    
    r_rtc: if g_rtc_chip generate
        signal spi_ss_n : std_logic;
    begin
        i_spi_rtc: entity work.spi_peripheral_io
        generic map (
            g_fixed_rate => true,
            g_init_rate  => 31,
            g_crc        => false )
        port map (
            clock       => sys_clock,
            reset       => sys_reset,
            
            io_req      => io_req_rtc,
            io_resp     => io_resp_rtc,
                
            SD_DETECTn  => '0',
            SD_WRPROTn  => '1',
            SPI_SSn     => spi_ss_n,
            SPI_CLK     => RTC_SCK,
            SPI_MOSI    => RTC_MOSI,
            SPI_MISO    => RTC_MISO );

        RTC_CS <= not spi_ss_n;
    end generate;

    r_no_rtc: if not g_rtc_chip generate
        i_rtc_dummy: entity work.io_dummy
        port map (
            clock       => sys_clock,
            io_req      => io_req_rtc,
            io_resp     => io_resp_rtc );
    end generate;

    r_rtc_timer: if g_rtc_timer generate
        i_rtc_timer: entity work.real_time_clock
        generic map (
            g_freq      => g_clock_freq )
        port map (
            clock       => sys_clock,    
            reset       => sys_reset,
            
            req         => io_req_rtc_tmr,
            resp        => io_resp_rtc_tmr );
    end generate;

    r_no_rtc_timer: if not g_rtc_chip generate
        i_rtc_timer_dummy: entity work.io_dummy
        port map (
            clock       => sys_clock,
            io_req      => io_req_rtc_tmr,
            io_resp     => io_resp_rtc_tmr );
    end generate;

    r_gcr_codec: if g_hardware_gcr generate
        i_gcr_codec: entity work.gcr_codec
        port map (
            clock       => sys_clock,    
            reset       => sys_reset,
            
            req         => io_req_gcr_dec,
            resp        => io_resp_gcr_dec );
    end generate;

    r_iec: if g_hardware_iec generate
        i_iec: entity work.iec_processor_io
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            atn_i           => atn_i,
            atn_o           => hw_atn_o,
            clk_i           => clk_i,
            clk_o           => hw_clk_o,
            data_i          => data_i,
            data_o          => hw_data_o,
        
            req             => io_req_iec,
            resp            => io_resp_iec );
    end generate;

    r_c2n: if g_c2n_streamer generate
        i_c2n: entity work.c2n_playback_io
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            req             => io_req_c2n,
            resp            => io_resp_c2n,

			c64_stopped		=> c64_stopped,
            phi2_tick       => phi2_tick,
            c2n_sense       => c2n_sense,
            c2n_motor       => CAS_MOTOR,
            c2n_out_r       => c2n_out_r,
            c2n_out_w       => c2n_out_w );
    end generate;
    
    r_c2n_rec: if g_c2n_recorder generate
        i_c2n: entity work.c2n_record
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            req             => io_req_c2n_rec,
            resp            => io_resp_c2n_rec,

			c64_stopped		=> c64_stopped,
            phi2_tick       => phi2_tick,

            pull_sense      => c2n_pull_sense,
            c2n_sense       => c2n_sense_in,
            c2n_motor       => CAS_MOTOR,
            c2n_write       => CAS_WRITE,
            c2n_read        => CAS_READ );
    end generate;

    r_icap: if g_icap generate
        i_icap: entity work.icap
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            io_req          => io_req_icap,
            io_resp         => io_resp_icap );
    end generate;

    r_overlay: if g_video_overlay generate
        i_overlay: entity work.char_generator_peripheral
        generic map (
            g_screen_size   => 11,
            g_color_ram     => true )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            io_req          => io_req_big_io,  -- to be split later
            io_resp         => io_resp_big_io,

            keyb_col        => keyb_col,
            keyb_row        => keyb_row,
            
            overlay_on      => overlay_on,
            
            pix_clock       => vid_clock,
            pix_reset       => vid_reset,

            h_count         => vid_h_count,
            v_count         => vid_v_count,
            
            pixel_active    => vid_active,
            pixel_opaque    => vid_opaque,
            pixel_data      => vid_data );
        
    end generate;

	CAS_SENSE <= '0' when (c2n_sense='1') or (c2n_pull_sense='1') else 'Z';
	CAS_READ  <= '0' when c2n_out_r='0' else 'Z';
	CAS_WRITE <= '0' when c2n_out_w='0' else 'Z';

--    CAS_READ  <= trigger_1;
--    CAS_WRITE <= trigger_2;

    c2n_sense_in <= '1' when CAS_SENSE='0' else '0';
	

    i_mem_arb: entity work.mem_bus_arbiter_pri
    generic map (
        g_ports      => 5,
        g_registered => false )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        reqs(0)     => mem_req_cart,
        reqs(1)     => mem_req_1541,
        reqs(2)     => mem_req_1541_2,
        reqs(3)     => mem_req_debug,
        reqs(4)     => mem_req_cpu,

        resps(0)    => mem_resp_cart,
        resps(1)    => mem_resp_1541,
        resps(2)    => mem_resp_1541_2,
        resps(3)    => mem_resp_debug,
        resps(4)    => mem_resp_cpu,
        
        req         => mem_req,
        resp        => mem_resp );        


    i_aud_select: entity work.audio_select
    port map (
        clock           => sys_clock,
        reset           => sys_reset,
        
        req             => io_req_aud_sel,
        resp            => io_resp_aud_sel,
        
        drive0          => pwm,
        drive1          => pwm_2,
        cas_read        => CAS_READ,
        cas_write       => CAS_WRITE,
        sid_left        => sid_pwm_left,
        sid_right       => sid_pwm_right,
        samp_left       => samp_pwm_left,
        samp_right      => samp_pwm_right,
                
        pwm_out         => PWM_OUT );

    iec_atn_o    <= '0' when atn_o='0'  or atn_o_2='0'  or hw_atn_o='0'  else '1';
    iec_clock_o  <= '0' when clk_o='0'  or clk_o_2='0'  or hw_clk_o='0'  else '1';
    iec_data_o   <= '0' when data_o='0' or data_o_2='0' or hw_data_o='0' else '1';
    iec_srq_o    <= hw_srq_o; -- only source
        
	DISK_ACTn   <= act_led_n xor error;
	MOTOR_LEDn  <= motor_led_n xor error;
    CART_LEDn   <= cart_led_n xor error;
	SDACT_LEDn  <= (dirty_led_1_n and dirty_led_2_n and not (sd_act_stretched or usb_busy)) xor error;

--	DISK_ACTn   <= not freezer_state(1);
--	MOTOR_LEDn  <= not freezer_state(0);
--    CART_LEDn   <= IRQn;
--	SDACT_LEDn  <= NMIn;

    filt1: entity work.spike_filter generic map (10) port map(sys_clock, iec_atn_i,    atn_i);
    filt2: entity work.spike_filter generic map (10) port map(sys_clock, iec_clock_i,  clk_i);
    filt3: entity work.spike_filter generic map (10) port map(sys_clock, iec_data_i,   data_i);
    filt4: entity work.spike_filter generic map (10) port map(sys_clock, iec_srq_i,    srq_i);
    filt5: entity work.spike_filter port map(sys_clock, IRQn, c64_irq_n);
    c64_irq <= not c64_irq_n;

    -- dummy
    SD_DATA     <= "ZZ";
    
    g_ela: if g_analyzer generate
        signal ev_data  : std_logic_vector(15 downto 0);
    begin
        i_ela: entity work.logic_analyzer
        generic map (
            g_timer_div    => 50,
            g_change_width => 16,
            g_data_length  => 2 )
        port map (
            clock       => sys_clock,
            reset       => sys_reset,
            
            ev_dav      => '0',
            ev_data     => ev_data,
            
            ---
            mem_req     => mem_req_debug,
            mem_resp    => mem_resp_debug,
            
            io_req      => io_req_debug,
            io_resp     => io_resp_debug );
         
        ev_data <= srq_i & atn_i & data_i & clk_i & '1' & atn_o_2 & data_o_2 & clk_o_2 &
                   '0' & atn_o & data_o & clk_o & hw_srq_o & hw_atn_o & hw_data_o & hw_clk_o;
    end generate;
    
end logic;
