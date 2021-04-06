
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;
use work.dma_bus_pkg.all;

entity ultimate_logic_32 is
generic (
	g_version		: unsigned(7 downto 0) := X"FF";
    g_simulation    : boolean := true;
    g_ultimate2plus : boolean := false;
    g_ultimate_64   : boolean := false;
    g_clock_freq    : natural := 50_000_000;
    g_numerator     : natural := 8;
    g_denominator   : natural := 25;
    g_baud_rate     : natural := 115_200;
    g_timer_rate    : natural := 200_000;
    g_fpga_type     : natural := 0;
    g_cartreset_init: std_logic := '0';
    g_boot_stop     : boolean := false;
    g_direct_dma    : boolean := false;
    g_ext_freeze_act: boolean := false;
    g_microblaze    : boolean := true;
    g_big_endian    : boolean := true;
    g_boot_rom      : boolean := false;
    g_video_overlay : boolean := false;
    g_icap          : boolean := false;
    g_uart          : boolean := true;
    g_uart_rx       : boolean := false;
    g_drive_1541    : boolean := true;
    g_drive_1541_2  : boolean := false;
    g_hardware_gcr  : boolean := true;
    g_cartridge     : boolean := true;
    g_command_intf  : boolean := true;
    g_acia          : boolean := false;
    g_stereo_sid    : boolean := true;
    g_8voices       : boolean := true;
    g_ram_expansion : boolean := true;
    g_extended_reu  : boolean := false;
    g_hardware_iec  : boolean := true;
    g_iec_prog_tim  : boolean := false;
    g_c2n_streamer  : boolean := true;
    g_c2n_recorder  : boolean := true;
    g_drive_sound   : boolean := true;
    g_rtc_chip      : boolean := true;
    g_rtc_timer     : boolean := false;
    g_usb_host      : boolean := false;
    g_usb_host2     : boolean := true;
    g_spi_flash     : boolean := true;
    g_vic_copper    : boolean := false;
    g_sampler       : boolean := true;
    g_rmii          : boolean := false;
    g_kernal_repl   : boolean := true );
