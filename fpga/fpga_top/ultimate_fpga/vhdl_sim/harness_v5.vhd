
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.tl_flat_memory_model_pkg.all;
use work.mem_bus_pkg.all;
use work.cart_slot_pkg.all;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.command_if_pkg.all;

entity harness_v5 is
end harness_v5;

architecture tb of harness_v5 is
    constant c_uart_divisor : natural := 434;

    signal PHI2        : std_logic := '0';
    signal RSTn        : std_logic := 'H';
    signal DOTCLK      : std_logic := '1';
    signal BUFFER_ENn  : std_logic := '1';
    signal BA          : std_logic := '0';
    signal DMAn        : std_logic := '1';
    signal EXROMn      : std_logic;
    signal GAMEn       : std_logic;
    signal ROMHn       : std_logic := '1';
    signal ROMLn       : std_logic := '1';
    signal IO1n        : std_logic := '1';
    signal IO2n        : std_logic := '1';
    signal IRQn        : std_logic := '1';
    signal NMIn        : std_logic := '1';
    signal SDRAM_CSn   : std_logic;
    signal SDRAM_RASn  : std_logic;
    signal SDRAM_CASn  : std_logic;
    signal SDRAM_WEn   : std_logic;
    signal SDRAM_CKE   : std_logic;
    signal SDRAM_CLK   : std_logic;
    signal SDRAM_DQM   : std_logic;
    signal LB_ADDR     : std_logic_vector(14 downto 0);
    signal LB_DATA     : std_logic_vector(7 downto 0) := X"00";

    signal logic_SDRAM_CSn   : std_logic;
    signal logic_SDRAM_RASn  : std_logic;
    signal logic_SDRAM_CASn  : std_logic;
    signal logic_SDRAM_WEn   : std_logic;
    signal logic_SDRAM_CKE   : std_logic;
    signal logic_SDRAM_CLK   : std_logic;
    signal logic_SDRAM_DQM   : std_logic;
    signal logic_LB_ADDR     : std_logic_vector(14 downto 0);

    signal PWM_OUT     : std_logic_vector(1 downto 0);
    signal IEC_ATN     : std_logic := '1';
    signal IEC_DATA    : std_logic := '1';
    signal IEC_CLOCK   : std_logic := '1';
    signal IEC_RESET   : std_logic := '1';
    signal IEC_SRQ_IN  : std_logic := '1';
    signal DISK_ACTn   : std_logic; -- activity LED
    signal CART_LEDn   : std_logic;
    signal SDACT_LEDn  : std_logic;
    signal MOTOR_LEDn  : std_logic;
    signal UART_TXD    : std_logic;
    signal UART_RXD    : std_logic := '1';
    signal SD_SSn      : std_logic;
    signal SD_CLK      : std_logic;
    signal SD_MOSI     : std_logic;
    signal SD_MISO     : std_logic := '1';
    signal SD_WP       : std_logic := '1';
    signal SD_CARDDETn : std_logic := '1';
    signal BUTTON      : std_logic_vector(2 downto 0) := "000";
    signal SLOT_ADDR   : std_logic_vector(15 downto 0);
    signal SLOT_DATA   : std_logic_vector(7 downto 0);
    signal RWn         : std_logic := '1';
    signal CAS_MOTOR   : std_logic := '1';
    signal CAS_SENSE   : std_logic := '0';
    signal CAS_READ    : std_logic := '0';
    signal CAS_WRITE   : std_logic := '0';
    signal RTC_CS      : std_logic;
    signal RTC_SCK     : std_logic;
    signal RTC_MOSI    : std_logic;
    signal RTC_MISO    : std_logic := '1';
    signal FLASH_CSn   : std_logic;
    signal FLASH_SCK   : std_logic;
    signal FLASH_MOSI  : std_logic;
    signal FLASH_MISO  : std_logic := '1';
    signal ULPI_CLOCK  : std_logic := '0';
    signal ULPI_RESET  : std_logic := '0';
    signal ULPI_NXT    : std_logic := '0';
    signal ULPI_STP    : std_logic;
    signal ULPI_DIR    : std_logic := '0';
    signal ULPI_DATA   : std_logic_vector(7 downto 0) := (others => 'H');

    signal sys_clock    : std_logic := '0';
    signal sys_reset    : std_logic := '0';
    signal sys_shifted  : std_logic := '0';

    signal rx_char      : std_logic_vector(7 downto 0);
    signal rx_char_d    : std_logic_vector(7 downto 0);
    signal rx_ack       : std_logic;
    signal tx_char      : std_logic_vector(7 downto 0) := X"00";
    signal tx_done      : std_logic;
    signal do_tx        : std_logic := '0';
    
    shared variable dram : h_mem_object;
    shared variable ram  : h_mem_object;
