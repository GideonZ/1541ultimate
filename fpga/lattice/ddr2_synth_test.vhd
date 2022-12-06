library ieee;
use ieee.std_logic_1164.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;

entity ddr2_synth_test is
port (
    -- 50 MHz input
    ref_clock   : in  std_logic := '0';
    reset       : in  std_logic := '0';
    
    LED_ERRORn  : out std_logic;
    LED_OKn     : out std_logic;

    CLOCK_24    : out std_logic;
    CLOCK_AUDIO : out std_logic;
    
    signal io_req           : in t_io_req := c_io_req_init;
    signal io_resp          : out t_io_resp;

    SDRAM_CLK   : out std_logic;
    SDRAM_CLKn  : out std_logic;

    -- command group
    SDRAM_CKE   : out std_logic := '0'; -- bit 5
    SDRAM_ODT   : out std_logic := '0';
    SDRAM_CSn   : out std_logic := '1';
    SDRAM_RASn  : out std_logic := '1';
    SDRAM_CASn  : out std_logic := '1';
    SDRAM_WEn   : out std_logic := '1'; -- bit 0

    -- address group
    SDRAM_A     : out std_logic_vector(13 downto 0);
    SDRAM_BA    : out std_logic_vector(2 downto 0);

    -- data group
    SDRAM_DM    : inout std_logic := 'Z';
    SDRAM_DQ    : inout std_logic_vector(7 downto 0) := (others => 'Z');

    -- dqs
    SDRAM_DQS   : inout std_logic := 'Z');

end entity;

architecture fitme of ddr2_synth_test is
    signal mem_clock        : std_logic;
    signal sys_clock        : std_logic;
    signal sys_reset        : std_logic;
    signal pll_locked       : std_logic;
    signal ctrl_clock       : std_logic;
        
    signal mem_req          : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp         : t_mem_resp_32;
begin
    i_pll: entity work.pll1
    port map (
        CLKI   => ref_clock, -- 50 MHz
        CLKOP  => mem_clock,   -- 200 MHz
        CLKOS  => CLOCK_24,   -- 24 MHz
        CLKOS2 => CLOCK_AUDIO, -- 12.245 MHz (47.831 kHz sample rate)
        CLKOS3 => sys_clock, -- 50 MHz
        LOCK   => pll_locked );

    sys_reset <= not pll_locked when rising_edge(sys_clock);

    i_memctrl: entity work.ddr2_ctrl
    port map (
        mem_clock         => mem_clock,
        reset             => sys_reset,

        io_clock          => sys_clock,
        io_reset          => sys_reset,
        io_req            => io_req,
        io_resp           => io_resp,
        inhibit           => '0',
        is_idle           => open,
        mem_clock_half    => ctrl_clock,
        
        req               => mem_req,
        resp              => mem_resp,
        
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

    i_mem_test: entity work.mem_test
    port map (
        clock       => ctrl_clock,
        reset       => sys_reset,
        ready       => '1', -- only for synthesis test, otherwise only after calibration
        
        mem_req     => mem_req,
        mem_resp    => mem_resp,
        
        ok_n        => LED_OKn,
        error_n     => LED_ERRORn
    );
end architecture;
