-------------------------------------------------------------------------------
-- Title      : u2p_nios_ddr2
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@technolution.eu>
-------------------------------------------------------------------------------
-- Description: Toplevel with just the alt-mem phy. Testing and experimenting
--              with memory latency. 
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    
entity u2p_nios_ddr2 is
port (
    -- slot side
    SLOT_PHI2        : in    std_logic;
    SLOT_DOTCLK      : in    std_logic;
    SLOT_RSTn        : inout std_logic;
    SLOT_BUFFER_ENn  : out   std_logic;
    SLOT_ADDR        : inout std_logic_vector(15 downto 0);
    SLOT_DATA        : inout std_logic_vector(7 downto 0);
    SLOT_RWn         : inout std_logic;
    SLOT_BA          : in    std_logic;
    SLOT_DMAn        : out   std_logic;
    SLOT_EXROMn      : inout std_logic;
    SLOT_GAMEn       : inout std_logic;
    SLOT_ROMHn       : inout std_logic;
    SLOT_ROMLn       : inout std_logic;
    SLOT_IO1n        : inout std_logic;
    SLOT_IO2n        : inout std_logic;
    SLOT_IRQn        : inout std_logic;
    SLOT_NMIn        : inout std_logic;
    SLOT_VCC         : in    std_logic;
    
    -- memory
    SDRAM_A     : out   std_logic_vector(13 downto 0); -- DRAM A
    SDRAM_BA    : out   std_logic_vector(2 downto 0) := (others => '0');
    SDRAM_DQ    : inout std_logic_vector(7 downto 0);
    SDRAM_CSn   : out   std_logic;
    SDRAM_RASn  : out   std_logic;
    SDRAM_CASn  : out   std_logic;
    SDRAM_WEn   : out   std_logic;
    SDRAM_DM    : inout std_logic;
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

    -- IEC bus
    IEC_ATN     : inout std_logic;
    IEC_DATA    : inout std_logic;
    IEC_CLOCK   : inout std_logic;
    IEC_RESET   : in    std_logic;
    IEC_SRQ_IN  : inout std_logic;
    
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
    RMII_TX_DATA    : out std_logic_vector(1 downto 0);
    RMII_TX_EN      : out std_logic;

    MDIO_CLK    : out   std_logic := '0';
    MDIO_DATA   : inout std_logic := 'Z';

    -- Speaker data
    SPEAKER_DATA    : out std_logic := '0';
    SPEAKER_ENABLE  : out std_logic := '0';

    -- Debug UART
    UART_TXD    : out   std_logic;
    UART_RXD    : in    std_logic;
    
    -- I2C Interface for RTC, audio codec and usb hub
    I2C_SDA     : inout std_logic := 'Z';
    I2C_SCL     : inout std_logic := 'Z';
    I2C_SDA_18  : inout std_logic := 'Z';
    I2C_SCL_18  : inout std_logic := 'Z';

    -- Flash Interface
    FLASH_CSn   : out   std_logic;
    FLASH_SCK   : out   std_logic;
    FLASH_MOSI  : out   std_logic;
    FLASH_MISO  : in    std_logic;
    FLASH_SEL   : out   std_logic := '0';
    FLASH_SELCK : out   std_logic := '0';

    -- USB Interface (ULPI)
    ULPI_RESET  : out   std_logic;
    ULPI_CLOCK  : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;
    ULPI_DIR    : in    std_logic;
    ULPI_DATA   : inout std_logic_vector(7 downto 0);

    HUB_RESETn  : out   std_logic := '1';
    HUB_CLOCK   : out   std_logic := '0';

    -- Cassette Interface
    CAS_MOTOR   : in    std_logic := '0';
    CAS_SENSE   : inout std_logic := 'Z';
    CAS_READ    : inout std_logic := 'Z';
    CAS_WRITE   : inout std_logic := 'Z';
    
    -- Buttons
    BUTTON      : in    std_logic_vector(2 downto 0));

end entity;