--    shared variable rom  : h_mem_object;
--    shared variable bram : h_mem_object;

    -- memory controller interconnect
    signal memctrl_inhibit  : std_logic := '0';
    signal mem_req          : t_mem_req;
    signal mem_resp         : t_mem_resp;
    signal mem_req_cached   : t_mem_burst_req;
    signal mem_resp_cached  : t_mem_burst_resp;
    signal io_req           : t_io_req;
    signal io_resp          : t_io_resp;

    -- cache monitoring
    signal hit_count   : unsigned(31 downto 0);
    signal miss_count  : unsigned(31 downto 0);
    signal hit_ratio   : real := 0.0;
begin
    mut: entity work.ultimate_logic
    generic map (
        g_simulation    => true,
        g_uart          => true,
        g_drive_1541    => true,
        g_drive_1541_2  => true,
        g_hardware_gcr  => true,
        g_cartridge     => true,
        g_command_intf  => true,
        g_stereo_sid    => true,
        g_ram_expansion => true,
        g_extended_reu  => true,
        g_hardware_iec  => true,
        g_iec_prog_tim  => true,
        g_c2n_streamer  => true,
        g_c2n_recorder  => true,
        g_drive_sound   => true,
        g_rtc_chip      => true,
        g_rtc_timer     => true,
        g_usb_host      => true,
        g_spi_flash     => true )
    port map (
        sys_clock   => sys_clock,
        sys_reset   => sys_reset,
        
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
         
        -- io bus for simulation
        sim_io_req  => io_req,
        sim_io_resp => io_resp,

        -- PWM outputs (for audio)
        PWM_OUT     => PWM_OUT,
    
        -- IEC bus
        IEC_ATN     => IEC_ATN,
        IEC_DATA    => IEC_DATA,
        IEC_CLOCK   => IEC_CLOCK,
        IEC_RESET   => IEC_RESET,
        IEC_SRQ_IN  => IEC_SRQ_IN,
        
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
        
        -- Cassette Interface
        CAS_MOTOR   => CAS_MOTOR,
        CAS_SENSE   => CAS_SENSE,
        CAS_READ    => CAS_READ,
        CAS_WRITE   => CAS_WRITE,

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
        ULPI_CLOCK  => ULPI_CLOCK,
        ULPI_RESET  => ULPI_RESET,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        ULPI_DIR    => ULPI_DIR,
        ULPI_DATA   => ULPI_DATA,
    
        -- Buttons
        BUTTON      => BUTTON );

    i_cache: entity work.dm_cache
    port map (
        clock       => sys_clock,
        reset       => sys_reset,
        
        client_req  => mem_req,
        client_resp => mem_resp,
        
        mem_req     => mem_req_cached,
        mem_resp    => mem_resp_cached,
        
        hit_count   => hit_count,
        miss_count  => miss_count );

    hit_ratio <= real(to_integer(hit_count)) / real(to_integer(miss_count) + to_integer(hit_count) + 1);

	i_memctrl: entity work.ext_mem_ctrl_v5_sdr
    generic map (
        g_simulation => true,
    	A_Width	     => 15 )
		
    port map (
        clock       => sys_clock,
        clk_shifted => sys_shifted,
        reset       => sys_reset,

        inhibit     => '0', --memctrl_inhibit,
        is_idle     => open, --memctrl_idle,
        
        req         => mem_req_cached,
        resp        => mem_resp_cached,
        
		SDRAM_CSn   => logic_SDRAM_CSn,	
	    SDRAM_RASn  => logic_SDRAM_RASn,
	    SDRAM_CASn  => logic_SDRAM_CASn,
	    SDRAM_WEn   => logic_SDRAM_WEn,
		SDRAM_CKE	=> logic_SDRAM_CKE,
		SDRAM_CLK	=> logic_SDRAM_CLK,
        MEM_A       => logic_LB_ADDR,

        MEM_D       => LB_DATA );

    -- clock to out.. for data it is inside of the memory controller, because it's bidirectional
    SDRAM_CSn    <= transport logic_SDRAM_CSn  after 4.9 ns; 
    SDRAM_RASn   <= transport logic_SDRAM_RASn after 4.9 ns; 
    SDRAM_CASn   <= transport logic_SDRAM_CASn after 4.9 ns;
    SDRAM_WEn    <= transport logic_SDRAM_WEn  after 4.9 ns;
    SDRAM_CKE    <= transport logic_SDRAM_CKE  after 4.9 ns;
    SDRAM_CLK    <= transport logic_SDRAM_CLK  after 4.9 ns;
    LB_ADDR      <= transport logic_LB_ADDR    after 4.9 ns; 

	sys_clock <= not sys_clock after 10 ns; -- 50 MHz
    sys_reset <= '1', '0' after 100 ns;
    sys_shifted <= transport sys_clock after 15 ns; -- 270 degrees

	ULPI_CLOCK <= not ULPI_CLOCK after 8.333 ns; -- 60 MHz
    ULPI_RESET <= '1', '0' after 100 ns;

	PHI2  <= not PHI2 after 507.5 ns; -- 0.98525 MHz
    RSTn  <= '0', 'H' after 6 us, '0' after 100 us, 'H' after 105 us;
    
    i_ulpi_phy: entity work.ulpi_phy_bfm
    generic map (
        g_rx_interval => 100000 )
    port map (
        clock       => ULPI_CLOCK,
        reset       => ULPI_RESET,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP );

    i_io_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => sys_clock,
    
        req         => io_req,
        resp        => io_resp );

	process
	begin
        bind_mem_model("intram", ram);
        bind_mem_model("dram", dram);

        load_memory("../../software/1st_boot/result/1st_boot.bin", ram, X"00000000");
        -- 1st boot will try to load the 2nd bootloader and application from flash. In simulation this is a cumbersome
        -- process. It would work with a good model of the serial spi flash, but since it is not included in the public
        -- archive, you need to create a special boot image that just jumps to 0x20000 and load the application here to dram:
        load_memory("../../software/ultimate/result/ultimate.bin", dram, X"00020000");

		wait;
	end process;

    SLOT_DATA <= (others => 'H');
    ROMHn     <= '1';
    ROMLn     <= not PHI2 after 50 ns;
    IO1n      <= '1';
    IO2n      <= '1';

    process
    begin
        SLOT_ADDR <= X"D400";
        RWn       <= '1';
        while true loop
            wait until PHI2 = '0';
            --SLOT_ADDR(8 downto 0) <= std_logic_vector(unsigned(SLOT_ADDR(8 downto 0)) + 1);
            SLOT_ADDR <= std_logic_vector(unsigned(SLOT_ADDR) + 1);
            RWn <= '1';
            wait until PHI2 = '0';
            RWn <= '0';
        end loop;
    end process;

	process
	begin
		BA <= '1';
		for i in 0 to 100 loop
			wait until PHI2='0';
		end loop;
		BA <= '0';
		for i in 0 to 10 loop
			wait until PHI2='0';
		end loop;
	end process;

    dram_bfm: entity work.dram_model_8
    generic map(
        g_given_name  => "dram",
        g_cas_latency => 2,
        g_burst_len_r => 4,
        g_burst_len_w => 4,
        g_column_bits => 10,
        g_row_bits    => 13,
        g_bank_bits   => 2 )
    port map (
        CLK  => SDRAM_CLK,
        CKE  => SDRAM_CKE,
        A    => LB_ADDR(12 downto 0),
        BA   => LB_ADDR(14 downto 13),
        CSn  => SDRAM_CSn,
        RASn => SDRAM_RASn,
        CASn => SDRAM_CASn,
        WEn  => SDRAM_WEn,
        DQM  => SDRAM_DQM, 
    
        DQ   => LB_DATA);

    i_rx: entity work.rx
    generic map (c_uart_divisor)
    port map (
        clk     => sys_clock,
        reset   => sys_reset,
    
        rxd     => UART_TXD,
        
        rxchar  => rx_char,
        rx_ack  => rx_ack );

    i_tx: entity work.tx
    generic map (c_uart_divisor)
    port map (
        clk     => sys_clock,
        reset   => sys_reset,
    
        dotx    => do_tx,
        txchar  => tx_char,
        done    => tx_done,

        txd     => UART_RXD );

    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            if rx_ack='1' then
                rx_char_d <= rx_char;
            end if;
        end if;
    end process;
    