port (
    -- globals
    sys_clock   : in    std_logic;
    sys_reset   : in    std_logic;
    mb_reset    : in    std_logic := '0';
    
    ulpi_clock  : in    std_logic;
    ulpi_reset  : in    std_logic;

    -- slot side
    BUFFER_ENn  : out   std_logic := '1';

    phi2_i      : in    std_logic := '0';
    dotclk_i    : in    std_logic := '0';

    rstn_o      : out   std_logic := '1';
    rstn_i      : in    std_logic := '1';

    slot_addr_o : out   unsigned(15 downto 0);
    slot_addr_i : in    unsigned(15 downto 0) := (others => '1');
    slot_addr_tl: out   std_logic;
    slot_addr_th: out   std_logic;
    
    slot_data_o : out   std_logic_vector(7 downto 0);
    slot_data_i : in    std_logic_vector(7 downto 0) := (others => '1');
    slot_data_t : out   std_logic;

    rwn_i       : in    std_logic := '1';
    rwn_o       : out   std_logic;
        
    ultimax     : out   std_logic;
    exromn_i    : in    std_logic := '1';
    exromn_o    : out   std_logic;
    gamen_i     : in    std_logic := '1';
    gamen_o     : out   std_logic;

    irqn_i      : in    std_logic := '1';
    irqn_o      : out   std_logic;
    nmin_i      : in    std_logic := '1';
    nmin_o      : out   std_logic;

    ba_i        : in    std_logic := '0';
    dman_o      : out   std_logic;
    romhn_i     : in    std_logic := '1';
    romln_i     : in    std_logic := '1';
    io1n_i      : in    std_logic := '1';
    io2n_i      : in    std_logic := '1';

    VCC         : in    std_logic := '1';
    freeze_activate : in  std_logic := '0';

    -- local bus side
    mem_inhibit : out   std_logic;
    mem_req     : out   t_mem_req_32;
    mem_resp    : in    t_mem_resp_32;
    
    -- Direct DMA for U64
    direct_dma_req   : out   t_dma_req := c_dma_req_init;
    direct_dma_resp  : in    t_dma_resp := c_dma_resp_init;

    -- Audio outputs
    audio_speaker    : out signed(12 downto 0);
    audio_left       : out signed(18 downto 0);
    audio_right      : out signed(18 downto 0);
    speaker_vol      : in std_logic_vector(3 downto 0) := X"0";

    aud_drive1       : out signed(17 downto 0); 
    aud_drive2       : out signed(17 downto 0); 
    aud_tape_r       : out signed(17 downto 0); 
    aud_tape_w       : out signed(17 downto 0); 
    aud_samp_l       : out signed(17 downto 0); 
    aud_samp_r       : out signed(17 downto 0); 
    aud_sid_1        : out signed(17 downto 0);
    aud_sid_2        : out signed(17 downto 0);

    -- IEC bus
    -- actual levels of the pins --
    iec_reset_i : in    std_logic := '1';
    iec_atn_i   : in    std_logic := '1';
    iec_data_i  : in    std_logic := '1';
    iec_clock_i : in    std_logic := '1';
    iec_srq_i   : in    std_logic := '1';
    
    iec_reset_o : out   std_logic := '1';
    iec_atn_o   : out   std_logic;
    iec_data_o  : out   std_logic;
    iec_clock_o : out   std_logic;
    iec_srq_o   : out   std_logic;

    MOTOR_LEDn  : out   std_logic;
    DISK_ACTn   : out   std_logic; -- activity LED
	CART_LEDn	: out   std_logic;
	SDACT_LEDn	: out   std_logic;
    motor_led2n : out   std_logic;
    disk_act2n  : out   std_logic;
    	
    -- Parallel cable pins
    drv_track_is_0      : out std_logic;
    drv_via1_port_a_o   : out std_logic_vector(7 downto 0);
    drv_via1_port_a_i   : in  std_logic_vector(7 downto 0);
    drv_via1_port_a_t   : out std_logic_vector(7 downto 0);
    drv_via1_ca2_o      : out std_logic;
    drv_via1_ca2_i      : in  std_logic;
    drv_via1_ca2_t      : out std_logic;
    drv_via1_cb1_o      : out std_logic;
    drv_via1_cb1_i      : in  std_logic;
    drv_via1_cb1_t      : out std_logic;

    -- Debug port
    drv_debug_data      : out std_logic_vector(31 downto 0);
    drv_debug_valid     : out std_logic;
    c64_debug_data      : out std_logic_vector(31 downto 0);
    c64_debug_valid     : out std_logic;
    c64_debug_select    : in  std_logic_vector(2 downto 0) := "000";
    
	-- Debug UART
	UART_TXD	: out   std_logic;
	UART_RXD	: in    std_logic := '1';
	
    -- SD Card Interface
    SD_SSn      : out   std_logic;
    SD_CLK      : out   std_logic;
    SD_MOSI     : out   std_logic;
    SD_MISO     : in    std_logic := '1';
    SD_CARDDETn : in    std_logic := '1';
    SD_DATA     : inout std_logic_vector(2 downto 1) := "ZZ";
    
    -- LED interface
    LED_CLK     : out   std_logic;
    LED_DATA    : out   std_logic;

    -- RTC Interface
    RTC_CS      : out   std_logic;
    RTC_SCK     : out   std_logic;
    RTC_MOSI    : out   std_logic;
    RTC_MISO    : in    std_logic := '0';

    -- Flash Interface
    FLASH_CSn   : out   std_logic;
    FLASH_SCK   : out   std_logic;
    FLASH_MOSI  : out   std_logic;
    FLASH_MISO  : in    std_logic := '0';

    -- USB Interface (ULPI)
    ULPI_NXT    : in    std_logic := '0';
    ULPI_STP    : out   std_logic;
    ULPI_DIR    : in    std_logic := '0';
    ULPI_DATA   : inout std_logic_vector(7 downto 0) := "ZZZZZZZZ";

    -- Cassette Interface
    c2n_read_in     : in  std_logic := '1';
    c2n_write_in    : in  std_logic := '1';
    c2n_read_out    : out std_logic := '1';
    c2n_write_out   : out std_logic := '1';
    c2n_read_en     : out std_logic := '0';
    c2n_write_en    : out std_logic := '0';
    c2n_sense_in    : in  std_logic := '0';
    c2n_sense_out   : out std_logic := '0';
    c2n_motor_in    : in  std_logic := '0';
    c2n_motor_out   : out std_logic := '0';
    
    -- Ethernet interface
    eth_clock       : in std_logic := '0';
    eth_reset       : in std_logic := '0';
    eth_tx_data     : out std_logic_vector(7 downto 0);
    eth_tx_eof      : out std_logic;
    eth_tx_valid    : out std_logic;
    eth_tx_ready    : in  std_logic;
    eth_rx_data     : in  std_logic_vector(7 downto 0);
    eth_rx_sof      : in  std_logic;
    eth_rx_eof      : in  std_logic;
    eth_rx_valid    : in  std_logic;

    -- Interface to other graphical output (Full HD of course and in 3D!) ;-)
    vid_clock   : in    std_logic := '0';
    vid_reset   : in    std_logic := '0';
    vid_h_count : in    unsigned(11 downto 0) := (others => '0');
    vid_v_count : in    unsigned(11 downto 0) := (others => '0');
    vid_active  : out   std_logic;
    vid_opaque  : out   std_logic;
    vid_data    : out   unsigned(3 downto 0);
    overlay_on  : out   std_logic;
    keyb_row    : in    std_logic_vector(7 downto 0) := (others => '1');
    keyb_col    : inout std_logic_vector(7 downto 0) := (others => '1');

    -- Simulation port
    ext_io_req  : in  t_io_req := c_io_req_init;
    ext_io_resp : out t_io_resp;
    ext_mem_req : in  t_mem_req_32 := c_mem_req_32_init;
    ext_mem_resp: out t_mem_resp_32;
    
    cpu_irq     : out std_logic;
    trigger     : in  std_logic := '0';
    sw_trigger  : out std_logic;
        
    -- Buttons
    button      : in  std_logic_vector(2 downto 0) );
	
