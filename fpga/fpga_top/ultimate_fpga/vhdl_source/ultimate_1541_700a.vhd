
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity ultimate_1541_700a is
generic (
    g_version       : unsigned(7 downto 0) := X"F0" );
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
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));
    
end ultimate_1541_700a;


architecture structural of ultimate_1541_700a is

    attribute IFD_DELAY_VALUE : string;
    attribute IFD_DELAY_VALUE of LB_DATA: signal is "0";

    signal reset_in     : std_logic;
    signal dcm_lock     : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal sys_clock_2x : std_logic;
    signal sys_shifted  : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);
        
    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    
    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic;
    signal mem_req          : t_mem_req;
    signal mem_resp         : t_mem_resp;

    -- IEC open drain
    signal iec_atn_o   : std_logic;
    signal iec_data_o  : std_logic;
    signal iec_clock_o : std_logic;
    signal iec_srq_o   : std_logic;
    
    -- debug
    signal scale_cnt        : unsigned(11 downto 0) := X"000";
    attribute iob : string;
    attribute iob of scale_cnt : signal is "false";
begin
    reset_in <= '1' when BUTTON="000" else '0'; -- all 3 buttons pressed
    button_i <= not BUTTON;

    i_clkgen: entity work.s3e_clockgen
    port map (
        clk_50       => CLOCK,
        reset_in     => reset_in,

        dcm_lock     => dcm_lock,
        
        sys_clock    => sys_clock,    -- 50 MHz
        sys_reset    => sys_reset,
        sys_shifted  => sys_shifted,
--        sys_clock_2x => sys_clock_2x,

        eth_clock    => open );


    i_logic: entity work.ultimate_logic 
    generic map (
        g_version       => g_version,
        g_simulation    => false,
        g_clock_freq    => 50_000_000,
        g_baud_rate     => 115_200,
        g_timer_rate    => 200_000,
        g_icap          => true,
        g_uart          => true,
        g_drive_1541    => true,
        g_drive_1541_2  => true,
        g_hardware_gcr  => true,
        g_ram_expansion => true,
        g_extended_reu  => false,
        g_stereo_sid    => true,
        g_hardware_iec  => true,
        g_iec_prog_tim  => false,
        g_c2n_streamer  => true,
        g_c2n_recorder  => true,
        g_cartridge     => true,
		g_command_intf  => false,
        g_drive_sound   => true,
        g_rtc_chip      => true,
        g_rtc_timer     => true,
        g_usb_host      => true,
        g_spi_flash     => true,
        g_vic_copper    => false,
        g_video_overlay => false,
        g_sampler       => false,
        g_analyzer      => false )
    port map (
        -- globals
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
    
        ulpi_clock  => ulpi_clock,
        ulpi_reset  => ulpi_reset_i,
    
        -- slot side
        PHI2        => PHI2,
        DOTCLK      => DOTCLK,
        RSTn        => RSTn,
    
        BUFFER_ENn  => BUFFER_ENn,
                                   
        SLOT_ADDR   => SLOT_ADDR,
        SLOT_DATA   => SLOT_DATA,
        RWn         => RWn,
        BA          => BA,
        DMAn        => DMAn,
                                   
        EXROMn      => EXROMn,
        GAMEn       => GAMEn,
                                   
        ROMHn       => ROMHn,
        ROMLn       => ROMLn,
        IO1n        => IO1n,
        IO2n        => IO2n,
                                   
        IRQn        => IRQn,
        NMIn        => NMIn,
        
        -- local bus side
        mem_inhibit => memctrl_inhibit,
        --memctrl_idle    => memctrl_idle,
        mem_req     => mem_req,
        mem_resp    => mem_resp,
                 
        -- PWM outputs (for audio)
        PWM_OUT     => PWM_OUT,
    
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
        CAS_MOTOR   => CAS_MOTOR,
        CAS_SENSE   => CAS_SENSE,
        CAS_READ    => CAS_READ,
        CAS_WRITE   => CAS_WRITE,
        
        vid_clock   => sys_clock,
        vid_reset   => sys_reset,
        vid_h_count => X"000",
        vid_v_count => X"000",
        vid_active  => open,
        vid_opaque  => open,
        vid_data    => open,

        -- Buttons
        BUTTON      => button_i );

    IEC_ATN    <= '0' when iec_atn_o   = '0' else 'Z';
    IEC_DATA   <= '0' when iec_data_o  = '0' else 'Z';
    IEC_CLOCK  <= '0' when iec_clock_o = '0' else 'Z';
    IEC_SRQ_IN <= '0' when iec_srq_o   = '0' else 'Z';

	i_memctrl: entity work.ext_mem_ctrl_v4
    generic map (
        g_simulation => false,
    	A_Width	     => 15 )
		
    port map (
        clock       => sys_clock,
        clk_shifted => sys_shifted,
        reset       => sys_reset,

        inhibit     => memctrl_inhibit,
        is_idle     => open, --memctrl_idle,
        
        req         => mem_req,
        resp        => mem_resp,
        
		SDRAM_CSn   => SDRAM_CSn,	
	    SDRAM_RASn  => SDRAM_RASn,
	    SDRAM_CASn  => SDRAM_CASn,
	    SDRAM_WEn   => SDRAM_WEn,
		SDRAM_CKE	=> SDRAM_CKE,
		SDRAM_CLK	=> SDRAM_CLK,

        MEM_A       => LB_ADDR,
        MEM_D       => LB_DATA );

    -- tie offs
    SDRAM_DQM  <= '0';

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

end structural;
