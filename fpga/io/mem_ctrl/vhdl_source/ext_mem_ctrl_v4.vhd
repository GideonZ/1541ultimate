-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM (no burst)
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

entity ext_mem_ctrl_v4 is
generic (
    g_simulation       : boolean := false;
    A_Width            : integer := 15;
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
    SDRAM_DQM   : out std_logic := '0';

    MEM_A       : out   std_logic_vector(A_Width-1 downto 0);
    MEM_D       : inout std_logic_vector(7 downto 0) := (others => 'Z'));
end ext_mem_ctrl_v4;    

-- ADDR: 25 24 23 ...
--        0  X  X ... SDRAM (32MB)

architecture Gideon of ext_mem_ctrl_v4 is
    type t_init is record
        addr    : std_logic_vector(15 downto 0);
        cmd     : std_logic_vector(2 downto 0);  -- we-cas-ras
    end record;
    type t_init_array is array(natural range <>) of t_init;
    constant c_init_array : t_init_array(0 to 7) := (
        ( X"0400", "010" ), -- auto precharge
        ( X"0220", "000" ), -- mode register, burstlen=1, writelen=1, CAS lat = 2
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ), -- auto refresh
        ( X"0000", "001" ) );

    type t_state is (boot, init, idle, sd_cas, sd_wait);
    signal state          : t_state;
    signal sram_d_o       : std_logic_vector(MEM_D'range) := (others => '1');
    signal sram_d_t       : std_logic := '0';
    signal delay          : integer range 0 to 15;
    signal inhibit_d      : std_logic;
    signal rwn_i	      : std_logic;
    signal tag		      : std_logic_vector(req.tag'range);
    signal mem_a_i        : std_logic_vector(MEM_A'range) := (others => '0');
    signal col_addr       : std_logic_vector(9 downto 0) := (others => '0');
    signal refresh_cnt    : integer range 0 to SDRAM_Refr_period-1;
    signal do_refresh     : std_logic := '0';
    signal not_clock      : std_logic;
    signal reg_out        : integer range 0 to 3 := 0;     
    signal rdata_i        : std_logic_vector(7 downto 0) := (others => '0');
    signal dout_sel       : std_logic := '0';
    signal refr_delay     : integer range 0 to 3;
    signal boot_cnt       : integer range 0 to SDRAM_WakeupTime-1 := SDRAM_WakeupTime-1;
    signal init_cnt       : integer range 0 to c_init_array'high;
    signal enable_sdram   : std_logic := '1';

    signal req_i          : std_logic;
    signal dack           : std_logic;
    signal rack           : std_logic;
    signal rack_tag       : std_logic_vector(req.tag'range);
    signal dack_tag       : std_logic_vector(req.tag'range);

--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";

--    attribute register_duplication : string;
--    attribute register_duplication of mem_a_i  : signal is "no";

    attribute iob : string;
    attribute iob of rdata_i : signal is "true"; -- the general memctrl/rdata must be packed in IOB
    attribute iob of SDRAM_CKE : signal is "false";

begin


    is_idle <= '1' when state = idle else '0';

    req_i <= req.request;

    resp.data     <= rdata_i;
    resp.rack     <= rack;
    resp.rack_tag <= rack_tag;
    resp.dack_tag <= dack_tag;

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
			rack      <= '1';
			rack_tag  <= req.tag;
			tag		  <= req.tag;
			rwn_i     <= req.read_writen;
			sram_d_t  <= '0'; --not req.read_writen;
			sram_d_o  <= req.data;
            
            mem_a_i(12 downto 0)  <= std_logic_vector(req.address(24 downto 12)); -- 13 row bits
            mem_a_i(14 downto 13) <= std_logic_vector(req.address(11 downto 10)); --  2 bank bits
            col_addr              <= std_logic_vector(req.address( 9 downto  0)); -- 10 column bits
            SDRAM_CSn  <= '0';
            SDRAM_RASn <= '0';
            SDRAM_CASn <= '1';
            SDRAM_WEn  <= '1'; -- Command = ACTIVE
            sram_d_t   <= '0'; -- no data yet
            delay      <= 1;
            state <= sd_cas;                            
        end procedure;
    begin
            
        if rising_edge(clock) then
            rack     <= '0';
			dack     <= '0';
			rack_tag <= (others => '0');
			dack_tag <= (others => '0');
            dout_sel <= '0';
            inhibit_d   <= inhibit;
            
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
				if do_refresh='1' and not (inhibit_d='1' or inhibit='1') then
				    send_refresh_cmd;
				elsif inhibit='0' then
    				if req_i='1' and refr_delay = 0 then
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
                    if rwn_i = '1' then -- read
    					dack     <= '1';
    					dack_tag <= tag;
                    end if;
                    state    <= idle;
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
                state        <= boot;
				sram_d_t     <= '0';
				delay        <= 0;
				tag		     <= (others => '0');
                do_refresh   <= '0';
                boot_cnt     <= SDRAM_WakeupTime-1;
                init_cnt     <= 0;
                enable_sdram <= '1';
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
