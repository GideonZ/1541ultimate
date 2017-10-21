
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity ultimate_mb_700a is
generic (
    g_dual_drive    : boolean := false;
    g_version       : unsigned(7 downto 0) := X"0D" );
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
    
    -- memory
    SDRAM_A     : out   std_logic_vector(12 downto 0); -- DRAM A
    SDRAM_BA    : out   std_logic_vector(1 downto 0);
    SDRAM_DQ    : inout std_logic_vector(7 downto 0);
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
    
    -- LED Interface
    LED_CLK     : out   std_logic;
    LED_DATA    : out   std_logic;

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
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));
    
end entity;


architecture structural of ultimate_mb_700a is

    signal reset_in     : std_logic;
    signal dcm_lock     : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal sys_clock_2x : std_logic;
--    signal sys_shifted  : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);
    signal RSTn_out     : std_logic;
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;

    -- Slot
    signal slot_addr_o  : std_logic_vector(15 downto 0);
    signal slot_addr_t  : std_logic;
    signal slot_data_o  : std_logic_vector(7 downto 0);
    signal slot_data_t  : std_logic;
    signal slot_rwn_o   : std_logic;
    signal irq_oc, nmi_oc, rst_oc, dma_oc, exrom_oc, game_oc    : std_logic;
    
    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;

    -- IEC open drain
    signal iec_atn_o   : std_logic;
    signal iec_data_o  : std_logic;
    signal iec_clock_o : std_logic;
    signal iec_srq_o   : std_logic;
    
    -- Cassette
    signal c2n_read_in      : std_logic;
    signal c2n_write_in     : std_logic;
    signal c2n_read_out     : std_logic;
    signal c2n_write_out    : std_logic;
    signal c2n_read_en      : std_logic;
    signal c2n_write_en     : std_logic;
    signal c2n_sense_in     : std_logic;
    signal c2n_sense_out    : std_logic;
    signal c2n_motor_in     : std_logic;
    signal c2n_motor_out    : std_logic;

    -- Audio outputs
    signal audio_left  : signed(18 downto 0);
    signal audio_right : signed(18 downto 0);

    -- debug
    signal scale_cnt        : unsigned(11 downto 0) := X"000";
    attribute iob : string;
    attribute iob of scale_cnt : signal is "false";