end ultimate_logic_32;


architecture logic of ultimate_logic_32 is

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
        cap(22) := '0'; 
        cap(23) := to_std(g_usb_host2);
        cap(24) := to_std(g_rmii);
        cap(25) := to_std(g_ultimate2plus);
        cap(26) := to_std(g_ultimate_64);
        cap(27) := to_std(g_acia);
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
    constant c_tag_slot          : std_logic_vector(7 downto 0) := X"07";
    constant c_tag_reu           : std_logic_vector(7 downto 0) := X"08";
    constant c_tag_usb2          : std_logic_vector(7 downto 0) := X"09";
    constant c_tag_cpu_i         : std_logic_vector(7 downto 0) := X"0A";
    constant c_tag_cpu_d         : std_logic_vector(7 downto 0) := X"0B";
    constant c_tag_rmii          : std_logic_vector(7 downto 0) := X"0E"; -- and 0F

    -- Timing
    signal tick_16MHz       : std_logic;
    signal tick_4MHz        : std_logic;
    signal tick_1MHz        : std_logic;
    signal tick_1kHz        : std_logic;    

	-- Memory interface
    signal mem_req_32_cpu        : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_cpu       : t_mem_resp_32 := c_mem_resp_32_init;

    signal mem_req_1541          : t_mem_req := c_mem_req_init;
    signal mem_resp_1541         : t_mem_resp := c_mem_resp_init;
    signal mem_req_1541_2        : t_mem_req := c_mem_req_init;
    signal mem_resp_1541_2       : t_mem_resp := c_mem_resp_init;
    signal mem_req_debug         : t_mem_req := c_mem_req_init;
    signal mem_resp_debug        : t_mem_resp := c_mem_resp_init;

    -- converted to 32 bits
    signal mem_req_32_1541       : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_1541      : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_1541_2     : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_1541_2    : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_cart       : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_cart      : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_debug      : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_debug     : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_usb        : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_usb       : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_rmii       : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp_32_rmii      : t_mem_resp_32 := c_mem_resp_32_init;

    -- IO Bus
    signal cpu_io_busy      : std_logic;
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
    signal io_req_rmii      : t_io_req;
    signal io_resp_rmii     : t_io_resp := c_io_resp_init;
    signal io_req_debug     : t_io_req;
    signal io_resp_debug    : t_io_resp := c_io_resp_init;
    signal io_irq           : std_logic;
    
    signal drive_sample_1   : signed(12 downto 0);
    signal drive_sample_2   : signed(12 downto 0);
    signal audio_tape_read  : signed(18 downto 0);
    signal audio_tape_write : signed(18 downto 0);
    signal sid_left         : signed(17 downto 0);
    signal sid_right        : signed(17 downto 0);
    signal samp_left        : signed(17 downto 0);
    signal samp_right       : signed(17 downto 0);
    signal audio_select_left    : std_logic_vector(3 downto 0);
    signal audio_select_right   : std_logic_vector(3 downto 0);
    
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

    -- Cassette
    signal c2n_play_sense_out   : std_logic := '0';
    signal c2n_play_motor_out   : std_logic := '0';
    signal c2n_rec_sense_out    : std_logic := '0';
    signal c2n_rec_motor_out    : std_logic := '0';
    signal c2n_read_en_i        : std_logic := '0';
    signal c2n_read_out_i       : std_logic := '0';
        
    -- miscellaneous interconnect
    signal c64_reset_in     : std_logic;
    signal c64_reset_in_n   : std_logic;
    signal c64_irq_n        : std_logic;
    signal c64_irq          : std_logic;
    signal phi2_tick        : std_logic;
    signal c64_stopped		: std_logic;
    signal cas_read_c       : std_logic;
    signal cas_write_c      : std_logic;
	signal busy_led			: std_logic;
	signal sd_busy          : std_logic;
	signal sd_act_stretched : std_logic;
	signal disk_led_n		: std_logic := '1';
	signal motor_led_n		: std_logic := '1';
	signal cart_led_n		: std_logic := '1';
	signal c2n_pull_sense   : std_logic := '0';
    signal freezer_state    : std_logic_vector(1 downto 0);
    signal dirty_led_1_n    : std_logic := '1';
    signal dirty_led_2_n    : std_logic := '1';

    signal dman_oi          : std_logic;
    signal trigger_1        : std_logic;
    signal trigger_2        : std_logic;
    signal sync             : std_logic;
    signal sys_irq_usb      : std_logic := '0';
    signal sys_irq_tape     : std_logic := '0';
    signal sys_irq_iec      : std_logic := '0';
    signal sys_irq_cmdif    : std_logic := '0';
    signal sys_irq_acia     : std_logic := '0';
    signal sys_irq_eth_tx   : std_logic := '0';
    signal sys_irq_eth_rx   : std_logic := '0';
    signal misc_io          : std_logic_vector(7 downto 0);

    signal audio_speaker_tmp : signed(17 downto 0);
