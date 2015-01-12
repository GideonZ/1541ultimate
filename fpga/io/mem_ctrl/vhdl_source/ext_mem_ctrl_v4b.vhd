-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM (burst capable)
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

entity ext_mem_ctrl_v4b is
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
end ext_mem_ctrl_v4b;    

-- ADDR: 25 24 23 ...
--        0  X  X ... SDRAM (32MB)

architecture Gideon of ext_mem_ctrl_v4b is
    --                                                          SRCW
    constant c_cmd_inactive  : std_logic_vector(3 downto 0) := "1111";
    constant c_cmd_nop       : std_logic_vector(3 downto 0) := "0111";
    constant c_cmd_active    : std_logic_vector(3 downto 0) := "0011";
    constant c_cmd_read      : std_logic_vector(3 downto 0) := "0101";
    constant c_cmd_write     : std_logic_vector(3 downto 0) := "0100";
    constant c_cmd_bterm     : std_logic_vector(3 downto 0) := "0110";
    constant c_cmd_precharge : std_logic_vector(3 downto 0) := "0010";
    constant c_cmd_refresh   : std_logic_vector(3 downto 0) := "0001";
    constant c_cmd_mode_reg  : std_logic_vector(3 downto 0) := "0000";

    type t_init is record
        addr    : std_logic_vector(15 downto 0);
        cmd     : std_logic_vector(3 downto 0);
    end record;
    type t_init_array is array(natural range <>) of t_init;
    constant c_init_array : t_init_array(0 to 7) := (
        ( X"0400", c_cmd_precharge ),
        ( X"0222", c_cmd_mode_reg ), -- mode register, burstlen=4, writelen=1, CAS lat = 2
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ) );

    type t_state is (boot, init, idle, sd_read, read_single, read_single_end, sd_write, wait_for_precharge, delay_to_terminate);
    signal state          : t_state;
    signal sdram_cmd      : std_logic_vector(3 downto 0) := "1111";
    signal sdram_d_o      : std_logic_vector(MEM_D'range) := (others => '1');
    signal sdram_d_t      : std_logic := '0';
    signal delay          : integer range 0 to 15;
    signal rwn_i	      : std_logic;
    signal tag		      : std_logic_vector(req.tag'range);
    signal mem_a_i        : std_logic_vector(MEM_A'range) := (others => '0');
    signal col_addr       : std_logic_vector(9 downto 0) := (others => '0');
    signal refresh_cnt    : integer range 0 to SDRAM_Refr_period-1;
    signal do_refresh     : std_logic := '0';
    signal refresh_inhibit: std_logic := '0';
    signal not_clock      : std_logic;
    signal rdata_i        : std_logic_vector(7 downto 0) := (others => '0');
    signal refr_delay     : integer range 0 to 3;
    signal boot_cnt       : integer range 0 to SDRAM_WakeupTime-1 := SDRAM_WakeupTime-1;
    signal init_cnt       : integer range 0 to c_init_array'high;
    signal enable_sdram   : std_logic := '1';

    signal req_i          : std_logic;
    signal dack_count     : unsigned(1 downto 0) := "00";
    signal count_out      : unsigned(1 downto 0) := "00";
    signal dack           : std_logic := '0';
    signal dack_pre       : std_logic := '0';
    signal rack           : std_logic := '0';
    signal dack_tag_pre   : std_logic_vector(req.tag'range) := (others => '0');
    signal rack_tag       : std_logic_vector(req.tag'range) := (others => '0');
    signal dack_tag       : std_logic_vector(req.tag'range) := (others => '0');


--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";

--    attribute register_duplication : string;
--    attribute register_duplication of mem_a_i  : signal is "no";

    attribute iob : string;
    attribute iob of rdata_i   : signal is "true"; -- the general memctrl/rdata must be packed in IOB
    attribute iob of sdram_cmd : signal is "true";
    attribute iob of mem_a_i   : signal is "true";
    attribute iob of SDRAM_CKE : signal is "false";

begin
    is_idle <= '1' when state = idle else '0';

    req_i <= req.request;

    resp.data     <= rdata_i;
    resp.rack     <= rack;
    resp.rack_tag <= rack_tag;
    resp.dack_tag <= dack_tag;
    resp.count    <= count_out;
    
    process(clock)
        procedure send_refresh_cmd is
        begin
            do_refresh <= '0';
            sdram_cmd  <= c_cmd_refresh;
            refr_delay <= 3; 
        end procedure;

        procedure accept_req is
        begin
			rack       <= '1';
			rack_tag   <= req.tag;
			tag		   <= req.tag;
			rwn_i      <= req.read_writen;
            dack_count <= req.size;
			sdram_d_o  <= req.data;
            
            mem_a_i(12 downto 0)  <= std_logic_vector(req.address(24 downto 12)); -- 13 row bits
            mem_a_i(14 downto 13) <= std_logic_vector(req.address(11 downto 10)); --  2 bank bits
            col_addr              <= std_logic_vector(req.address( 9 downto  0)); -- 10 column bits
            sdram_cmd             <= c_cmd_active;
        end procedure;
    begin
            
        if rising_edge(clock) then
            rack     <= '0';
			rack_tag <= (others => '0');
			dack_pre <= '0';
			dack_tag_pre <= (others => '0');
			dack     <= dack_pre;
			dack_tag <= dack_tag_pre;

            if dack='1' then
                count_out <= count_out + 1;
            else
                count_out <= "00";
            end if;

            rdata_i  <= MEM_D;  -- clock in

            sdram_cmd  <= c_cmd_inactive;
            SDRAM_CKE  <= enable_sdram;
            sdram_d_t  <= '0';
            
            if refr_delay /= 0 then
                refr_delay <= refr_delay - 1;
            end if;

            if delay /= 0 then
                delay <= delay - 1;
            end if;

            if inhibit='1' then
                refresh_inhibit <= '1';
            end if;

            case state is
            when boot =>
                refresh_inhibit <= '0';
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
                sdram_cmd(3) <= '1';
                sdram_cmd(2 downto 0) <= c_init_array(init_cnt).cmd(2 downto 0);
                if delay = 0 then
                    delay <= 7;
                    sdram_cmd(3) <= '0';
                    if init_cnt = c_init_array'high then
                        state <= idle;
                    else
                        init_cnt <= init_cnt + 1;
                    end if;
                end if;

            when idle =>
                -- first cycle after inhibit goes 1, should not be a refresh
                -- this enables putting cartridge images in sdram, because we guarantee the first access after inhibit to be a cart cycle
				if do_refresh='1' and refresh_inhibit='0' then
				    send_refresh_cmd;
				elsif inhibit='0' then -- make sure we are allowed to start a new cycle
    				if req_i='1' and refr_delay = 0 then
                        accept_req;
                        refresh_inhibit <= '0';
                        if req.read_writen = '1' then
                            state <= sd_read;
                        else
                            state <= sd_write;
                        end if;
                    end if;
                end if;
					
            when sd_read =>
                mem_a_i(10) <= '0'; -- no auto precharge
                mem_a_i(9 downto 0) <= col_addr;
                sdram_cmd <= c_cmd_read;
                if dack_count = "00" then
                    state <= read_single;
                else
                    state <= delay_to_terminate;
                end if;
            
            when read_single =>
                dack_pre <= '1';
                dack_tag_pre <= tag;
                sdram_cmd <= c_cmd_bterm;
                state <= read_single_end;
                                
            when read_single_end =>
                sdram_cmd <= c_cmd_precharge;
                state <= idle;
                            
            when delay_to_terminate =>
                dack_pre <= '1';
                dack_tag_pre <= tag;
                dack_count <= dack_count - 1;
                delay <= 2;
                if dack_count = "00" then
                    sdram_cmd <= c_cmd_precharge;
                    state <= idle;
                end if;
                                    
            when sd_write =>
                if delay = 0 then
                    mem_a_i(10) <= '1'; -- auto precharge
                    mem_a_i(9 downto 0) <= col_addr;
                    sdram_cmd  <= c_cmd_write;
                    sdram_d_t  <= '1';
                    delay <= 2;
                    state <= wait_for_precharge;
                end if;
                
            when wait_for_precharge =>
                if delay = 0 then
                    state <= idle;
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
				sdram_d_t    <= '0';
				delay        <= 0;
				tag		     <= (others => '0');
                do_refresh   <= '0';
                boot_cnt     <= SDRAM_WakeupTime-1;
                init_cnt     <= 0;
                enable_sdram <= '1';
                refresh_inhibit <= '0';
            end if;
        end if;
    end process;
    
    MEM_D      <= sdram_d_o when sdram_d_t='1' else (others => 'Z');
    MEM_A      <= mem_a_i;
    SDRAM_CSn  <= sdram_cmd(3);
    SDRAM_RASn <= sdram_cmd(2);
    SDRAM_CASn <= sdram_cmd(1);
    SDRAM_WEn  <= sdram_cmd(0);

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

-- ACT to READ: tRCD = 20 ns ( = 1 CLK)
-- ACT to PRCH: tRAS = 44 ns ( = 3 CLKs)
-- ACT to ACT:  tRC  = 66 ns ( = 4 CLKs)
-- ACT to ACTb: tRRD = 15 ns ( = 1 CLK)
-- PRCH time;   tRP  = 20 ns ( = 1 CLK)
-- wr. recov.   tWR=8ns+1clk ( = 2 CLKs) (starting from last data word)

-- CL=2
--      0 1 2 3 4 5 6 7 8 9 
-- BL1  A R P + 
--      - - - D
-- +: ONLY if same bank, otherwise we don't meet tRC.

-- BL2  A R - P
--      - - - D D

-- BL3  A R - - P
--      - - - D D D

-- BL4  A r - - - p
--      - - - D D D D

-- BL1W A(W)= = p
--      - D - - -

-- BL4  A r - - - p - A
--      - - - D D D D

-- BL1W A(W)= = p
--      - D - - -