architecture rtl of u2p_nios_ddr2 is

    component pll
        PORT
        (
            inclk0      : IN STD_LOGIC  := '0';
            c0          : OUT STD_LOGIC ;
            c1          : OUT STD_LOGIC ;
            locked      : OUT STD_LOGIC 
        );
    end component;

    component nios_solo is
        port (
            clk_clk            : in  std_logic                     := 'X';             -- clk
--            dram_waitrequest   : in  std_logic                     := 'X';             -- waitrequest
--            dram_readdata      : in  std_logic_vector(31 downto 0) := (others => 'X'); -- readdata
--            dram_readdatavalid : in  std_logic                     := 'X';             -- readdatavalid
--            dram_burstcount    : out std_logic_vector(0 downto 0);                     -- burstcount
--            dram_writedata     : out std_logic_vector(31 downto 0);                    -- writedata
--            dram_address       : out std_logic_vector(25 downto 0);                    -- address
--            dram_write         : out std_logic;                                        -- write
--            dram_read          : out std_logic;                                        -- read
--            dram_byteenable    : out std_logic_vector(3 downto 0);                     -- byteenable
--            dram_debugaccess   : out std_logic;                                        -- debugaccess
            io_ack             : in  std_logic                     := 'X';             -- ack
            io_rdata           : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
            io_read            : out std_logic;                                        -- read
            io_wdata           : out std_logic_vector(7 downto 0);                     -- wdata
            io_write           : out std_logic;                                        -- write
            io_address         : out std_logic_vector(19 downto 0);                    -- address
            io_irq             : in  std_logic                     := 'X';             -- irq
            io_u2p_ack         : in  std_logic                     := 'X';             -- ack
            io_u2p_rdata       : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- rdata
            io_u2p_read        : out std_logic;                                        -- read
            io_u2p_wdata       : out std_logic_vector(7 downto 0);                     -- wdata
            io_u2p_write       : out std_logic;                                        -- write
            io_u2p_address     : out std_logic_vector(19 downto 0);                    -- address
            io_u2p_irq         : in  std_logic                     := 'X';             -- irq
            mem_mem_req_address     : out std_logic_vector(25 downto 0);                    -- mem_req_address
            mem_mem_req_byte_en     : out std_logic_vector(3 downto 0);                     -- mem_req_byte_en
            mem_mem_req_read_writen : out std_logic;                                        -- mem_req_read_writen
            mem_mem_req_request     : out std_logic;                                        -- mem_req_request
            mem_mem_req_tag         : out std_logic_vector(7 downto 0);                     -- mem_req_tag
            mem_mem_req_wdata       : out std_logic_vector(31 downto 0);                    -- mem_req_wdata
            mem_mem_resp_dack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_dack_tag
            mem_mem_resp_data       : in  std_logic_vector(31 downto 0) := (others => 'X'); -- mem_resp_data
            mem_mem_resp_rack_tag   : in  std_logic_vector(7 downto 0)  := (others => 'X'); -- mem_resp_rack_tag
            reset_reset_n      : in  std_logic                     := 'X'              -- reset_n
        );
    end component nios_solo;

    signal por_n        : std_logic;
    signal por_count    : unsigned(23 downto 0) := (others => '0');
    signal led_n        : std_logic_vector(0 to 3);
    signal ref_reset    : std_logic;
        
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal sys_clock    : std_logic;
    signal sys_reset    : std_logic;
    signal sys_reset_n  : std_logic;
    signal eth_reset    : std_logic;
    signal button_i     : std_logic_vector(2 downto 0);

    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    signal io_u2p_req   : t_io_req;
    signal io_u2p_resp  : t_io_resp;
    signal io_req_new_io    : t_io_req;
    signal io_resp_new_io   : t_io_resp;
    signal io_req_remote    : t_io_req;
    signal io_resp_remote   : t_io_resp;
    signal io_req_ddr2      : t_io_req;
    signal io_resp_ddr2     : t_io_resp;
        
    signal mem_req      : t_mem_req_32;
    signal mem_resp     : t_mem_resp_32;

    signal dram_waitrequest   : std_logic                     := 'X';             -- waitrequest
    signal dram_readdata      : std_logic_vector(31 downto 0) := (others => 'X'); -- readdata
    signal dram_readdatavalid : std_logic                     := 'X';             -- readdatavalid
    signal dram_writedata     : std_logic_vector(31 downto 0);                    -- writedata
    signal dram_address       : std_logic_vector(25 downto 0);                    -- address
    signal dram_write         : std_logic;                                        -- write
    signal dram_read          : std_logic;                                        -- read
    signal dram_byteenable    : std_logic_vector(3 downto 0);                     -- byteenable

    -- miscellaneous interconnect
    signal ulpi_reset_i     : std_logic;
    signal reset_request_n  : std_logic := '1';
    signal is_idle          : std_logic;
    
