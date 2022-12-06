
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.cart_slot_pkg.all;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.command_if_pkg.all;

library std;
use std.textio.all;

entity harness_logic_32 is
end entity;

architecture tb of harness_logic_32 is
    constant c_uart_divisor : natural := 10;

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

    signal slot_addr_o : std_logic_vector(15 downto 0);
    signal slot_addr_i : std_logic_vector(15 downto 0) := (others => '1');
    signal slot_addr_tl: std_logic;
    signal slot_addr_th: std_logic;
    signal slot_data_o : std_logic_vector(7 downto 0);
    signal slot_data_i : std_logic_vector(7 downto 0) := (others => '1');
    signal slot_data_t : std_logic;

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

    -- Parallel cable connection
    signal drv_via1_port_a_o    : std_logic_vector(7 downto 0);
    signal drv_via1_port_a_i    : std_logic_vector(7 downto 0);
    signal drv_via1_port_a_t    : std_logic_vector(7 downto 0);
    signal drv_via1_ca2_o       : std_logic;
    signal drv_via1_ca2_i       : std_logic;
    signal drv_via1_ca2_t       : std_logic;
    signal drv_via1_cb1_o       : std_logic;
    signal drv_via1_cb1_i       : std_logic;
    signal drv_via1_cb1_t       : std_logic;

    signal IEC_ATN     : std_logic := '1';
    signal IEC_DATA    : std_logic := '1';
    signal IEC_CLOCK   : std_logic := '1';
    signal IEC_RESET   : std_logic := '1';
    signal IEC_SRQ_IN  : std_logic := '1';

    signal iec_atn_o   : std_logic := '1';
    signal iec_data_o  : std_logic := '1';
    signal iec_clock_o : std_logic := '1';
    signal iec_reset_o : std_logic := '1';
    signal iec_srq_o   : std_logic := '1';

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
    signal SD_DATA     : std_logic_vector(2 downto 1) := "HH";
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
    signal ulpi_data_o      : std_logic_vector(7 downto 0);
    signal ulpi_data_t      : std_logic;

    signal sys_clock    : std_logic := '1';
    signal sys_reset    : std_logic := '1';
    signal sys_clock_2x : std_logic := '1';

    signal rx_char      : std_logic_vector(7 downto 0);
    signal rx_char_d    : std_logic_vector(7 downto 0);
    signal rx_ack       : std_logic;
    signal tx_char      : std_logic_vector(7 downto 0) := X"00";
    signal tx_done      : std_logic;
    signal do_tx        : std_logic := '0';
    
    -- memory controller interconnect
    signal mem_inhibit      : std_logic := '0';
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;
    signal io_req           : t_io_req;
    signal io_resp          : t_io_resp;

    signal CLOCK_50        : std_logic := '0';
    signal SDRAM_CLK       : std_logic;
    signal SDRAM_CKE       : std_logic;
    signal SDRAM_CSn       : std_logic := '1';
    signal SDRAM_RASn      : std_logic := '1';
    signal SDRAM_CASn      : std_logic := '1';
    signal SDRAM_WEn       : std_logic := '1';
    signal SDRAM_DQM       : std_logic := '0';
    signal SDRAM_A         : std_logic_vector(12 downto 0);
    signal SDRAM_BA        : std_logic_vector(1 downto 0);
    signal SDRAM_DQ        : std_logic_vector(7 downto 0) := (others => 'Z');