begin
    r_mb: if g_microblaze generate
        signal invalidate       : std_logic;
        signal inv_addr         : std_logic_vector(31 downto 0);
    begin
        i_cpu: entity work.mblite_wrapper
        generic map (
            g_tag_i     => c_tag_cpu_i,
            g_tag_d     => c_tag_cpu_d )
        port map (
            clock       => sys_clock,
            reset       => sys_reset,
            mb_reset    => mb_reset,
            
            irq_i       => io_irq,
            disable_i   => misc_io(1),
            disable_d   => misc_io(2),
            invalidate  => invalidate,
            inv_addr    => inv_addr,
            
            -- memory interface
            mem_req     => mem_req_32_cpu,
            mem_resp    => mem_resp_32_cpu,
            
            io_busy     => cpu_io_busy,
            io_req      => cpu_io_req,
            io_resp     => cpu_io_resp );

        -- TODO: also invalidate for rmii
        invalidate <= misc_io(0) when (mem_resp_32_usb.rack_tag(5 downto 0) = c_tag_usb2(5 downto 0)) and (mem_req_32_usb.read_writen = '0') else '0';
        inv_addr(31 downto 26) <= (others => '0');
        inv_addr(25 downto 0) <= std_logic_vector(mem_req_32_usb.address);
    end generate;