begin
    process(RMII_REFCLK, reset_request_n)
    begin
        if reset_request_n = '0' then
            por_count <= (others => '0');
        elsif rising_edge(RMII_REFCLK) then
            if por_count = X"FFFFFF" then
                por_n <= '1';
            else
                por_n <= '0';
                por_count <= por_count + 1;
            end if;
        end if;
    end process;

    ref_reset <= not por_n;
    
    i_pll: pll port map (
        inclk0  => RMII_REFCLK, -- 50 MHz
        c0      => HUB_CLOCK, -- 24 MHz
        c1      => audio_clock, -- 12.245 MHz (47.831 kHz sample rate)
        locked  => open );

    i_audio_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => audio_clock,
        input       => not sys_reset_n,
        input_c     => audio_reset  );
    
    i_ulpi_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => ulpi_clock,
        input       => sys_reset,
        input_c     => ulpi_reset_i  );

    i_eth_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => RMII_REFCLK,
        input       => sys_reset,
        input_c     => eth_reset  );

    sys_reset_n <= not sys_reset;
    
    i_nios: nios_solo
    port map (
        clk_clk            => sys_clock,
        reset_reset_n      => sys_reset_n,

--        dram_waitrequest   => dram_waitrequest,
--        dram_readdata      => dram_readdata,
--        dram_readdatavalid => dram_readdatavalid,
--        dram_burstcount    => open,
--        dram_writedata     => dram_writedata,
--        dram_address       => dram_address,
--        dram_write         => dram_write,
--        dram_read          => dram_read,
--        dram_byteenable    => dram_byteenable,
--        dram_debugaccess   => open,

        io_ack             => io_resp.ack,
        io_rdata           => io_resp.data,
        io_read            => io_req.read,
        io_wdata           => io_req.data,
        io_write           => io_req.write,
        unsigned(io_address) => io_req.address,
        io_irq             => '0',

        io_u2p_ack         => io_u2p_resp.ack,
        io_u2p_rdata       => io_u2p_resp.data,
        io_u2p_read        => io_u2p_req.read,
        io_u2p_wdata       => io_u2p_req.data,
        io_u2p_write       => io_u2p_req.write,
        unsigned(io_u2p_address) => io_u2p_req.address,
        io_u2p_irq         => '0',
        
        unsigned(mem_mem_req_address) => mem_req.address,
        mem_mem_req_byte_en     => mem_req.byte_en,
        mem_mem_req_read_writen => mem_req.read_writen,
        mem_mem_req_request     => mem_req.request,
        mem_mem_req_tag         => mem_req.tag,
        mem_mem_req_wdata       => mem_req.data,
        mem_mem_resp_dack_tag   => mem_resp.dack_tag,
        mem_mem_resp_data       => mem_resp.data,
        mem_mem_resp_rack_tag   => mem_resp.rack_tag
    );

    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo => 8,
        g_range_hi => 9,
        g_ports    => 3
    )
    port map (
        clock      => sys_clock,
        req        => io_u2p_req,
        resp       => io_u2p_resp,
        reqs(0)    => io_req_new_io,
        reqs(1)    => io_req_ddr2,
        reqs(2)    => io_req_remote,
        resps(0)   => io_resp_new_io,
        resps(1)   => io_resp_ddr2,
        resps(2)   => io_resp_remote
    );
    
