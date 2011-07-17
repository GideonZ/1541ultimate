library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.mem_bus_master_bfm_pkg.all;

entity harness_dm_cache is
end harness_dm_cache;

architecture harness of harness_dm_cache is
    signal clock       : std_logic  := '0';
    signal clock_shifted    : std_logic;
    signal reset       : std_logic;
    signal client_req  : t_mem_req  := c_mem_req_init;
    signal client_resp : t_mem_resp := c_mem_resp_init;
    signal mem_req     : t_mem_burst_req  := c_mem_burst_req_init;
    signal mem_resp    : t_mem_burst_resp := c_mem_burst_resp_init;

	signal SDRAM_CLK   : std_logic;
	signal SDRAM_CKE   : std_logic;
    signal SDRAM_CSn   : std_logic := '1';
	signal SDRAM_RASn  : std_logic := '1';
	signal SDRAM_CASn  : std_logic := '1';
	signal SDRAM_WEn   : std_logic := '1';
    signal SDRAM_DQM   : std_logic := '0';
    signal SDRAM_A     : std_logic_vector(14 downto 0);
    signal SDRAM_D     : std_logic_vector(7 downto 0) := (others => 'Z');

	signal logic_CLK   : std_logic;
	signal logic_CKE   : std_logic;
    signal logic_CSn   : std_logic := '1';
	signal logic_RASn  : std_logic := '1';
	signal logic_CASn  : std_logic := '1';
	signal logic_WEn   : std_logic := '1';
    signal logic_DQM   : std_logic := '0';
    signal logic_A     : std_logic_vector(14 downto 0) := (others => 'H');

    signal hit_count   : unsigned(31 downto 0);
    signal miss_count  : unsigned(31 downto 0);
    signal hit_ratio   : real := 0.0;
begin
    clock <= not clock after 10 ns;
    clock_shifted <= transport clock after 7.5 ns;

    reset <= '1', '0' after 100 ns;
    
    i_cache: entity work.dm_cache
    port map (
        clock       => clock,
        reset       => reset,
        
        client_req  => client_req,
        client_resp => client_resp,
        
        mem_req     => mem_req,
        mem_resp    => mem_resp,
        
        hit_count   => hit_count,
        miss_count  => miss_count );

    hit_ratio <= real(to_integer(hit_count)) / real(to_integer(miss_count) + to_integer(hit_count) + 1);

    i_mem_master_bfm: entity work.mem_bus_master_bfm
    generic map (
        g_name      => "mem_master" )
    port map (
        clock       => clock,
    
        req         => client_req,
        resp        => client_resp );

    i_mem_ctrl: entity work.ext_mem_ctrl_v5_sdr
    generic map (
        g_simulation => true,
    	A_Width	     => 15 )
    port map (
        clock       => clock,
        clk_shifted => clock_shifted,
        reset       => reset,
    
        inhibit     => '0',
        is_idle     => open,
    
        req         => mem_req,
        resp        => mem_resp,
    
    	SDRAM_CLK	=> logic_CLK,
    	SDRAM_CKE	=> logic_CKE,
        SDRAM_CSn   => logic_CSn,
    	SDRAM_RASn  => logic_RASn,
    	SDRAM_CASn  => logic_CASn,
    	SDRAM_WEn   => logic_WEn,
        SDRAM_DQM   => logic_DQM,
    
        MEM_A       => logic_A,
        MEM_D       => SDRAM_D );

	SDRAM_CLK	<= transport logic_CLK  after 6 ns;
	SDRAM_CKE	<= transport logic_CKE  after 6 ns;
    SDRAM_CSn   <= transport logic_CSn  after 6 ns;
	SDRAM_RASn  <= transport logic_RASn after 6 ns;
	SDRAM_CASn  <= transport logic_CASn after 6 ns;
	SDRAM_WEn   <= transport logic_WEn  after 6 ns;
    SDRAM_DQM   <= transport logic_DQM  after 6 ns;
    SDRAM_A     <= transport logic_A    after 6 ns;
    
    i_dram_bfm: entity work.dram_model_8
    generic map(
        g_given_name  => "dram",
        g_cas_latency => 1,
        g_burst_len_r => 4,
        g_burst_len_w => 4,
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
    
        DQ   => SDRAM_D);

end harness;
