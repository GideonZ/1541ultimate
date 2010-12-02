library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity c1541_drive is
generic (
    g_audio_tag     : std_logic_vector(7 downto 0) := X"01";
    g_floppy_tag    : std_logic_vector(7 downto 0) := X"02";
    g_cpu_tag       : std_logic_vector(7 downto 0) := X"04";
    g_audio         : boolean := true;
    g_audio_div     : integer := 2222; -- 22500 Hz (from 50 MHz)
    g_audio_base    : unsigned(27 downto 0) := X"0030000";
    g_ram_base      : unsigned(27 downto 0) := X"0060000" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    drive_stop      : in  std_logic;
    
    -- slave port on io bus
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
                
    -- master port on memory bus
    mem_req         : out t_mem_req;
    mem_resp        : in  t_mem_resp;
    
    -- serial bus pins
    atn_o           : out std_logic; -- open drain
    atn_i           : in  std_logic;

    clk_o           : out std_logic; -- open drain
    clk_i           : in  std_logic;              

    data_o          : out std_logic; -- open drain
    data_i          : in  std_logic;              

    iec_reset_n     : in  std_logic;
    c64_reset_n     : in  std_logic;
    
    -- LED
    act_led_n       : out std_logic;
    motor_led_n     : out std_logic;
    dirty_led_n     : out std_logic;

    -- audio out
    audio_sample    : out signed(12 downto 0) );

end c1541_drive;

architecture structural of c1541_drive is
    signal cpu_clock_en     : std_logic;
    signal drv_clock_en     : std_logic;
    signal iec_reset_o      : std_logic;
    
    signal param_write      : std_logic;
    signal param_ram_en     : std_logic;
    signal param_addr       : std_logic_vector(10 downto 0);
    signal param_wdata      : std_logic_vector(7 downto 0);
    signal param_rdata      : std_logic_vector(7 downto 0);

    signal do_track_out     : std_logic;
    signal do_track_in      : std_logic;
    signal do_head_bang     : std_logic;
    signal en_hum           : std_logic;
    signal en_slip          : std_logic;

    signal use_c64_reset    : std_logic;
    signal floppy_inserted  : std_logic := '0';
    signal bank_is_ram      : std_logic_vector(7 downto 0);
    signal power            : std_logic;
    signal motor_on         : std_logic;
    signal mode             : std_logic;
    signal step             : std_logic_vector(1 downto 0) := "00";
    signal soe              : std_logic;
    signal rate_ctrl        : std_logic_vector(1 downto 0);
    signal byte_ready       : std_logic;
    signal sync             : std_logic;
    signal track            : std_logic_vector(6 downto 0);
    signal track_is_0       : std_logic;
	signal drive_address	: std_logic_vector(1 downto 0) := "00";
	signal write_prot_n	    : std_logic := '1';
    signal drv_reset        : std_logic := '1';
    signal disk_rdata       : std_logic_vector(7 downto 0);
    signal disk_wdata       : std_logic_vector(7 downto 0);
    
    signal mem_req_cpu      : t_mem_req;
    signal mem_resp_cpu     : t_mem_resp;
    signal mem_req_flop     : t_mem_req;
    signal mem_resp_flop    : t_mem_resp;
    signal mem_req_snd      : t_mem_req := c_mem_req_init;
    signal mem_resp_snd     : t_mem_resp;

    signal count            : unsigned(7 downto 0) := X"00";
	signal led_intensity	: unsigned(1 downto 0);
