-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : External Memory controller for SRAM / FLASH / SDRAM (no burst)
-------------------------------------------------------------------------------
-- File       : ext_mem_ctrl.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single access memory controller.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_arith.all;
    use ieee.std_logic_unsigned.all;

library unisim;
    use unisim.vcomponents.all;


entity ext_mem_ctrl is
generic (
	tag_width		   : integer := 2;
	
    SRAM_Byte_Lanes    : integer := 1;
	SRAM_Data_Width	   : integer := 8;

	SRAM_WR_ASU		   : integer := 0;
	SRAM_WR_Pulse      : integer := 1; -- 2 cycles in total
	SRAM_WR_Hold       : integer := 1;

	SRAM_RD_ASU		   : integer := 0;
	SRAM_RD_Pulse      : integer := 1;
	SRAM_RD_Hold	   : integer := 1; -- recovery time (bus turnaround)
	
	FLASH_ASU          : integer := 0;
	FLASH_Pulse	       : integer := 3;
	FLASH_Hold		   : integer := 1; -- bus turn around

    A_Width            : integer := 23;
    SDRAM_Refr_period  : integer := 375 );
port (
    clock       : in  std_logic := '0';
    clk_shifted : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : in  std_logic;
    is_idle     : out std_logic;

    req         : in  std_logic;
    req_tag		: in  std_logic_vector(1 to tag_width) := (others => '0');
    readwriten  : in  std_logic;
    address     : in  std_logic_vector(25 downto 0); -- 64M Space
    rack        : out std_logic;
	dack		: out std_logic;
    rack_tag    : out std_logic_vector(1 to tag_width);
	dack_tag	: out std_logic_vector(1 to tag_width);
	data_on_bus : out std_logic;
	
    wdata       : in  std_logic_vector(SRAM_Data_Width-1 downto 0);
	wdata_mask	: in  std_logic_vector(SRAM_Byte_Lanes-1 downto 0) := (others => '0');
    rdata       : out std_logic_vector(SRAM_Data_Width-1 downto 0);

    io_rdata    : in  std_logic_vector(SRAM_Data_Width-1 downto 0) := (others => '0');
    dma_req     : out std_logic;
    dma_rwn     : out std_logic;
    dma_ok      : in  std_logic;
    dma_ack     : in  std_logic := '0';

    enable_refr : in  std_logic := '0';
    enable_sdram: in  std_logic := '0';

	SDRAM_CLK	: out std_logic;
	SDRAM_CKE	: out std_logic;
    SDRAM_CSn   : out std_logic;
	SDRAM_RASn  : out std_logic;
	SDRAM_CASn  : out std_logic;
	SDRAM_WEn   : out std_logic;

    SRAM_CSn    : out   std_logic;
	FLASH_CSn   : out   std_logic;
    MEM_A       : out   std_logic_vector(A_Width-1 downto 0);
    MEM_OEn     : out   std_logic;
    MEM_WEn     : out   std_logic;
    MEM_D       : inout std_logic_vector(SRAM_Data_Width-1 downto 0) := (others => 'Z');
    MEM_BEn     : out   std_logic_vector(SRAM_Byte_Lanes-1 downto 0) );
end ext_mem_ctrl;    

-- ADDR: 25 24 23 ...
--        0  0  0 ... SRAM
--        0  0  1 ... C64 DMA
--        0  1  0 ... Flash
--        0  1  1 ... SDRAM command
--        1  X  X ... SDRAM (32MB)