begin
    sys_clock <= not sys_clock after 10 ns;
    sys_clock_2x <= not sys_clock_2x after 5 ns;
    sys_reset <= '1', '0' after 100 ns;

    mut: entity work.ultimate_logic_32
    generic map (
        g_version       => X"02",
        g_simulation    => true,
        g_clock_freq    => 50_000_000,
        g_baud_rate     => 1_000_000,
        g_timer_rate    => 200_000,
        g_boot_rom      => false,
        g_video_overlay => false,
        g_icap          => false,
        g_uart          => true,
        g_drive_1541    => true,
        g_drive_1541_2  => false,
        g_hardware_gcr  => true,
        g_cartridge     => true,
        g_command_intf  => true,
        g_stereo_sid    => false,
        g_ram_expansion => true,
        g_extended_reu  => false,
        g_hardware_iec  => false,
        g_c2n_streamer  => false,
        g_c2n_recorder  => false,
        g_drive_sound   => true,
        g_rtc_chip      => false,
        g_rtc_timer     => false,
        g_usb_host      => false,
        g_usb_host2     => true,
        g_spi_flash     => true,
        g_vic_copper    => false,
        g_sampler       => false,
        g_profiler      => true,
        g_analyzer      => false )
    port map (
        sys_clock       => sys_clock,
        sys_reset       => sys_reset,
        ulpi_clock      => ulpi_clock,
        ulpi_reset      => ulpi_reset,

        phi2_i            => PHI2,
        dotclk_i          => DOTCLK,
        rstn_o            => RSTn,
        rstn_i            => RSTn,
        slot_addr_o       => slot_addr_o,
        slot_addr_i       => SLOT_ADDR,
        slot_addr_tl      => slot_addr_tl,
        slot_addr_th      => slot_addr_th,
        slot_data_o       => slot_data_o,
        slot_data_i       => SLOT_DATA,
        slot_data_t       => slot_data_t,
        rwn_i             => RWn,
        rwn_o             => RWn,
        exromn_i          => EXROMn,
        exromn_o          => EXROMn,
        gamen_i           => GAMEn,
        gamen_o           => GAMEn,
        irqn_i            => IRQn,
        irqn_o            => IRQn,
        nmin_i            => NMIn,
        nmin_o            => NMIn,
        ba_i              => BA,
        dman_o            => DMAn,
        romhn_i           => ROMHn,
        romln_i           => ROMLn,
        io1n_i            => IO1n,
        io2n_i            => IO2n,

        -- Parallel cable pins
        drv_via1_port_a_o   => drv_via1_port_a_o,
        drv_via1_port_a_i   => drv_via1_port_a_i,
        drv_via1_port_a_t   => drv_via1_port_a_t,
        drv_via1_ca2_o      => drv_via1_ca2_o,
        drv_via1_ca2_i      => drv_via1_ca2_i,
        drv_via1_ca2_t      => drv_via1_ca2_t,
        drv_via1_cb1_o      => drv_via1_cb1_o,
        drv_via1_cb1_i      => drv_via1_cb1_i,
        drv_via1_cb1_t      => drv_via1_cb1_t,

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

        mem_inhibit     => mem_inhibit,
        mem_req         => mem_req,
        mem_resp        => mem_resp,
        iec_reset_i     => IEC_RESET,
        iec_atn_i       => IEC_ATN,
        iec_data_i      => IEC_DATA,
        iec_clock_i     => IEC_CLOCK,
        iec_srq_i       => IEC_SRQ_IN,
        iec_reset_o     => iec_reset_o,
        iec_atn_o       => iec_atn_o,
        iec_data_o      => iec_data_o,
        iec_clock_o     => iec_clock_o,
        iec_srq_o       => iec_srq_o,
        BUTTON          => BUTTON,
        DISK_ACTn       => DISK_ACTn,
        CART_LEDn       => CART_LEDn,
        SDACT_LEDn      => SDACT_LEDn,
        MOTOR_LEDn      => MOTOR_LEDn,
        UART_TXD        => UART_TXD,
        UART_RXD        => UART_RXD,
        SD_SSn          => SD_SSn,
        SD_CLK          => SD_CLK,
        SD_MOSI         => SD_MOSI,
        SD_MISO         => SD_MISO,
        SD_CARDDETn     => SD_CARDDETn,
        SD_DATA         => SD_DATA,
        RTC_CS          => RTC_CS,
        RTC_SCK         => RTC_SCK,
        RTC_MOSI        => RTC_MOSI,
        RTC_MISO        => RTC_MISO,
        FLASH_CSn       => FLASH_CSn,
        FLASH_SCK       => FLASH_SCK,
        FLASH_MOSI      => FLASH_MOSI,
        FLASH_MISO      => FLASH_MISO,
        ULPI_NXT        => ULPI_NXT,
        ULPI_STP        => ULPI_STP,
        ULPI_DIR        => ULPI_DIR,
        ULPI_DATA_O     => ulpi_data_o,
        ULPI_DATA_I     => ULPI_DATA,
        ULPI_DATA_T     => ulpi_data_t,
        ext_io_req      => io_req,
        ext_io_resp     => io_resp );
    
    ULPI_DATA <= ulpi_data_o when ulpi_data_t = '1' else "ZZZZZZZZ";

    SLOT_ADDR(15 downto 12) <= slot_addr_o(15 downto 12) when slot_addr_th = '1' else (others => 'H');
    SLOT_ADDR(11 downto 00) <= slot_addr_o(11 downto 00) when slot_addr_tl = '1' else (others => 'H');
    SLOT_DATA               <= slot_data_o when slot_data_t = '1' else (others => 'H');

    -- Parallel cable not implemented. This is the way to stub it...
    drv_via1_port_a_i <= drv_via1_port_a_o or not drv_via1_port_a_t;
    drv_via1_ca2_i    <= drv_via1_ca2_o    or not drv_via1_ca2_t;
    drv_via1_cb1_i    <= drv_via1_cb1_o    or not drv_via1_cb1_t;

    i_mem_ctrl: entity work.ext_mem_ctrl_v5
    generic map (
        g_simulation => false )
    port map (
        clock       => sys_clock,
        clk_2x      => sys_clock_2x,
        reset       => sys_reset,
    
        inhibit     => mem_inhibit,
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


	ULPI_CLOCK <= not ULPI_CLOCK after 8.333 ns; -- 60 MHz
    ULPI_RESET <= '1', '0' after 100 ns;

	PHI2  <= not PHI2 after 507.5 ns; -- 0.98525 MHz
    RSTn  <= '0', 'H' after 6 us, '0' after 100 us, 'H' after 105 us;

    i_io_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => sys_clock,
    
        req         => io_req,
        resp        => io_resp );

    SLOT_DATA <= (others => 'H');
    ROMHn     <= '1';
    ROMLn     <= not PHI2 after 50 ns;
    IO1n      <= '1';
    IO2n      <= '1';

    process
    begin
        SLOT_ADDR <= X"7F00";
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

    ram: entity work.dram_8
    generic map(
        g_cas_latency => 3,
        g_burst_len_r => 4,
        g_burst_len_w => 4,
        g_column_bits => 10,
        g_row_bits    => 13,
        g_bank_bits   => 2
    )
    port map(
        CLK           => SDRAM_CLK,
        CKE           => SDRAM_CKE,
        A             => SDRAM_A,
        BA            => SDRAM_BA,
        CSn           => SDRAM_CSn,
        RASn          => SDRAM_RASn,
        CASn          => SDRAM_CASn,
        WEn           => SDRAM_WEn,
        DQM           => SDRAM_DQM,
        DQ            => SDRAM_DQ
    );