--    r_no_mb: if not g_microblaze generate
--        -- route the memory access of the processor to the outside world
--        mem_req_32_cpu <= ext_mem_req;
--        ext_mem_resp   <= mem_resp_32_cpu;
--    end generate;

    cpu_irq <= io_irq;
		
    i_io_arb: entity work.io_bus_arbiter_pri
    generic map (
        g_ports     => 2 )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        reqs(0)     => ext_io_req,
        reqs(1)     => cpu_io_req,
        
        resps(0)    => ext_io_resp,
        resps(1)    => cpu_io_resp,
        
        req         => io_req,
        resp        => io_resp );

    i_timing: entity work.fractional_div
    generic map(
        g_numerator   => g_numerator,
        g_denominator => g_denominator
    )
    port map(
        clock         => sys_clock,
        tick          => tick_16MHz,
        tick_by_4     => tick_4MHz,
        tick_by_16    => tick_1MHz,
        one_16000     => tick_1kHz
    );

    c64_reset_in <= not c64_reset_in_n;
    
    i_itu: entity work.itu
    generic map (
		g_version	    => g_version,
        g_capabilities  => c_capabilities,
        g_uart          => g_uart,
        g_uart_rx       => g_uart_rx,
        g_edge_init     => "10000101",
        g_edge_write    => false,
        g_baudrate      => g_baud_rate )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        io_req      => io_req_itu,
        io_resp     => io_resp_itu,
    
        tick_4MHz   => tick_4MHz,
        tick_1us    => tick_1MHz,
        tick_1ms    => tick_1kHz,
        buttons     => button,

        irq_high(0) => sys_irq_acia,
        irq_high(7 downto 1) => "0000000",
        irq_in(7)   => c64_reset_in,
        irq_in(6)   => sys_irq_eth_tx,
        irq_in(5)   => sys_irq_eth_rx,
        irq_in(4)   => sys_irq_cmdif,
        irq_in(3)   => sys_irq_tape,
        irq_in(2)   => sys_irq_usb,
        
        irq_out     => io_irq,
        
        busy_led    => busy_led,
        misc_io     => misc_io,

        uart_txd    => UART_TXD,
        uart_rxd    => UART_RXD );


    r_drive: if g_drive_1541 generate
    begin
        i_drive: entity work.c1541_drive
        generic map (
            g_big_endian    => g_big_endian,
            g_cpu_tag       => c_tag_1541_cpu_1,
            g_floppy_tag    => c_tag_1541_floppy_1,
            g_audio_tag     => c_tag_1541_audio_1,
            g_audio         => g_drive_sound,
            g_audio_base    => X"0EC0000",
            g_ram_base      => X"0EE0000" )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            drive_stop      => c64_stopped,
            
            -- timing
            tick_16MHz      => tick_16MHz,
            tick_4MHz       => tick_4MHz,
            
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
            c64_reset_n     => c64_reset_in_n,
            
            -- Debug port
            debug_data      => drv_debug_data,
            debug_valid     => drv_debug_valid,

            -- Parallel cable pins
            track_is_0      => drv_track_is_0,
            via1_port_a_o   => drv_via1_port_a_o,
            via1_port_a_i   => drv_via1_port_a_i,
            via1_port_a_t   => drv_via1_port_a_t,
            via1_ca2_o      => drv_via1_ca2_o,
            via1_ca2_i      => drv_via1_ca2_i,
            via1_ca2_t      => drv_via1_ca2_t,
            via1_cb1_o      => drv_via1_cb1_o,
            via1_cb1_i      => drv_via1_cb1_i,
            via1_cb1_t      => drv_via1_cb1_t,

            -- LED
            act_led_n       => disk_led_n,
            motor_led_n     => motor_led_n,
            dirty_led_n     => dirty_led_1_n,

            -- audio out
            audio_sample    => drive_sample_1 );
    end generate;

    audio_speaker_tmp <= drive_sample_1 * signed(resize(unsigned(speaker_vol),5));
    audio_speaker <= audio_speaker_tmp(16 downto 4);

    r_drive_2: if g_drive_1541_2 generate
        -- Parallel cable pins
        signal via1_port_a_o   : std_logic_vector(7 downto 0);
        signal via1_port_a_i   : std_logic_vector(7 downto 0);
        signal via1_port_a_t   : std_logic_vector(7 downto 0);
        signal via1_ca2_o      : std_logic;
        signal via1_ca2_i      : std_logic;
        signal via1_ca2_t      : std_logic;
        signal via1_cb1_o      : std_logic;
        signal via1_cb1_i      : std_logic;
        signal via1_cb1_t      : std_logic;
        signal track_is_0      : std_logic;
    begin
        i_drive: entity work.c1541_drive
        generic map (
            g_big_endian    => g_big_endian,
            g_cpu_tag       => c_tag_1541_cpu_2,
            g_floppy_tag    => c_tag_1541_floppy_2,
            g_audio_tag     => c_tag_1541_audio_2,
            g_audio         => g_drive_sound,
            g_audio_base    => X"0EC0000",
            g_ram_base      => X"0ED0000" )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            drive_stop      => c64_stopped,
            
            -- timing
            tick_16MHz      => tick_16MHz,
            tick_4MHz       => tick_4MHz,

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
            c64_reset_n     => c64_reset_in_n,

            -- Parallel cable pins
            track_is_0      => track_is_0,
            via1_port_a_o   => via1_port_a_o,
            via1_port_a_i   => via1_port_a_i,
            via1_port_a_t   => via1_port_a_t,
            via1_ca2_o      => via1_ca2_o,
            via1_ca2_i      => via1_ca2_i,
            via1_ca2_t      => via1_ca2_t,
            via1_cb1_o      => via1_cb1_o,
            via1_cb1_i      => via1_cb1_i,
            via1_cb1_t      => via1_cb1_t,

            -- LED
            act_led_n       => disk_act2n,
            motor_led_n     => motor_led2n,
            dirty_led_n     => dirty_led_2_n,

            -- audio out
            audio_sample    => drive_sample_2 );

        via1_port_a_i(7 downto 1) <= via1_port_a_o(7 downto 1) or not via1_port_a_t(7 downto 1);
        via1_port_a_i(0)          <= track_is_0; -- for 1541C
        
        via1_ca2_i    <= via1_ca2_o    or not via1_ca2_t;
        via1_cb1_i    <= via1_cb1_o    or not via1_cb1_t;
    end generate;

    r_cart: if g_cartridge generate
        i_slot_srv: entity work.slot_server_v4
        generic map (
            g_clock_freq    => g_clock_freq,
            g_direct_dma    => g_direct_dma,
            g_ext_freeze_act=> g_ext_freeze_act,
            g_tag_slot      => c_tag_slot,
            g_tag_reu       => c_tag_reu,
            g_ram_base_reu  => X"1000000", -- should be on 16M boundary, or should be limited in size
            g_rom_base_cart => X"0F00000", -- should be on a 1M boundary
            g_ram_base_cart => X"0EF0000", -- should be on a 64K boundary
            g_big_endian    => g_big_endian,
            g_cartreset_init=> g_cartreset_init,
            g_boot_stop     => g_boot_stop,
            g_control_read  => true,
            g_kernal_repl   => g_kernal_repl,
            g_ram_expansion => g_ram_expansion,
            g_extended_reu  => g_extended_reu,
            g_command_intf  => g_command_intf,
            g_acia          => g_acia,
            g_sampler       => g_sampler,
            g_implement_sid => g_stereo_sid,
            g_sid_voices    => 16,
            g_8voices       => g_8voices,
            g_vic_copper    => g_vic_copper )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
            
            -- Cartridge pins
            VCC             => VCC,

            phi2_i          => phi2_i,

            rstn_i          => rstn_i,
            rstn_o          => rstn_o,
                       
            slot_addr_i     => slot_addr_i,
            slot_addr_o     => slot_addr_o,
            slot_addr_tl    => slot_addr_tl,
            slot_addr_th    => slot_addr_th,

            slot_data_i     => slot_data_i,
            slot_data_o     => slot_data_o,
            slot_data_t     => slot_data_t,

            rwn_i           => rwn_i,
            rwn_o           => rwn_o, -- goes with addr_t

            ba_i            => ba_i,
            dman_o          => dman_oi,
                                       
            ultimax         => ultimax,
            exromn_i        => exromn_i,
            exromn_o        => exromn_o,
            gamen_i         => gamen_i,
            gamen_o         => gamen_o,
            irqn_i          => irqn_i,
            irqn_o          => irqn_o,
            -- nmin_i          => nmin_i,
            nmin_o          => nmin_o,
                                           
            romhn_i         => romhn_i,
            romln_i         => romln_i,
            io1n_i          => io1n_i,
            io2n_i          => io2n_i,
        
            -- other hardware pins
            BUFFER_ENn      => BUFFER_ENn,
            sense           => c2n_sense_in,        
			buttons 		=> button,
            cart_led_n      => cart_led_n,
            
            -- audio
            sid_left        => sid_left,
            sid_right       => sid_right,
            samp_left       => samp_left,
            samp_right      => samp_right,

            -- debug
            freeze_activate => freeze_activate,
            freezer_state   => freezer_state,
            sync            => sync,
            sw_trigger      => sw_trigger,
            trigger_1       => trigger_1,
            trigger_2       => trigger_2,
            debug_data      => c64_debug_data,
            debug_valid     => c64_debug_valid,
            debug_select    => c64_debug_select,
            
            -- timing output
			c64_stopped		=> c64_stopped,
            phi2_tick       => phi2_tick,

            -- master on memory bus
            memctrl_inhibit => mem_inhibit,
            mem_req         => mem_req_32_cart,
            mem_resp        => mem_resp_32_cart,

            direct_dma_req  => direct_dma_req,
            direct_dma_resp => direct_dma_resp,
            
            -- slave on io bus
            io_req          => io_req_cart,
            io_resp         => io_resp_cart,
            io_irq_cmd      => sys_irq_cmdif,
            io_irq_acia     => sys_irq_acia );

        dman_o <= dman_oi;
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
        g_ports     => 9 )
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
        reqs(8)  => io_req_rmii,     -- 4060800

        resps(0) => io_resp_sd,
        resps(1) => io_resp_rtc,
        resps(2) => io_resp_flash,
        resps(3) => io_resp_debug,
        resps(4) => io_resp_rtc_tmr,
        resps(5) => io_resp_gcr_dec,
        resps(6) => io_resp_icap,
        resps(7) => io_resp_aud_sel,
        resps(8) => io_resp_rmii );


