
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity video_fpga is
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
    BA          : in    std_logic; -- IBUF only
    DMAn        : out   std_logic;
    
    EXROMn      : inout std_logic;
    GAMEn       : inout std_logic;
    
    ROMHn       : in    std_logic; -- IBUF ONLY
    ROMLn       : out   std_logic;
    IO1n        : in    std_logic; -- IBUF ONLY
    IO2n        : out   std_logic;

    IRQn        : out   std_logic;
    NMIn        : out   std_logic;
    
    -- local bus side
    
    LB_ADDR     : out   std_logic_vector(21 downto 0); -- 4M linear space
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
	
	-- USB
	USB_IOP		: inout std_logic;
	USB_ION		: inout std_logic;
	USB_SEP		: in    std_logic;
	USB_SEN		: in    std_logic;
	USB_DET     : inout std_logic;

    -- SD Card Interface
    SD_SSn      : out   std_logic;
    SD_CLK      : out   std_logic;
    SD_MOSI     : out   std_logic;
    SD_MISO     : in    std_logic;
    SD_WP       : in    std_logic;
    SD_CARDDETn : in    std_logic;
    
    -- 1-wire Interface
    ONE_WIRE    : inout std_logic;

    -- Ethernet Interface
    ETH_CLK     : inout std_logic := '0';
    ETH_IRQ     : in    std_logic := '0';
    ETH_CSn     : out   std_logic;
    ETH_CS      : out   std_logic;
    ETH_RST     : out   std_logic;
    
    -- Cassette Interface
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic;
    CAS_READ    : inout std_logic;
    CAS_WRITE   : inout std_logic;
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end video_fpga;


architecture structural of video_fpga is

    constant c_default_divider  : integer := 896;

--    attribute IFD_DELAY_VALUE : string;
--    attribute IFD_DELAY_VALUE of LB_DATA: signal is "0";

    signal reset_in     : std_logic;
    signal dcm_lock     : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
--    signal sys_clock_2x : std_logic;
    signal sys_shifted  : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);
    signal button_c     : std_logic_vector(2 downto 0);
    signal button_d     : std_logic_vector(2 downto 0);
    signal pix_clock    : std_logic;
    signal pix_clock_en : std_logic;
    signal pix_reset    : std_logic;
    signal pix_clock_pll: std_logic;
    
----------------------------------------------
    signal sync_in     : std_logic;
    signal h_sync      : std_logic;
    signal v_sync      : std_logic;

    signal pll_clock   : std_logic := '0';
    signal n           : unsigned(11 downto 0) := to_unsigned(c_default_divider-1, 12);
    signal up, down    : std_logic;
    signal analog      : std_logic := 'Z';
    signal reference   : std_logic;
    signal mute        : std_logic;
    signal pulse_level : std_logic;
    signal pulse_enable: std_logic;
        
    type t_state is (idle, waiting);
    signal wait_count  : integer range 0 to 16383;
    signal state       : t_state;

----------------------------------------------
    signal div_count   : integer range 0 to 1599 := 0;
    signal toggle_15k  : std_logic := '0';
    signal toggle_25M  : std_logic := '0';
----------------------------------------------
    signal pixel_active : std_logic;
    signal pixel_data   : std_logic;
    signal sync_out_n   : std_logic;
        
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
        --sys_clock_2x => sys_clock_2x,

        pix_clock    => pix_clock,
        pix_reset    => pix_reset );

    ETH_CLK <= '0';
    
    i_clock_buf: BUFG port map (I => pll_clock, O => pix_clock_pll);

    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            button_c <= button_i;
            button_d <= button_c;
            case state is
            when idle =>
                wait_count <= 10000;
                if button_d(0)='1' and button_c(0)='0' then
                    n <= n + 5;
                    state <= waiting;
                elsif button_d(2)='1' and button_c(2)='0' then
                    n <= n - 4;
                    state <= waiting;
                elsif button_d(1)='1' and button_c(1)='0' then
                    n <= to_unsigned(c_default_divider-1, n'length);
                    state <= waiting;
                end if;

            when waiting =>
                if wait_count = 0 then
                    state <= idle;
                else
                    wait_count <= wait_count - 1;
                end if;

            when others =>
                null;
            end case;
            
            if sys_reset='1' then
                n <= to_unsigned(c_default_divider-1, n'length);
            end if;
        end if;
    end process;

    p_15khz: process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            toggle_25M <= not toggle_25M;
            if div_count = 0 then
                div_count <= 1599;
                toggle_15k <= not toggle_15k;
            else
                div_count <= div_count - 1;
            end if;
        end if;
    end process;


    i_sep: entity work.sync_separator
    generic map (
        g_clock_mhz => 50 )
    port map (
        clock       => sys_clock,
        
        sync_in     => sync_in,
        mute        => mute,
        
        h_sync      => h_sync,
        v_sync      => v_sync );
    
    i_phase: entity work.phase_detector
    port map (
        n           => n,
        pll_clock   => pll_clock,
        h_sync      => toggle_15k, --h_sync,
        reference   => reference,
        mute        => '0', --mute,
        
        up          => up,
        down        => down,
        pulse_level => pulse_level,
        pulse_enable=> pulse_enable,        
        analog      => analog );
    
    i_chargen: entity work.char_generator
    generic map (
        g_divider       => 11 )
    port map (
        clock           => pix_clock,
        reset           => pix_reset,
            
        io_req          => c_io_req_init,
        io_resp         => open,
    
        h_sync          => h_sync,
        v_sync          => v_sync,
        
        pixel_active    => pixel_active,
        pixel_data      => pixel_data,
        sync_out_n      => sync_out_n );


-------------------------------------------------------
    sync_in   <= IO1n;
    pll_clock <= DOTCLK;

    -- front row
    IRQn    <= reference;
    GAMEn   <= h_sync;
    EXROMn  <= v_sync;
    IO2n    <= not pulse_enable;
    ROMLn   <= pulse_level;
    DMAn    <= mute;    

    -- back row
    NMIn    <= toggle_15k;
    RSTn    <= toggle_25M;    

    SLOT_ADDR(15) <= '1'; --sync_in; --sync_out_n;
    SLOT_ADDR(14) <= pixel_active;
    SLOT_ADDR(13) <= pixel_data;
    
-------------------------------------------------------

    BUFFER_ENn <= '0';

    ETH_RST   <= sys_reset;
    ETH_CS    <= '1'; 

    -- tie offs
    SDRAM_DQM  <= '0';

	-- USB
	USB_IOP		<= USB_SEP;
	USB_ION		<= USB_SEN;
	USB_DET     <= 'Z';
    ONE_WIRE    <= 'Z';

end structural;
