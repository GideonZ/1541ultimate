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
    use ieee.numeric_std.all;

library unisim;
    use unisim.vcomponents.all;

library work;
    use work.mem_bus_pkg.all;

entity ext_mem_ctrl_v4_u1 is
generic (
    g_simulation       : boolean := false;

	SRAM_WR_ASU		   : integer := 0;
	SRAM_WR_Pulse      : integer := 1; -- 2 cycles in total
	SRAM_WR_Hold       : integer := 1;

	SRAM_RD_ASU		   : integer := 0;
	SRAM_RD_Pulse      : integer := 1;
	SRAM_RD_Hold	   : integer := 1; -- recovery time (bus turnaround)
	
    ETH_Acc_Time       : integer := 9;

	FLASH_ASU          : integer := 0;
	FLASH_Pulse	       : integer := 4;
	FLASH_Hold		   : integer := 1; -- bus turn around

    A_Width            : integer := 23;
    SDRAM_WakeupTime   : integer := 40;     -- refresh periods
    SDRAM_Refr_period  : integer := 375 );
port (
    clock       : in  std_logic := '0';
    clk_shifted : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : in  std_logic;
    is_idle     : out std_logic;

    req         : in  t_mem_req;
    resp        : out t_mem_resp;

	SDRAM_CLK	: out std_logic;
	SDRAM_CKE	: out std_logic;
    SDRAM_CSn   : out std_logic := '1';
	SDRAM_RASn  : out std_logic := '1';
	SDRAM_CASn  : out std_logic := '1';
	SDRAM_WEn   : out std_logic := '1';

    ETH_CSn     : out   std_logic := '1';
    SRAM_CSn    : out   std_logic;
	FLASH_CSn   : out   std_logic;
    MEM_A       : out   std_logic_vector(A_Width-1 downto 0);
    MEM_OEn     : out   std_logic;
    MEM_WEn     : out   std_logic;
    MEM_D       : inout std_logic_vector(7 downto 0) := (others => 'Z');
    MEM_BEn     : out   std_logic );
end ext_mem_ctrl_v4_u1;    

-- ADDR: 25 24 23 22 21 20 19 ...
--        0  0  0  0  0  0  0 SRAM
--        0  x  x  x  x  x  x SDRAM
--        1  0  x  x  x  x  x Flash
--        1  1  x  x  x  x  x Ethernet