begin
    reset_in <= '1' when BUTTON="000" else '0'; -- all 3 buttons pressed
    button_i <= not BUTTON;

    i_clkgen: entity work.s3a_clockgen
    port map (
        clk_50       => CLOCK,
        reset_in     => reset_in,

        dcm_lock     => dcm_lock,
        
        sys_clock    => sys_clock,    -- 50 MHz
        sys_reset    => sys_reset,
        sys_clock_2x => sys_clock_2x );

    i_logic: entity work.ultimate_logic_32
    generic map (
        g_version       => g_version,
        g_simulation    => false,
        g_clock_freq    => 50_000_000,
        g_baud_rate     => 115_200,
        g_timer_rate    => 200_000,
        g_icap          => true,
        g_uart          => true,
        g_drive_1541    => true,
        g_drive_1541_2  => g_dual_drive,
        g_hardware_gcr  => true,
        g_ram_expansion => true,
        g_extended_reu  => false,
        g_stereo_sid    => not g_dual_drive,
        g_hardware_iec  => true,
        g_iec_prog_tim  => false,
        g_c2n_streamer  => true,
        g_c2n_recorder  => true,
        g_cartridge     => true,
		g_command_intf  => true,
        g_drive_sound   => true,
        g_rtc_chip      => true,
        g_rtc_timer     => false,
        g_usb_host      => false,
        g_usb_host2     => true,
        g_spi_flash     => true,
        g_vic_copper    => false,
        g_video_overlay => false,
        g_sampler       => not g_dual_drive,
        g_analyzer      => false,
        g_profiler      => false )
    port map (
        -- globals
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
    
        ulpi_clock  => ulpi_clock,
        ulpi_reset  => ulpi_reset_i,
    
        -- slot side
        phi2_i      => PHI2,
        dotclk_i    => DOTCLK,
        rstn_i      => RSTn,
        rstn_o      => RSTn_out,
                                   
        slot_addr_o => slot_addr_o,
        slot_addr_i => SLOT_ADDR,
        slot_addr_t => slot_addr_t,
        slot_data_o => slot_data_o,
        slot_data_i => SLOT_DATA,
        slot_data_t => slot_data_t,
        rwn_i       => RWn,
        rwn_o       => slot_rwn_o,
        exromn_i    => EXROMn,
        exromn_o    => exrom_oc,
        gamen_i     => GAMEn,
        gamen_o     => game_oc,
        irqn_i      => IRQn,
        irqn_o      => irq_oc,
        nmin_i      => NMIn,
        nmin_o      => nmi_oc,
        ba_i        => BA,
        dman_o      => dma_oc,
        romhn_i     => ROMHn,
        romln_i     => ROMLn,
        io1n_i      => IO1n,
        io2n_i      => IO2n,
        
        -- local bus side
        mem_inhibit => memctrl_inhibit,
        --memctrl_idle    => memctrl_idle,
        mem_req     => mem_req,
        mem_resp    => mem_resp,
                 
        -- Audio outputs
        audio_left  => audio_left,
        audio_right => audio_right,
    
        -- IEC bus
        iec_reset_i => IEC_RESET,
        iec_atn_i   => IEC_ATN,
        iec_data_i  => IEC_DATA,
        iec_clock_i => IEC_CLOCK,
        iec_srq_i   => IEC_SRQ_IN,
                                  
        iec_reset_o => open,
        iec_atn_o   => iec_atn_o,
        iec_data_o  => iec_data_o,
        iec_clock_o => iec_clock_o,
        iec_srq_o   => iec_srq_o,
                                    
        DISK_ACTn   => DISK_ACTn, -- activity LED
        CART_LEDn   => CART_LEDn,
        SDACT_LEDn  => SDACT_LEDn,
        MOTOR_LEDn  => MOTOR_LEDn,
        
        -- Debug UART
        UART_TXD    => UART_TXD,
        UART_RXD    => UART_RXD,
        
        -- SD Card Interface
        SD_SSn      => SD_SSn,
        SD_CLK      => SD_CLK,
        SD_MOSI     => SD_MOSI,
        SD_MISO     => SD_MISO,
        SD_CARDDETn => SD_CARDDETn,
        SD_DATA     => SD_DATA,
        
        -- LED interface
        LED_CLK     => LED_CLK,
        LED_DATA    => LED_DATA,
        
        -- RTC Interface
        RTC_CS      => RTC_CS,
        RTC_SCK     => RTC_SCK,
        RTC_MOSI    => RTC_MOSI,
        RTC_MISO    => RTC_MISO,
    
        -- Flash Interface
        FLASH_CSn   => FLASH_CSn,
        FLASH_SCK   => FLASH_SCK,
        FLASH_MOSI  => FLASH_MOSI,
        FLASH_MISO  => FLASH_MISO,
    
        -- USB Interface (ULPI)
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        ULPI_DIR    => ULPI_DIR,
        ULPI_DATA   => ULPI_DATA,
    
        -- Cassette Interface
        c2n_read_in    => c2n_read_in, 
        c2n_write_in   => c2n_write_in, 
        c2n_read_out   => c2n_read_out, 
        c2n_write_out  => c2n_write_out, 
        c2n_read_en    => c2n_read_en, 
        c2n_write_en   => c2n_write_en, 
        c2n_sense_in   => c2n_sense_in, 
        c2n_sense_out  => c2n_sense_out, 
        c2n_motor_in   => c2n_motor_in, 
        c2n_motor_out  => c2n_motor_out, 
        
--        vid_clock   => sys_clock,
--        vid_reset   => sys_reset,
--        vid_h_count => X"000",
--        vid_v_count => X"000",
--        vid_active  => open,
--        vid_opaque  => open,
--        vid_data    => open,

        -- Buttons
        BUTTON      => button_i );

    irq_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => irq_oc, oc_out => IRQn);
    nmi_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => nmi_oc, oc_out => NMIn);
    dma_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => dma_oc, oc_out => DMAn);
    exr_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => exrom_oc, oc_out => EXROMn);
    gam_push: entity work.oc_pusher port map(clock => sys_clock, sig_in => game_oc, oc_out => GAMEn);

    SLOT_ADDR  <= slot_addr_o when slot_addr_t = '1' else (others => 'Z');
    SLOT_DATA  <= slot_data_o when slot_data_t = '1' else (others => 'Z');
    RWn        <= slot_rwn_o  when slot_addr_t = '1' else 'Z';
    RSTn       <= '0' when RSTn_out = '0' else 'Z';

    IEC_ATN    <= '0' when iec_atn_o   = '0' else 'Z';
    IEC_DATA   <= '0' when iec_data_o  = '0' else 'Z';
    IEC_CLOCK  <= '0' when iec_clock_o = '0' else 'Z';
    IEC_SRQ_IN <= '0' when iec_srq_o   = '0' else 'Z';

    -- Tape
    c2n_motor_in <= CAS_MOTOR;
    CAS_SENSE    <= '0' when c2n_sense_out = '1' else 'Z';
    c2n_sense_in <= CAS_SENSE;
    CAS_READ     <= c2n_read_out when c2n_read_en = '1' else 'Z';
    c2n_read_in  <= CAS_READ;
    CAS_WRITE    <= c2n_write_out when c2n_write_en = '1' else 'Z';
    c2n_write_in <= CAS_WRITE;

    i_mem_ctrl: entity work.ext_mem_ctrl_v5
    generic map (
        g_simulation => false )
    port map (
        clock       => sys_clock,
        clk_2x      => sys_clock_2x,
        reset       => sys_reset,
    
        inhibit     => memctrl_inhibit,
        is_idle     => open,
    
        req         => mem_req,
        resp        => mem_resp,
    
        SDRAM_CLK   => SDRAM_CLK,
        SDRAM_CKE   => SDRAM_CKE,
        SDRAM_CSn   => SDRAM_CSn,
        SDRAM_RASn  => SDRAM_RASn,
        SDRAM_CASn  => SDRAM_CASn,
        SDRAM_WEn   => SDRAM_WEn,
        SDRAM_DQM   => SDRAM_DQM,
    
        SDRAM_BA    => SDRAM_BA,
        SDRAM_A     => SDRAM_A,
        SDRAM_DQ    => SDRAM_DQ );

    process(ulpi_clock, reset_in)
    begin
        if rising_edge(ulpi_clock) then
            ulpi_reset_i <= sys_reset;
        end if;
        if reset_in='1' then
            ulpi_reset_i <= '1';
        end if;
    end process;

    process(ulpi_clock)
    begin
        if rising_edge(ulpi_clock) then
            scale_cnt <= scale_cnt + 1;
        end if;
    end process;

    ULPI_RESET <= ulpi_reset_i;

    i_pwm0: entity work.sigma_delta_dac --delta_sigma_2to5
    generic map (
        g_left_shift    => 0,
        g_invert        => true,
        g_use_mid_only  => false,
        g_width => audio_left'length )
    port map (
        clock   => sys_clock,
        reset   => sys_reset,
        
        dac_in  => audio_left,
    
        dac_out => PWM_OUT(0) );

    i_pwm1: entity work.sigma_delta_dac --delta_sigma_2to5
    generic map (
        g_left_shift    => 0,
        g_invert        => true,
        g_use_mid_only  => false,
        g_width => audio_right'length )
    port map (
        clock   => sys_clock,
        reset   => sys_reset,
        
        dac_in  => audio_right,
    
        dac_out => PWM_OUT(1) );
        
end structural;