architecture Gideon of ext_mem_ctrl is
    type t_state is (idle, setup, pulse, hold, dma_access, recover, sd_cas, sd_wait);
    signal state          : t_state;
    signal sram_d_o       : std_logic_vector(MEM_D'range) := (others => '1');
    signal sram_d_t       : std_logic := '0';
    signal delay          : integer range 0 to 7;
    signal rwn_i	      : std_logic;
    signal tag		      : std_logic_vector(1 to tag_width);
    signal memsel         : std_logic_vector(1 downto 0);
    signal mem_a_i        : std_logic_vector(MEM_A'range) := (others => '0');
    signal col_addr       : std_logic_vector(9 downto 0) := (others => '0');
    signal refresh_cnt    : integer range 0 to SDRAM_Refr_period-1;
    signal do_refresh     : std_logic := '0';
    signal not_clock      : std_logic;
    signal reg_out        : integer range 0 to 3 := 0;     

--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";

--    attribute register_duplication : string;
--    attribute register_duplication of mem_a_i  : signal is "no";

    attribute iob : string;
    attribute iob of rdata : signal is "true"; -- the general memctrl/rdata must be packed in IOB
begin
	assert SRAM_WR_Hold > 0  report "Write hold time should be greater than 0." severity failure;
--	assert SRAM_RD_Hold > 0  report "Read hold time should be greater than 0 for bus turnaround." severity failure;
	assert SRAM_WR_Pulse > 0 report "Write pulse time should be greater than 0." severity failure;
	assert SRAM_RD_Pulse > 0 report "Read pulse time should be greater than 0." severity failure;
	assert FLASH_Pulse > 0   report "Flash cmd pulse time should be greater than 0." severity failure;
	assert FLASH_Hold > 0    report "Flash hold time should be greater than 0." severity failure;

    is_idle <= '1' when state = idle else '0';
    data_on_bus <= '1' when (state = pulse) and (delay = 2) and (tag(1)='1') else '0';   

    process(clock)
    begin
        if rising_edge(clock) then
            rack     <= '0';
			dack     <= '0';
			rack_tag <= (others => '0');
			dack_tag <= (others => '0');

            if reg_out/=0 then
                reg_out <= reg_out-1;
            end if;

            rdata <= MEM_D;  -- clock in

            SDRAM_CSn <= '1';
            SDRAM_CKE <= enable_sdram;
            
            case state is
            when idle =>
				if inhibit='0' then
                    dma_req <= '0';
    				if req='1' then
    					rack      <= '1';
    					rack_tag  <= req_tag;
    					tag		  <= req_tag;
    					rwn_i     <= readwriten;
    					mem_a_i   <= address(MEM_A'range);
    					memsel    <= address(25 downto 24);
    					sram_d_t  <= not readwriten;
    					sram_d_o  <= wdata;
    					SRAM_CSn  <= address(25) or     address(24) or address(23); -- should be all '0' for CSn to become active
    					FLASH_CSn <= address(25) or not address(24) or address(23); -- '0' when A25..23 = 010
    
                        if address(25)='0' and do_refresh='1' then -- hidden refresh on dram
                            do_refresh <= '0';
                            SDRAM_CSn  <= '0';
                            SDRAM_RASn <= '0';
                            SDRAM_CASn <= '0';
                            SDRAM_WEn  <= '1'; -- Auto Refresh
                        end if;

                        if address(25)='1' then
                            mem_a_i(12 downto 0)  <= address(24 downto 12); -- 13 row bits
                            mem_a_i(17 downto 16) <= address(11 downto 10); --  2 bank bits
                            col_addr              <= address( 9 downto  0); -- 10 column bits
                            SDRAM_CSn  <= '0';
                            SDRAM_RASn <= '0';
                            SDRAM_CASn <= '1';
                            SDRAM_WEn  <= '1'; -- Command = ACTIVE
                            sram_d_t   <= '0'; -- no data yet
                            delay      <= 1;
                            state <= sd_cas;                            
                            
                        elsif address(24 downto 23)="10" then -- Flash
							if FLASH_ASU=0 then
								state <= pulse;
								delay <= FLASH_Pulse;
							else
								delay <= FLASH_ASU;
								state <= setup;
							end if;

                            if readwriten='0' then -- write
        						MEM_BEn <= not wdata_mask;
								MEM_WEn <= '0';
                                MEM_OEn <= '1';
                            else -- read
        						MEM_BEn <= (others => '0');
								MEM_OEn <= '0';
                                MEM_WEn <= '1';
                            end if;

                        elsif address(24 downto 23)="11" then -- sdram command
                            SDRAM_CSn  <= '0';
                            SDRAM_RASn <= address(13);
                            SDRAM_CASn <= address(14);
                            SDRAM_WEn  <= address(15);
                            dack       <= '1';
                            dack_tag   <= req_tag;
                            state      <= idle;
                            
                        elsif address(24 downto 23)="01" then -- DMA
                            MEM_BEn <= (others => '1');
                            MEM_OEn <= '1';
                            MEM_WEn <= '1';
                            dma_req <= '1';
                            state   <= dma_access;
                            sram_d_t   <= '0';

                        else -- SRAM
                            if readwriten='0' then -- write
        						MEM_BEn <= not wdata_mask;
    							if SRAM_WR_ASU=0 then
    								state <= pulse;
    								MEM_WEn <= '0';
    								delay <= SRAM_WR_Pulse;
    							else
    								delay <= SRAM_WR_ASU;
    								state <= setup;
    							end if;

                            else -- read
        						MEM_BEn <= (others => '0');
								MEM_OEn <= '0';
    							if SRAM_RD_ASU=0 then
    								state <= pulse;
    								delay <= SRAM_RD_Pulse;
    							else
    								delay <= SRAM_RD_ASU;
    								state <= setup;
    							end if;
                            end if;
                        end if;
                    end if;
                else -- inhibit is active
                    dma_req  <= '0';
                    sram_d_o <= io_rdata;
                    if dma_ok='1' then
                        sram_d_t <= '1';
                        reg_out  <= 2;
                    end if;
                    if reg_out=2 then
                        dma_req <= '1';
                    elsif reg_out=1 then
                        sram_d_t <= '0';
                    end if;                        
                end if;
					
			when sd_cas =>
                mem_a_i(10) <= '1'; -- auto precharge
                mem_a_i(9 downto 0) <= col_addr;

                if delay = 0 then
                    -- read or write with auto precharge
                    SDRAM_CSn   <= '0';
                    SDRAM_RASn  <= '1';
                    SDRAM_CASn  <= '0';
                    SDRAM_WEn   <= rwn_i;
                    if rwn_i='0' then -- write
                        sram_d_t <= '1';
                    end if;
                    delay   <= 1;
                    state   <= sd_wait;
                else
                    delay <= delay - 1;
                end if;
                
            when sd_wait =>
                sram_d_t <= '0';
                if delay=0 then
					dack     <= '1';
					dack_tag <= tag;
                    state    <= idle;
                else
                    delay <= delay - 1;
		        end if;

			when setup =>
				if delay = 1 then
					state <= pulse;
					if memsel(0)='0' then -- SRAM
						if rwn_i='0' then
							delay <= SRAM_WR_Pulse;
							MEM_WEn <= '0';
						else
							delay <= SRAM_RD_Pulse;
							MEM_OEn <= '0';
						end if;
					else
						delay <= FLASH_Pulse;
						if rwn_i='0' then
							MEM_WEn <= '0';
						else
							MEM_OEn <= '0';
						end if;
					end if;						
				else
					delay <= delay - 1;
				end if;

			when pulse =>
				if delay = 1 then
					MEM_OEn  <= '1';
					MEM_WEn  <= '1';
					dack     <= '1';
					dack_tag <= tag;
					if memsel(0)='0' then -- SRAM
						if rwn_i='0' and SRAM_WR_Hold > 0 then
							delay <= SRAM_WR_Hold;
							state <= hold;
					    elsif rwn_i='1' and SRAM_RD_Hold > 0 then
							state <= hold;				    	
					    	delay <= SRAM_RD_Hold;
						else
							sram_d_t  <= '0';
							SRAM_CSn  <= '1';
							FLASH_CSn <= '0';
							state     <= idle;
					    end if;
					else -- Flash
						if rwn_i='0' and FLASH_Hold > 0 then -- for writes, add hold cycles
							delay <= FLASH_Hold;
							state <= hold;
						else
							sram_d_t <= '0';
							SRAM_CSn  <= '1';
							FLASH_CSn <= '0';
							state    <= idle;
					    end if;
					end if;
				else
					delay <= delay - 1;
			    end if;
         
			when hold =>
				if delay = 1 then
					sram_d_t <= '0';
					SRAM_CSn  <= '1';
					FLASH_CSn <= '0';
					state <= idle;
				else
					delay <= delay - 1;
			    end if;

            when dma_access =>
                if dma_ok='1' then
                    sram_d_t   <= not rwn_i;
                end if;
                if dma_ack='1' then
                    dma_req  <= '0';
                    sram_d_t <= '0';
                    dack     <= '1';
                    dack_tag <= tag;
                    state    <= recover;
                end if;
                                    
            when recover =>
                -- just a wait state to recover from glitches from disconnecting from the bus
                state <= idle;

            when others =>
                null;

            end case;

            if refresh_cnt = SDRAM_Refr_period-1 then
                do_refresh  <= enable_refr;
                refresh_cnt <= 0;
            else
                refresh_cnt <= refresh_cnt + 1;
            end if;

            if reset='1' then
                state      <= idle;
                dma_req    <= '0';
				SRAM_CSn   <= '1';
				FLASH_CSn  <= '0';
                MEM_BEn    <= (others => '1');
--                sram_d_o   <= (others => '1');
				sram_d_t   <= '0';
                MEM_OEn    <= '1';
				MEM_WEn    <= '1';
				delay      <= 0;
				tag		   <= (others => '0');
                do_refresh <= '0';
            end if;
        end if;
    end process;
    
    dma_rwn     <= rwn_i;

    MEM_D       <= sram_d_o when sram_d_t='1' else (others => 'Z');
    MEM_A       <= mem_a_i;
    
    not_clock <= not clk_shifted;
    clkout: FDDRRSE
	port map (
		CE => '1',
		C0 => clk_shifted,
		C1 => not_clock,
		D0 => '0',
		D1 => enable_sdram,
		Q  => SDRAM_CLK,
		R  => '0',
		S  => '0' );

end Gideon;
