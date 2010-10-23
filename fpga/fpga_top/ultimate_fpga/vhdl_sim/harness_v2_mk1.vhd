
library work;
use work.tl_flat_memory_model_pkg.all;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity harness_v2_mk1 is
end harness_v2_mk1;

architecture tb of harness_v2_mk1 is
    constant c_uart_divisor : natural := 434;

    signal PHI2        : std_logic := '0';
    signal RSTn        : std_logic := '1';
    signal DOTCLK      : std_logic := '1';
    signal BUFFER_ENn  : std_logic := '1';
    signal LB_ADDR     : std_logic_vector(21 downto 0);
    signal LB_DATA     : std_logic_vector(7 downto 0) := X"00";
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
    signal MEM_WEn     : std_logic;
    signal MEM_OEn     : std_logic;
    signal SDRAM_CSn   : std_logic;
    signal SDRAM_RASn  : std_logic;
    signal SDRAM_CASn  : std_logic;
    signal SDRAM_WEn   : std_logic;
    signal SDRAM_CKE   : std_logic;
    signal SDRAM_CLK   : std_logic;
    signal SDRAM_DQM   : std_logic;
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
    signal BUTTON      : std_logic_vector(2 downto 0) := "111";
    signal SLOT_ADDR   : std_logic_vector(15 downto 0);
    signal SLOT_DATA   : std_logic_vector(7 downto 0);
    signal RWn         : std_logic := '1';
    signal CAS_MOTOR   : std_logic := '1';
    signal CAS_SENSE   : std_logic := '0';
    signal CAS_READ    : std_logic := '0';
    signal CAS_WRITE   : std_logic := '0';
    signal ETH_CLK     : std_logic;
    signal ETH_RST     : std_logic;
    signal ETH_CSn     : std_logic;
    signal ETH_CS      : std_logic;
    signal FLASH_CSn   : std_logic;
    signal SRAM_CSn    : std_logic;
    signal ONE_WIRE    : std_logic := 'H';
    
    signal sys_clock    : std_logic := '0';
    signal sys_reset    : std_logic := '0';

    signal rx_char      : std_logic_vector(7 downto 0);
    signal rx_char_d    : std_logic_vector(7 downto 0);
    signal rx_ack       : std_logic;
    signal tx_char      : std_logic_vector(7 downto 0) := X"00";
    signal tx_done      : std_logic;
    signal do_tx        : std_logic := '0';
    
    shared variable dram : h_mem_object;
    shared variable ram  : h_mem_object;
    shared variable sram : h_mem_object;

--    shared variable bram : h_mem_object;

begin
    mut: entity work.ultimate_1541_250e
    generic map (
        g_simulation => true )
    port map (
        CLOCK       => sys_clock,
        
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
                                   
        LB_ADDR     => LB_ADDR,
        LB_DATA     => LB_DATA,
                                   
        FLASH_CSn   => FLASH_CSn,
        SRAM_CSn    => SRAM_CSn,
        
        MEM_WEn     => MEM_WEn,
        MEM_OEn     => MEM_OEn,
        
        SDRAM_CSn   => SDRAM_CSn,
        SDRAM_RASn  => SDRAM_RASn,
        SDRAM_CASn  => SDRAM_CASn,
        SDRAM_WEn   => SDRAM_WEn,
        SDRAM_CKE   => SDRAM_CKE,
        SDRAM_CLK   => SDRAM_CLK,
        SDRAM_DQM   => SDRAM_DQM,
         
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
        
    	-- USB
    	USB_IOP		=> open,
    	USB_ION		=> open,
    	USB_SEP		=> '1',
    	USB_SEN		=> '0',
    	USB_DET     => open,
    
        -- SD Card Interface
        SD_SSn      => SD_SSn,
        SD_CLK      => SD_CLK,
        SD_MOSI     => SD_MOSI,
        SD_MISO     => SD_MISO,
        SD_WP       => '0',
        SD_CARDDETn => SD_CARDDETn,
        
        -- Cassette Interface
        CAS_MOTOR   => CAS_MOTOR,
        CAS_SENSE   => CAS_SENSE,
        CAS_READ    => CAS_READ,
        CAS_WRITE   => CAS_WRITE,

        -- Ethernet Interface
        ETH_CLK     => ETH_CLK,
        ETH_IRQ     => '0',
        ETH_CSn     => ETH_CSn,
        ETH_CS      => ETH_CS,
        ETH_RST     => ETH_RST,

        ONE_WIRE    => ONE_WIRE,
        -- Buttons
        BUTTON      => BUTTON );

	sys_clock <= not sys_clock after 10 ns; -- 50 MHz
    sys_reset <= '1', '0' after 100 ns;
    
	PHI2  <= not PHI2 after 507.5 ns; -- 0.98525 MHz
    RSTn  <= '0', '1' after 6 us;
    
	process
	begin
        bind_mem_model("intram", ram);
        bind_mem_model("dram", dram);
        bind_mem_model("sram", sram);
        
        load_memory("../../software/1st_boot/result/1st_boot.bin", ram, X"00000000");
        load_memory("../../software/ultimate/result/ultimate_V1.bin", sram, X"00030000");

		wait;
	end process;

    SLOT_DATA <= (others => 'H');
    ROMHn     <= '1';
    ROMLn     <= not PHI2 after 50 ns;
    IO1n      <= '1';
    IO2n      <= '1';

    process
    begin
        SLOT_ADDR <= X"7FF0";
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

    sram_bfm: entity work.sram_model_8
    generic map("sram", 19, 10 ns)
    port map (LB_ADDR(18 downto 0), LB_DATA, SRAM_CSn, MEM_OEn, MEM_WEn);

    flash_bfm: entity work.sram_model_8
    generic map("flash", 21, 70 ns)
    port map (LB_ADDR(20 downto 0), LB_DATA, FLASH_CSn, MEM_OEn, '1');

    dram_bfm: entity work.dram_model_8
    generic map(
        g_given_name  => "dram",
        g_cas_latency => 2,
        g_burst_len_r => 1,
        g_burst_len_w => 1,
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

--    assert not (ADDRESS(18 downto 16)="011" and ADDRESS(15 downto 0)=X"86A0" and SRAM_CSn='0' and MEM_WEn='0')
--        report "writing to jump address."
--        severity failure;

--    sram: entity work.sram_model_8
--    generic map("sram", 19, 10 ns)
--    port map (LB_ADDR(18 downto 0), LB_DATA, SRAM_CSn, MEM_OEn, MEM_WEn);
--
--    flash: entity work.sram_model_8
--    generic map("flash", 21, 70 ns)
--    port map (LB_ADDR(20 downto 0), LB_DATA, FLASH_CSn, MEM_OEn, '1');

--    process(ETH_CS, ETH_CSn, LB_ADDR)
--    begin
--        if ETH_CS='1' and ETH_CSn='0' then
--            LB_DATA <= not LB_ADDR(7 downto 0) after 135 ns;
--        else
--            LB_DATA <= (others => 'Z') after 50 ns;
--        end if;  
--    end process;

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