--     procedure register_io_bus_bfm(named : string; variable pntr: inout p_io_bus_bfm_object);
--     procedure bind_io_bus_bfm(named : string; variable pntr: inout p_io_bus_bfm_object);
--     procedure io_read(variable io : inout p_io_bus_bfm_object; addr : unsigned; data : out std_logic_vector(7 downto 0));
--     procedure io_write(variable io : inout p_io_bus_bfm_object; addr : unsigned; data : std_logic_vector(7 downto 0));

--    constant c_cart_c64_mode            : unsigned(3 downto 0) := X"0";
--    constant c_cart_c64_stop            : unsigned(3 downto 0) := X"1";
--    constant c_cart_c64_stop_mode       : unsigned(3 downto 0) := X"2";
--    constant c_cart_c64_clock_detect    : unsigned(3 downto 0) := X"3";
--    constant c_cart_cartridge_rom_base  : unsigned(3 downto 0) := X"4";
--    constant c_cart_cartridge_type      : unsigned(3 downto 0) := X"5";
--    constant c_cart_cartridge_kill      : unsigned(3 downto 0) := X"6";
--    constant c_cart_reu_enable          : unsigned(3 downto 0) := X"8";
--    constant c_cart_reu_size            : unsigned(3 downto 0) := X"9";
--	constant c_cart_swap_buttons		: unsigned(3 downto 0) := X"A";
--    constant c_cart_ethernet_enable     : unsigned(3 downto 0) := X"F";
    process
        variable io : p_io_bus_bfm_object;
    begin
        wait until sys_reset='0';
        wait until sys_clock='1';
        bind_io_bus_bfm("io_bfm", io);
        io_write(io, X"40000" + c_cart_c64_mode, X"04"); -- reset
        io_write(io, X"40000" + c_cart_cartridge_type, X"06"); -- retro
        io_write(io, X"40000" + c_cart_c64_mode, X"08"); -- unreset
        io_write(io, X"44000" + c_cif_io_slot_base, X"7E"); 
        io_write(io, X"44000" + c_cif_io_slot_enable, X"01"); 
        wait for 6 us;
        wait until sys_clock='1';
        io_write(io, X"42002", X"42"); 
        wait;
    end process;

    process
        procedure send_char(i: std_logic_vector(7 downto 0)) is
        begin
            if tx_done /= '1' then
                wait until tx_done = '1';
            end if;
            wait until sys_clock='1';
            tx_char <= i;
            do_tx   <= '1';
            wait until tx_done = '0';
            wait until sys_clock='1';
            do_tx   <= '0';
        end procedure;        

        procedure send_string(i : string) is
            variable b : std_logic_vector(7 downto 0);
        begin
            for n in i'range loop
                b := std_logic_vector(to_unsigned(character'pos(i(n)), 8));
                send_char(b);
            end loop;
            send_char(X"0d");
            send_char(X"0a");
        end procedure;

    begin
        wait for 2 ms;

        --send_string("wd 4005000 12345678");
        send_string("run");
--        send_string("m 100000");
--        send_string("w 400000F 4");
        wait;
    end process;
    
    -- check timing data
    process(PHI2)
    begin
        if falling_edge(PHI2) then
            assert SLOT_DATA'last_event >= 189 ns
                report "Timing error on C64 bus."
                severity error;
        end if;
    end process;
end tb;
