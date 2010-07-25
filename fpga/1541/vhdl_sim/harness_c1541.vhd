library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;
use work.mem_bus_pkg.all;
use work.tl_flat_memory_model_pkg.all;

entity harness_c1541 is

end harness_c1541;

architecture harness of harness_c1541 is
    signal clock        : std_logic := '0';
    signal clk_shifted  : std_logic := '0';
    signal cpu_clock_en : std_logic := '0';
    signal drv_clock_en : std_logic := '0';
    
    signal reset        : std_logic := '0';
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    
    signal iec_atn      : std_logic;
    signal iec_atn_o    : std_logic;
    signal iec_atn_i    : std_logic;
    signal iec_data     : std_logic;
    signal iec_data_o   : std_logic;
    signal iec_data_i   : std_logic;
    signal iec_clk      : std_logic;
    signal iec_clk_o    : std_logic;
    signal iec_clk_i    : std_logic;

    signal mem_req      : t_mem_req;
    signal mem_resp     : t_mem_resp;

    signal act_led_n    : std_logic;
    signal audio_sample : unsigned(12 downto 0);

    signal SDRAM_A      : std_logic_vector(14 downto 0);
    signal SDRAM_DQ     : std_logic_vector(7 downto 0);
    signal SDRAM_CSn    : std_logic;
    signal SDRAM_RASn   : std_logic;
    signal SDRAM_CASn   : std_logic;
    signal SDRAM_WEn    : std_logic;
    signal SDRAM_DQM    : std_logic := '0';
	signal SDRAM_CKE    : std_logic;
	signal SDRAM_CLK    : std_logic;

    shared variable dram : h_mem_object;

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    clk_shifted <= transport clock after 15 ns;
    
    process(clock)
        variable count : integer := 0;
    begin
        if rising_edge(clock) then
            cpu_clock_en <= '0';
            drv_clock_en <= '0';
            
            case count is
            when 0 | 12 | 25 | 37 =>
                drv_clock_en <= '1';
                count := count + 1;
            when 49 =>
                cpu_clock_en <= '1';
                count := 0;
            when others =>
                count := count + 1;
            end case;                    
        end if;
    end process;
    
    i_io_bus_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => clock,
    
        req         => io_req,
        resp        => io_resp );

    i_drive: entity work.c1541_drive 
    generic map (
        g_audio         => true,
        g_audio_div     => 100, -- for simulation: 500 ksps
        g_audio_base    => X"0010000",
        g_ram_base      => X"0000000" )
    port map (
        clock           => clock,
        reset           => reset,
        cpu_clock_en    => cpu_clock_en,
        drv_clock_en    => drv_clock_en,
        
        -- slave port on io bus
        io_req          => io_req,
        io_resp         => io_resp,
                    
        -- master port on memory bus
        mem_req         => mem_req,
        mem_resp        => mem_resp,
        
        -- serial bus pins
        atn_o           => iec_atn_o, -- open drain
        atn_i           => iec_atn_i,
        clk_o           => iec_clk_o, -- open drain
        clk_i           => iec_clk_i,              
        data_o          => iec_data_o, -- open drain
        data_i          => iec_data_i,              
        
        -- LED
        act_led_n       => act_led_n,
        
        -- audio out
        audio_sample    => audio_sample );

    iec_atn <= '0' when iec_atn_o='0' else 'Z';
    iec_atn_i <= '0' when iec_atn='0' else '1';

    iec_clk <= '0' when iec_clk_o='0' else 'Z';
    iec_clk_i <= '0' when iec_clk='0' else '1';

    iec_data <= '0' when iec_data_o='0' else 'Z';
    iec_data_i <= '0' when iec_data='0' else '1';

	i_memctrl: entity work.ext_mem_ctrl_v4
    generic map (
    	g_simulation => true,
    	A_Width	    => 15 )
		
    port map (
        clock       => clock,
        clk_shifted => clk_shifted,
        reset       => reset,
                                  
        inhibit     => '0',
        is_idle     => open,
        
        req         => mem_req,
        resp        => mem_resp,
        
		SDRAM_CSn   => SDRAM_CSn,	
	    SDRAM_RASn  => SDRAM_RASn,
	    SDRAM_CASn  => SDRAM_CASn,
	    SDRAM_WEn   => SDRAM_WEn,
		SDRAM_CKE	=> SDRAM_CKE,
		SDRAM_CLK	=> SDRAM_CLK,

        MEM_A       => SDRAM_A,
        MEM_D       => SDRAM_DQ );

    i_memory: entity work.dram_model_8
    generic map (
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
        A    => SDRAM_A(12 downto 0),
        BA   => SDRAM_A(14 downto 13),
        CSn  => SDRAM_CSn,
        RASn => SDRAM_RASn,
        CASn => SDRAM_CASn,
        WEn  => SDRAM_WEn,
        DQM  => SDRAM_DQM,
        
        DQ   => SDRAM_DQ );

	process
	begin
        bind_mem_model("dram", dram);
        
        load_memory("../../../software/1541u/application/ultimate/roms/1541-ii.bin", dram, X"0000C000");
        load_memory("../../../software/1541u/application/ultimate/roms/sounds.bin", dram, X"00010000");

        --wait for 3 ms;
        --save_memory("datspace_sim.bin", ram, X"00068000", 8192);
		wait;
	end process;

end harness;