--    r_usb: if g_usb_host generate
--        i_usb: entity work.usb_host_io 
--        generic map (
--            g_simulation => g_simulation )
--        port map (
--            ulpi_clock  => ULPI_CLOCK,
--            ulpi_reset  => ulpi_reset,
--        
--            -- ULPI Interface
--            ULPI_DATA   => ULPI_DATA,
--            ULPI_DIR    => ULPI_DIR,
--            ULPI_NXT    => ULPI_NXT,
--            ULPI_STP    => ULPI_STP,
--        
--            usb_busy    => usb_busy, -- LED interface
--			
--            -- register interface bus
--            sys_clock   => sys_clock,
--            sys_reset   => sys_reset,
--            
--            sys_io_req  => io_req_usb,
--            sys_io_resp => io_resp_usb );
--    end generate;
--
    r_usb2: if g_usb_host2 generate
        i_usb2: entity work.usb_host_nano
        generic map (
            g_big_endian => g_big_endian,
            g_tag        => c_tag_usb2,
            g_simulation => g_simulation )
        port map (
            clock        => ULPI_CLOCK,
            reset        => ulpi_reset,
            ulpi_nxt     => ulpi_nxt,
            ulpi_dir     => ulpi_dir,
            ulpi_stp     => ulpi_stp,
            ulpi_data    => ulpi_data,
            sys_clock    => sys_clock,
            sys_reset    => sys_reset,
            sys_mem_req  => mem_req_32_usb,
            sys_mem_resp => mem_resp_32_usb,
            sys_io_req   => io_req_usb,
            sys_io_resp  => io_resp_usb,
            sys_irq      => sys_irq_usb );
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

    LED_CLK <= 'Z';
    LED_DATA <= 'Z';

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
            g_init_rate  => 1,
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
        
            tick            => tick_1MHz,

            srq_i           => srq_i,
            srq_o           => hw_srq_o,
            atn_i           => atn_i,
            atn_o           => hw_atn_o,
            clk_i           => clk_i,
            clk_o           => hw_clk_o,
            data_i          => data_i,
            data_o          => hw_data_o,
        
            irq             => sys_irq_iec, -- TODO: is not connected anywhere
            req             => io_req_iec,
            resp            => io_resp_iec );
    end generate;

    r_c2n: if g_c2n_streamer generate
        i_c2n: entity work.c2n_playback_io -- tap -> signals
        generic map (
            g_clock_freq    => g_clock_freq )
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            req             => io_req_c2n,
            resp            => io_resp_c2n,

			c64_stopped		=> c64_stopped,

            c2n_sense_in    => c2n_sense_in,
            c2n_motor_in    => c2n_motor_in,

            c2n_sense_out   => c2n_play_sense_out,
            c2n_motor_out   => c2n_play_motor_out,
            c2n_out_en_r    => c2n_read_en_i,
            c2n_out_en_w    => c2n_write_en,
            c2n_out_r       => c2n_read_out_i,
            c2n_out_w       => c2n_write_out );
    end generate;

    c2n_read_out <= c2n_read_out_i;
    c2n_read_en  <= c2n_read_en_i;
    
    r_c2n_rec: if g_c2n_recorder generate -- signals => tap
        i_c2n: entity work.c2n_record
        port map (
            clock           => sys_clock,
            reset           => sys_reset,
        
            irq             => sys_irq_tape,
            req             => io_req_c2n_rec,
            resp            => io_resp_c2n_rec,

			c64_stopped		=> c64_stopped,
            phi2_tick       => phi2_tick,

            c2n_sense       => c2n_sense_in,
            c2n_motor_in    => c2n_motor_in,
            c2n_write       => c2n_write_in,
            c2n_read        => c2n_read_in,

            pull_sense      => c2n_rec_sense_out,
            c2n_motor_out   => c2n_rec_motor_out );
    end generate;

    c2n_sense_out <= c2n_play_sense_out or c2n_rec_sense_out;
    c2n_motor_out <= c2n_play_motor_out or c2n_rec_motor_out;

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

    i_conv32_1541: entity work.mem_to_mem32(route_through)
    generic map (
        g_big_endian => g_big_endian )
    port map(
        clock       => sys_clock,
        reset       => sys_reset,
        mem_req_8   => mem_req_1541,
        mem_resp_8  => mem_resp_1541,
        mem_req_32  => mem_req_32_1541,
        mem_resp_32 => mem_resp_32_1541 );

    i_conv32_1541_2: entity work.mem_to_mem32(route_through)
    generic map (
        g_big_endian => g_big_endian )
    port map(
        clock       => sys_clock,
        reset       => sys_reset,
        mem_req_8   => mem_req_1541_2,
        mem_resp_8  => mem_resp_1541_2,
        mem_req_32  => mem_req_32_1541_2,
        mem_resp_32 => mem_resp_32_1541_2 );

    i_mem_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_ports      => 7,
        g_registered => false )
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        reqs(0)     => mem_req_32_cart,
        reqs(1)     => mem_req_32_1541,
        reqs(2)     => mem_req_32_1541_2,
        reqs(3)     => mem_req_32_rmii,
        reqs(4)     => mem_req_32_usb,
        reqs(5)     => mem_req_32_cpu,
        reqs(6)     => ext_mem_req,
        
        resps(0)    => mem_resp_32_cart,
        resps(1)    => mem_resp_32_1541,
        resps(2)    => mem_resp_32_1541_2,
        resps(3)    => mem_resp_32_rmii,
        resps(4)    => mem_resp_32_usb,
        resps(5)    => mem_resp_32_cpu,
        resps(6)    => ext_mem_resp,
        
        req         => mem_req,
        resp        => mem_resp );        


    i_aud_select: entity work.audio_select
    port map (
        clock           => sys_clock,
        reset           => sys_reset,
        
        req             => io_req_aud_sel,
        resp            => io_resp_aud_sel,
        
        select_left     => audio_select_left,
        select_right    => audio_select_right );
        
    -- generate raw samples for audio
    audio_tape_read  <= to_signed(-100000, 19) when cas_read_c = '0' else to_signed(100000, 19);
    audio_tape_write <= to_signed(-100000, 19) when cas_write_c = '0' else to_signed(100000, 19);  
        
    -- direct outputs for mixing in U64
    aud_drive1  <= drive_sample_1(11 downto 0) & "000000";
    aud_drive2  <= drive_sample_2(11 downto 0) & "000000";
    aud_tape_r  <= audio_tape_read(18 downto 1);
    aud_tape_w  <= audio_tape_write(18 downto 1);
    aud_samp_l  <= samp_left;
    aud_samp_r  <= samp_right;
    aud_sid_1   <= sid_left;
    aud_sid_2   <= sid_right;
    
    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            cas_read_c  <= (c2n_read_in and not c2n_read_en_i) or (c2n_read_out_i and c2n_read_en_i);
            cas_write_c <= c2n_write_in;

            audio_left <= (others => '0');
            audio_right <= (others => '0');
            case audio_select_left is
            when X"0" =>
                audio_left(18 downto 7) <= drive_sample_1(11 downto 0);
            when X"1" =>
                audio_left(18 downto 7) <= drive_sample_2(11 downto 0);
            when X"2" =>
                audio_left <= audio_tape_read;
            when X"3" =>
                audio_left <= audio_tape_write;
            when X"4" =>
                audio_left  <= sid_left & '0';
            when X"5" =>
                audio_left  <= sid_right & '0';
            when X"6" =>
                audio_left  <= samp_left & '0';
            when X"7" =>
                audio_left  <= samp_right & '0';