begin        
    i_timing: entity work.c1541_timing
    port map (
        clock        => clock,
        reset        => reset,
        
        use_c64_reset=> use_c64_reset,
        c64_reset_n  => c64_reset_n,
        iec_reset_n  => iec_reset_n,
        iec_reset_o  => iec_reset_o,
    
        drive_stop   => drive_stop,
    
        drv_clock_en => drv_clock_en,   -- 1/12.5 (4 MHz)
        cpu_clock_en => cpu_clock_en ); -- 1/50   (1 MHz)

    i_cpu: entity work.cpu_part_1541
    generic map (
        g_tag          => g_cpu_tag,
        g_ram_base     => g_ram_base )
    port map (
        clock       => clock,
        clock_en    => cpu_clock_en,
        reset       => drv_reset,
        
        -- serial bus pins
        atn_o       => atn_o, -- open drain
        atn_i       => atn_i,
    
        clk_o       => clk_o, -- open drain
        clk_i       => clk_i,    
    
        data_o      => data_o, -- open drain
        data_i      => data_i,

        
        -- trace data
        cpu_pc      => open, --cpu_pc_1541,
        
		-- configuration
        bank_is_ram    => bank_is_ram,
		
		-- memory interface
        mem_req         => mem_req_cpu,
        mem_resp        => mem_resp_cpu,

        -- drive pins
        power           => power,
        drive_address   => drive_address,
        write_prot_n    => write_prot_n,
        motor_on        => motor_on,
        mode            => mode,
        step            => step,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        byte_ready      => byte_ready,
        sync            => sync,
        track_is_0      => track_is_0,
        
        drv_rdata       => disk_rdata,
        drv_wdata       => disk_wdata,
    
        -- other
        act_led         => act_led_n );
    
    i_flop: entity work.floppy
    generic map (
        g_tag          => g_floppy_tag )
    port map (
        sys_clock       => clock,
        drv_clock_en    => drv_clock_en,  -- resulting in 4 MHz
        drv_reset       => drv_reset,
        
        -- signals from MOS 6522 VIA
        motor_on        => motor_on,
        mode            => mode,
        write_prot_n    => write_prot_n,
        step            => step,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        byte_ready      => byte_ready,
        sync            => sync,
        
        read_data       => disk_rdata,
        write_data      => disk_wdata,
        
        track           => track,
        track_is_0      => track_is_0,
    ---
        cpu_write       => param_write,
        cpu_ram_en      => param_ram_en,
        cpu_addr        => param_addr,
        cpu_wdata       => param_wdata,
        cpu_rdata       => param_rdata,
    ---
        floppy_inserted => floppy_inserted,
        do_track_out    => do_track_out,
        do_track_in     => do_track_in,
        do_head_bang    => do_head_bang,
        en_hum          => en_hum,
        en_slip         => en_slip,
    ---
        mem_req         => mem_req_flop,
        mem_resp        => mem_resp_flop );

    r_snd: if g_audio generate
        i_snd: entity work.floppy_sound
        generic map (
            g_tag          => g_audio_tag,
            rate_div       => g_audio_div, -- 22050 Hz
            sound_base     => g_audio_base(27 downto 16),
            motor_hum_addr => X"0000",
            flop_slip_addr => X"1200",
            track_in_addr  => X"2400",
            track_out_addr => X"2C00",
            head_bang_addr => X"3480",
            
            motor_len      => 4410,
            track_in_len   => X"0800",  -- ~100 ms;
            track_out_len  => X"0880",  -- ~100 ms;
            head_bang_len  => X"0880" ) -- ~100 ms;
        
        port map (
            clock           => clock, -- 50 MHz
            reset           => drv_reset,
            
            do_trk_out      => do_track_out,
            do_trk_in       => do_track_in,
            do_head_bang    => do_head_bang,
            en_hum          => en_hum,
            en_slip         => en_slip,
            
        	-- memory interface
        	mem_req		    => mem_req_snd,
        	mem_resp        => mem_resp_snd,
        
            -- audio
            sample_out      => audio_sample );
    end generate;

    i_regs: entity work.drive_registers
    generic map (
        g_audio_base    => g_audio_base,
        g_ram_base      => g_ram_base )
    port map (
        clock           => clock,
        reset           => reset,
                        
        io_req          => io_req,
        io_resp         => io_resp,
        
        param_write     => param_write,
        param_ram_en    => param_ram_en,
        param_addr      => param_addr,
        param_wdata     => param_wdata,
        param_rdata     => param_rdata,

        iec_reset_o     => iec_reset_o,
        use_c64_reset   => use_c64_reset,
        power           => power,
        drv_reset       => drv_reset,
        drive_address   => drive_address,
        floppy_inserted => floppy_inserted,
        write_prot_n    => write_prot_n,
        bank_is_ram     => bank_is_ram,
        dirty_led_n     => dirty_led_n,
        
        track           => track,
        mode            => mode,
        motor_on        => motor_on );
            
    -- memory arbitration
    i_arb: entity work.mem_bus_arbiter_pri
    generic map (
        g_ports      => 3,
        g_registered => false )
    port map (
        clock       => clock,
        reset       => reset,
        
        reqs(0)     => mem_req_flop,
        reqs(1)     => mem_req_cpu,
        reqs(2)     => mem_req_snd,

        resps(0)    => mem_resp_flop,
        resps(1)    => mem_resp_cpu,
        resps(2)    => mem_resp_snd,
        
        req         => mem_req,
        resp        => mem_resp );        

    process(clock)
    	variable led_int : unsigned(7 downto 0);
    begin
        if rising_edge(clock) then
            count <= count + 1;
			if count=X"00" then
				motor_led_n <= '0'; -- on
			end if;
			led_int := led_intensity & led_intensity & led_intensity & led_intensity;
			if count=led_int then
				motor_led_n <= '1'; -- off
			end if;
        end if;
    end process;

	led_intensity <= "00" when power='0' else
					 "01" when floppy_inserted='0' else
					 "10" when motor_on='0' else
					 "11";

end architecture;