architecture Gideon of ext_mem_ctrl_v4_u1 is
    type t_init is record
        addr    : std_logic_vector(23 downto 0);
        cmd     : std_logic_vector(2 downto 0);  -- we-cas-ras
    end record;
    type t_init_array is array(natural range <>) of t_init;
    constant c_init_array : t_init_array(0 to 7) := (
        ( X"000400", "010" ), -- auto precharge
        ( X"000220", "000" ), -- mode register, burstlen=1, writelen=1, CAS lat = 2
        ( X"000000", "001" ), -- auto refresh
        ( X"000000", "001" ), -- auto refresh
        ( X"000000", "001" ), -- auto refresh
        ( X"000000", "001" ), -- auto refresh
        ( X"000000", "001" ), -- auto refresh
        ( X"000000", "001" ) );

    type t_state is (boot, init, idle, setup, pulse, hold, sd_cas, sd_wait, eth_pulse);
    signal boot_cnt       : integer range 0 to SDRAM_WakeupTime-1 := SDRAM_WakeupTime-1;
    signal init_cnt       : integer range 0 to c_init_array'high;
    signal enable_sdram   : std_logic := '1';
    
    signal state          : t_state;
    signal sram_d_o       : std_logic_vector(MEM_D'range) := (others => '1');
    signal sram_d_t       : std_logic := '0';
    signal delay          : integer range 0 to 15;
    signal inhibit_d      : std_logic;
    signal rwn_i	      : std_logic;
    signal tag		      : std_logic_vector(7 downto 0);
    signal memsel         : std_logic;
    signal mem_a_i        : std_logic_vector(MEM_A'range) := (others => '0');
    signal col_addr       : std_logic_vector(9 downto 0) := (others => '0');
    signal refresh_cnt    : integer range 0 to SDRAM_Refr_period-1;
    signal do_refresh     : std_logic := '0';
    signal not_clock      : std_logic;
    signal reg_out        : integer range 0 to 3 := 0;     
    signal rdata_i        : std_logic_vector(7 downto 0) := (others => '0');
    signal refr_delay     : integer range 0 to 3;
    signal dack           : std_logic; -- for simulation handy to see
    attribute iob : string;
    attribute iob of rdata_i : signal is "true"; -- the general memctrl/rdata must be packed in IOB
begin
	assert SRAM_WR_Hold > 0  report "Write hold time should be greater than 0." severity failure;
--	assert SRAM_RD_Hold > 0  report "Read hold time should be greater than 0 for bus turnaround." severity failure;
	assert SRAM_WR_Pulse > 0 report "Write pulse time should be greater than 0." severity failure;
	assert SRAM_RD_Pulse > 0 report "Read pulse time should be greater than 0." severity failure;
	assert FLASH_Pulse > 0   report "Flash cmd pulse time should be greater than 0." severity failure;
	assert FLASH_Hold > 0    report "Flash hold time should be greater than 0." severity failure;

    is_idle <= '1' when state = idle else '0';

    resp.data   <= rdata_i;

    process(clock)
        procedure send_refresh_cmd is
        begin
            do_refresh <= '0';
            SDRAM_CSn  <= '0';
            SDRAM_RASn <= '0';
            SDRAM_CASn <= '0';
            SDRAM_WEn  <= '1'; -- Auto Refresh
            refr_delay <= 3; 
        end procedure;

        procedure accept_req is
        begin
			resp.rack      <= '1';
			resp.rack_tag  <= req.tag;
			tag		  <= req.tag;
			rwn_i     <= req.read_writen;
			mem_a_i   <= std_logic_vector(req.address(MEM_A'range));
			sram_d_t  <= not req.read_writen;
			sram_d_o  <= req.data;
			
			SRAM_CSn  <= '1';
			FLASH_CSn <= '1';
			ETH_CSn   <= '1';
			SDRAM_CSn <= '1';
			memsel    <= '0';
			
			if req.address(25 downto 24) = "11" then
				ETH_CSn <= '0';
                delay   <= ETH_Acc_Time;
                state   <= eth_pulse;
                
			elsif req.address(25 downto 24) = "10" then
				memsel    <= '1';
				FLASH_CSn <= '0';
				if FLASH_ASU=0 then
					state <= pulse;
					delay <= FLASH_Pulse;
				else
					delay <= FLASH_ASU;
					state <= setup;
				end if;
                if req.read_writen='0' then -- write
					MEM_BEn <= '0';
					MEM_WEn <= '0';
                    MEM_OEn <= '1';
                else -- read
					MEM_BEn <= '0';
					MEM_OEn <= '0';
                    MEM_WEn <= '1';
                end if;

			elsif req.address(25 downto 19)=0 then
				SRAM_CSn <= '0';
                if req.read_writen='0' then -- write
					MEM_BEn <= '0';
					if SRAM_WR_ASU=0 then
						state <= pulse;
						MEM_WEn <= '0';
						delay <= SRAM_WR_Pulse;
					else
						delay <= SRAM_WR_ASU;
						state <= setup;
					end if;

                else -- read
					MEM_BEn <= '0';
					MEM_OEn <= '0';
					if SRAM_RD_ASU=0 then
						state <= pulse;
						delay <= SRAM_RD_Pulse;
					else
						delay <= SRAM_RD_ASU;
						state <= setup;
					end if;
                end if;

			else -- SDRAM
                mem_a_i(12 downto 0)  <= std_logic_vector(req.address(24 downto 12)); -- 13 row bits
                mem_a_i(17 downto 16) <= std_logic_vector(req.address(11 downto 10)); --  2 bank bits
                col_addr              <= std_logic_vector(req.address( 9 downto  0)); -- 10 column bits
                SDRAM_CSn  <= '0';
                SDRAM_RASn <= '0';
                SDRAM_CASn <= '1';
                SDRAM_WEn  <= '1'; -- Command = ACTIVE
                sram_d_t   <= '0'; -- no data yet
                delay      <= 1;
                state <= sd_cas;                            
            end if;
        end procedure;
    begin
        if rising_edge(clock) then
            resp.rack     <= '0';
			dack          <= '0';
			resp.rack_tag <= (others => '0');
			resp.dack_tag <= (others => '0');
            inhibit_d     <= inhibit;
            
            rdata_i  <= MEM_D;  -- clock in

            SDRAM_CSn <= '1';
            SDRAM_CKE <= enable_sdram;
            
            if refr_delay /= 0 then
                refr_delay <= refr_delay - 1;
            end if;

            case state is
            when boot =>
                enable_sdram <= '1';
                if refresh_cnt = 0 then
                    boot_cnt <= boot_cnt - 1;
                    if boot_cnt = 1 then
                        state <= init;
                    end if;
                elsif g_simulation then
                    state <= idle;
                end if;

            when init =>
                mem_a_i    <= c_init_array(init_cnt).addr(mem_a_i'range);
                SDRAM_RASn <= c_init_array(init_cnt).cmd(0);
                SDRAM_CASn <= c_init_array(init_cnt).cmd(1);
                SDRAM_WEn  <= c_init_array(init_cnt).cmd(2);
                if delay = 0 then
                    delay <= 7;
                    SDRAM_CSn <= '0';
                    if init_cnt = c_init_array'high then
                        state <= idle;
                    else
                        init_cnt <= init_cnt + 1;
                    end if;
                else
                    delay <= delay - 1;
                end if;

            when idle =>
                -- first cycle after inhibit goes 0, do not do refresh
                -- this enables putting cartridge images in sdram
				if do_refresh='1' and not (inhibit_d='1' and inhibit='0') then
				    send_refresh_cmd;
				elsif inhibit='0' then
    				if req.request='1' and refr_delay=0 then
                        accept_req;
                    end if;
                end if;
					
			when sd_cas =>
                mem_a_i(10) <= '1'; -- auto precharge
                mem_a_i(9 downto 0) <= col_addr;
                sram_d_t <= not rwn_i; -- enable for writes

                if delay = 0 then
                    -- read or write with auto precharge
                    SDRAM_CSn   <= '0';
                    SDRAM_RASn  <= '1';
                    SDRAM_CASn  <= '0';
                    SDRAM_WEn   <= rwn_i;
                    if rwn_i='0' then -- write
                        delay   <= 2;
                    else
                        delay   <= 2;
                    end if;
                    state   <= sd_wait;
                else
                    delay <= delay - 1;
                end if;
                
            when sd_wait =>
                sram_d_t <= '0';
                if delay=0 then
                    if rwn_i='1' then
    					dack     <= '1';
					    resp.dack_tag <= tag;
					end if;
                    state    <= idle;
                else
                    delay <= delay - 1;
		        end if;

			when setup =>
				if delay = 1 then
					state <= pulse;
					if memsel='0' then -- SRAM
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
                    if rwn_i='1' then
    					dack     <= '1';
					    resp.dack_tag <= tag;
					end if;
					if memsel='0' then -- SRAM
						if rwn_i='0' and SRAM_WR_Hold > 0 then
							delay <= SRAM_WR_Hold;
							state <= hold;
					    elsif rwn_i='1' and SRAM_RD_Hold > 0 then
							state <= hold;				    	
					    	delay <= SRAM_RD_Hold;
						else
							sram_d_t  <= '0';
							SRAM_CSn  <= '1';
							FLASH_CSn <= '1';
							state     <= idle;
					    end if;
					else -- Flash
						if rwn_i='0' and FLASH_Hold > 0 then -- for writes, add hold cycles
							delay <= FLASH_Hold;
							state <= hold;
						else
							sram_d_t  <= '0';
							SRAM_CSn  <= '1';
							FLASH_CSn <= '1';
							state    <= idle;
					    end if;
					end if;
				else
					delay <= delay - 1;
			    end if;

            when eth_pulse =>
                delay <= delay - 1;

                case delay is
                when 2 =>
					dack     <= '1';
					resp.dack_tag <= tag;
                    MEM_WEn  <= '1';
                    MEM_OEn  <= '1';

                when 1 =>
					sram_d_t <= '0';
                    ETH_CSn  <= '1';
                    state    <= idle;

                when others =>
    				MEM_WEn  <= rwn_i;
                    MEM_OEn  <= not rwn_i;

                end case;
         
			when hold =>
				if delay = 1 then
					sram_d_t  <= '0';
					SRAM_CSn  <= '1';
					FLASH_CSn <= '1';
					state <= idle;
				else
					delay <= delay - 1;
			    end if;

            when others =>
                null;

            end case;

            if refresh_cnt = SDRAM_Refr_period-1 then
                do_refresh  <= '1';
                refresh_cnt <= 0;
            else
                refresh_cnt <= refresh_cnt + 1;
            end if;

            if reset='1' then
                state      <= boot;
                ETH_CSn    <= '1';
				SRAM_CSn   <= '1';
				FLASH_CSn  <= '1';
                MEM_BEn    <= '1';
				sram_d_t   <= '0';
                MEM_OEn    <= '1';
				MEM_WEn    <= '1';
				delay      <= 0;
				tag		   <= (others => '0');
                do_refresh <= '0';
            end if;
        end if;
    end process;
    
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