--            when X"8" =>
--                audio_left  <= (sid_left(17) & sid_left) + (samp_left(17) & samp_left);
--            when X"9" =>
--                audio_left  <= (sid_right(17) & sid_right) + (samp_right(17) & samp_right);
--            when X"A" =>
--                audio_left  <= (sid_left(17) & sid_left) + (sid_right(17) & sid_right);
--            when X"B" =>
--                audio_left  <= (samp_left(17) & samp_left) + (samp_right(17) & samp_right);
            when others =>
                null;
            end case;                            

            case audio_select_right is
            when X"0" =>
                audio_right(18 downto 7) <= drive_sample_1(11 downto 0);
            when X"1" =>
                audio_right(18 downto 7) <= drive_sample_2(11 downto 0);
            when X"2" =>
                audio_right <= audio_tape_read;
            when X"3" =>
                audio_right <= audio_tape_write;
            when X"4" =>
                audio_right  <= sid_left & '0';
            when X"5" =>
                audio_right  <= sid_right & '0';
            when X"6" =>
                audio_right  <= samp_left & '0';
            when X"7" =>
                audio_right  <= samp_right & '0';
--            when X"8" =>
--                audio_right  <= (sid_left(17) & sid_left) + (samp_left(17) & samp_left);
--            when X"9" =>
--                audio_right  <= (sid_right(17) & sid_right) + (samp_right(17) & samp_right);
--            when X"A" =>
--                audio_right  <= (sid_left(17) & sid_left) + (sid_right(17) & sid_right);
--            when X"B" =>
--                audio_right  <= (samp_left(17) & samp_left) + (samp_right(17) & samp_right);
            when others =>
                null;
            end case;                            
        end if;  
    end process;

    iec_atn_o    <= '0' when atn_o='0'  or atn_o_2='0'  or hw_atn_o='0'  else '1';
    iec_clock_o  <= '0' when clk_o='0'  or clk_o_2='0'  or hw_clk_o='0'  else '1';
    iec_data_o   <= '0' when data_o='0' or data_o_2='0' or hw_data_o='0' else '1';
    iec_srq_o    <= hw_srq_o; -- only source
        
    MOTOR_LEDn  <= motor_led_n;
	DISK_ACTn   <= disk_led_n;
    CART_LEDn   <= cart_led_n;
	SDACT_LEDn  <= (dirty_led_1_n and dirty_led_2_n and not (sd_act_stretched or busy_led));

    filt1: entity work.spike_filter generic map (10) port map(sys_clock, iec_atn_i,    atn_i);
    filt2: entity work.spike_filter generic map (10) port map(sys_clock, iec_clock_i,  clk_i);
    filt3: entity work.spike_filter generic map (10) port map(sys_clock, iec_data_i,   data_i);
    filt4: entity work.spike_filter generic map (10) port map(sys_clock, iec_srq_i,    srq_i);
    filt5: entity work.spike_filter port map(sys_clock, irqn_i, c64_irq_n);
    filt6: entity work.spike_filter port map(sys_clock, rstn_i, c64_reset_in_n );
    c64_irq <= not c64_irq_n;

    -- dummy
    SD_DATA     <= "ZZ";
    
    i_debug_dummy: entity work.io_dummy
    port map (
        clock       => sys_clock,
        io_req      => io_req_debug,
        io_resp     => io_resp_debug );

    r_rmii: if g_rmii generate
    begin
        i_rmii: entity work.ethernet_interface
        generic map (
            g_mem_tag   => c_tag_rmii
        )
        port map(
            sys_clock   => sys_clock,
            sys_reset   => sys_reset,
            io_req      => io_req_rmii,
            io_resp     => io_resp_rmii,
            io_irq_tx   => sys_irq_eth_tx,
            io_irq_rx   => sys_irq_eth_rx,
            mem_req     => mem_req_32_rmii,
            mem_resp    => mem_resp_32_rmii,
            
            eth_clock       => eth_clock,
            eth_reset       => eth_reset,
            eth_rx_data     => eth_rx_data,
            eth_rx_sof      => eth_rx_sof,
            eth_rx_eof      => eth_rx_eof,
            eth_rx_valid    => eth_rx_valid,
    
            eth_tx_data     => eth_tx_data,
            eth_tx_eof      => eth_tx_eof,
            eth_tx_valid    => eth_tx_valid,
            eth_tx_ready    => eth_tx_ready );
        
    end generate;

end logic;