--    i_dram_bridge: entity work.avalon_to_mem32_bridge
--    port map (
--        clock             => sys_clock,
--        reset             => sys_reset,
--
--        avs_read          => dram_read,
--        avs_write         => dram_write,
--        avs_address       => dram_address,
--        avs_writedata     => dram_writedata,
--        avs_byteenable    => dram_byteenable,
--        avs_waitrequest   => dram_waitrequest,
--        avs_readdata      => dram_readdata,
--        avs_readdatavalid => dram_readdatavalid,
--
--        mem_req           => mem_req,
--        mem_resp          => mem_resp );

    i_memphy: entity work.ddr2_ctrl
    port map (
        ref_clock         => RMII_REFCLK,
        ref_reset         => ref_reset,
        sys_clock_o       => sys_clock,
        sys_reset_o       => sys_reset,
        clock             => sys_clock,
        reset             => sys_reset,
        io_req            => io_req_ddr2,
        io_resp           => io_resp_ddr2,
        inhibit           => '0',
        is_idle           => is_idle,

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
        SDRAM_BA          => SDRAM_BA(1 downto 0),
        SDRAM_DM          => SDRAM_DM,
        SDRAM_DQ          => SDRAM_DQ,
        SDRAM_DQS         => SDRAM_DQS
    );

    i_remote: entity work.update_io
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        io_req      => io_req_remote,
        io_resp     => io_resp_remote,
        flash_selck => FLASH_SELCK,
        flash_sel   => FLASH_SEL
    );

    i_u2p_io: entity work.u2p_io
    port map (
        clock      => sys_clock,
        reset      => sys_reset,
        io_req     => io_req_new_io,
        io_resp    => io_resp_new_io,
        mdc        => MDIO_CLK,
        mdio       => MDIO_DATA,
        i2c_scl    => I2C_SCL,
        i2c_sda    => I2C_SDA,
        speaker_en => SPEAKER_ENABLE,
        hub_reset_n=> HUB_RESETn
    );
    

    ETH_RESETn     <= '1';

    SLOT_ADDR   <= (others => 'Z');
    SLOT_DATA   <= (others => 'Z');

    -- top
    SLOT_DMAn        <= 'Z'; 
    SLOT_ROMLn       <= 'Z'; 
    SLOT_IO2n        <= 'Z'; 
    SLOT_EXROMn      <= 'Z'; 
    SLOT_GAMEn       <= 'Z'; 
    SLOT_IO1n        <= 'Z'; 
    SLOT_RWn         <= 'Z'; 
    SLOT_IRQn        <= 'Z'; 
    SLOT_NMIn        <= 'Z'; 
    SLOT_RSTn        <= 'Z'; 
    SLOT_ROMHn       <= 'Z'; 
    
    -- Cassette Interface
    CAS_SENSE   <= '0'; 
    CAS_READ    <= '0'; 
    CAS_WRITE   <= '0'; 

    LED_MOTORn <= sys_reset;
    LED_DISKn  <= is_idle;
    LED_CARTn  <= button_i(0) xor button_i(1) xor button_i(2);
    LED_SDACTn <= SLOT_BA xor SLOT_DOTCLK xor SLOT_PHI2 xor CAS_MOTOR xor SLOT_VCC;

    button_i <= not BUTTON;

    SLOT_BUFFER_ENn <= '1'; -- we don't connect to a C64
    
    -- Flash Interface
    FLASH_CSn    <= '1';
    FLASH_SCK    <= '1';
    FLASH_MOSI   <= '1';

    -- USB Interface (ULPI)
    ULPI_RESET <= por_n;
    ULPI_STP    <= '0';
    ULPI_DATA   <= (others => 'Z');

end architecture;
