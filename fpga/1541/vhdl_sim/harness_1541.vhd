library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_unsigned.all;

library work;
    use work.flat_memory_model.all;
    use work.iec_bus_bfm_pkg.all;
    

entity harness_1541 is
end harness_1541;

        

architecture structural of harness_1541 is

    signal sram_a           : std_logic_vector(17 downto 0);
    signal sram_dq          : std_logic_vector(31 downto 0);
    signal sram_csn         : std_logic;
    signal sram_oen         : std_logic;
    signal sram_wen         : std_logic;
    signal sram_ben         : std_logic_vector(3 downto 0);

    signal iec_atn          : std_logic;
    signal iec_data         : std_logic;
    signal iec_clock        : std_logic;

---
    signal clock            : std_logic := '0';
    signal clock_en         : std_logic;
    signal reset            : std_logic;

    signal mem_req          : std_logic;
    signal mem_readwriten   : std_logic;
    signal mem_address      : std_logic_vector(19 downto 0) := (others => '0');
    signal mem_rack         : std_logic;
    signal mem_dack         : std_logic;
    signal mem_wdata        : std_logic_vector(7 downto 0);
    signal mem_rdata        : std_logic_vector(7 downto 0);

    signal act_led          : std_logic;
    signal motor_on         : std_logic;
    signal mode             : std_logic;
    signal step             : std_logic_vector(1 downto 0) := "00";
    signal soe              : std_logic;
    signal rate_ctrl        : std_logic_vector(1 downto 0);
                         
    signal track            : std_logic_vector(6 downto 0);
                         
    signal byte_ready       : std_logic;
    signal sync             : std_logic;
    signal disk_rdata       : std_logic_vector(7 downto 0);
    signal disk_wdata       : std_logic_vector(7 downto 0);

    signal atn_o, atn_i     : std_logic;
    signal clk_o, clk_i     : std_logic;
    signal data_o, data_i   : std_logic;

begin
    -- 4 MHz clock
    clock <= not clock after 125 ns;
    reset <= '1', '0' after 2 us;

    ce: process
    begin
        clock_en <= '0';
        wait until clock='1';
        wait until clock='1';
        wait until clock='1';
        clock_en <= '1';
        wait until clock='1';
    end process;

    cpu: entity work.cpu_part_1541
    port map (
        clock       => clock,
        clock_en    => clock_en,
        reset       => reset,
        
        -- serial bus pins
        atn_o       => atn_o, -- open drain
        atn_i       => atn_i,
    
        clk_o       => clk_o, -- open drain
        clk_i       => clk_i,    
    
        data_o      => data_o, -- open drain
        data_i      => data_i,
        
        -- drive pins
        drive_select(0) => '0',
        drive_select(1) => '0',
        motor_on        => motor_on,
        mode            => mode,
        write_prot_n    => '1',
        step            => step,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        byte_ready      => byte_ready,
        sync            => sync,
        
        drv_rdata       => disk_rdata,
        drv_wdata       => disk_wdata,
    
        -- other
        act_led         => act_led );
        
    iec_atn   <= '0' when atn_o='0'  else 'H'; -- open drain, with pull up
    iec_clock <= '0' when clk_o='0'  else 'H'; -- open drain, with pull up
    iec_data  <= '0' when data_o='0' else 'H'; -- open drain, with pull up

    atn_i  <= iec_atn;
    clk_i  <= iec_clock;
    data_i <= iec_data;

    flop: entity work.floppy
    port map (
        drv_clock       => clock,
        drv_clock_en    => '1',  -- combi clk/cke that yields 4 MHz; eg. 16/4
        drv_reset       => reset,
        
        -- signals from MOS 6522 VIA
        motor_on        => motor_on,
        mode            => mode,
        write_prot_n    => '1',
        step            => step,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        byte_ready      => byte_ready,
        sync            => sync,
        
        read_data       => disk_rdata,
        write_data      => disk_wdata,
        
        track           => track,
    ---
        mem_clock       => clock,
        mem_reset       => reset,
        
        mem_req         => mem_req,
        mem_rwn         => mem_readwriten,
        mem_addr        => mem_address,
        mem_rack        => mem_rack,
        mem_dack        => mem_dack,
        
        mem_wdata       => mem_wdata,
        mem_rdata       => mem_rdata );

    ram_ctrl: entity work.sram_8bit32
    generic map (
		SRAM_WR_ASU		   => 0,
		SRAM_WR_Pulse      => 1,
		SRAM_WR_Hold       => 2,
	
		SRAM_RD_ASU		   => 0,
		SRAM_RD_Pulse      => 1,
		SRAM_RD_Hold	   => 2 ) -- recovery time (bus turnaround)
    port map (
        clock       => clock,
        reset       => reset,
                                  
        req         => mem_req,
        readwriten  => mem_readwriten,
        address     => mem_address,
        rack        => mem_rack,
        dack        => mem_dack,
                                  
        wdata       => mem_wdata,
        rdata       => mem_rdata,
                                       
        SRAM_A      => SRAM_A,
        SRAM_OEn    => SRAM_OEn,
        SRAM_WEn    => SRAM_WEn,
        SRAM_CSn    => SRAM_CSn,
        SRAM_D      => SRAM_DQ,
        SRAM_BEn    => SRAM_BEn );

    sram: entity work.sram_model_32
    generic map(18, 50 ns)
    port map (SRAM_A, SRAM_DQ, SRAM_CSn, SRAM_BEn, SRAM_OEn, SRAM_WEn);

    iec_bfm: entity work.iec_bus_bfm
    port map (
        iec_clock   => iec_clock,
        iec_data    => iec_data,
        iec_atn     => iec_atn );


end structural;