--    i_ulpi_phy: entity work.ulpi_master_bfm
--    generic map (
--        g_given_name    => "device" )
--
--    port map (
--        clock           => ULPI_CLOCK,
--        reset           => ULPI_RESET,
--        ulpi_nxt        => ulpi_nxt,
--        ulpi_stp        => ulpi_stp,
--        ulpi_dir        => ulpi_dir,
--        ulpi_data       => ulpi_data );
--
--    i_device: entity work.usb_device_model;

    i_rx: entity work.rx
    generic map (c_uart_divisor)
    port map (
        clk     => sys_clock,
        reset   => sys_reset,
    
        tick    => '1',
        rxd     => UART_TXD,
        
        rxchar  => rx_char,
        rx_ack  => rx_ack );

    i_tx: entity work.tx
    generic map (c_uart_divisor)
    port map (
        clk     => sys_clock,
        reset   => sys_reset,
    
        tick    => '1',
        dotx    => do_tx,
        txchar  => tx_char,
        done    => tx_done,

        txd     => UART_RXD );

    process(sys_clock)
        variable s    : line;
        variable char : character;
    begin
        if rising_edge(sys_clock) then
            if rx_ack='1' then
                rx_char_d <= rx_char;
                char := character'val(to_integer(unsigned(rx_char)));
                if rx_char = X"0D" then
                    -- Ignore character 13
                elsif rx_char = X"0A" then
                    -- Writeline on character 10 (newline)
                    writeline(output, s);
                else
                    -- Write to buffer
                    write(s, char);
                end if;
            end if;
            if mem_resp.rack = '1' and mem_req.address < 16 then
                report "Access to address " & integer'image(to_integer(mem_req.address)) severity error;
            end if;
        end if;
    end process;
    
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
        --io_write(io, X"42002", X"42"); 
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
