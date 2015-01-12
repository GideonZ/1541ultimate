-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 32 bit (single beat), externally 4x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_arith.all;
    use ieee.std_logic_unsigned.all;

library unisim;
    use unisim.vcomponents.all;

library work;
    use work.mem_bus_pkg.all;

entity ext_mem_ctrl_v5 is
generic (
    g_simulation       : boolean := false;
    SDRAM_WakeupTime   : integer := 40;     -- refresh periods
    SDRAM_Refr_period  : integer := 375 );
port (
    clock       : in  std_logic := '0';
    clk_2x      : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : in  std_logic;
    is_idle     : out std_logic;

    req         : in  t_mem_req_32;
    resp        : out t_mem_resp_32;

	SDRAM_CLK	: out std_logic;
	SDRAM_CKE	: out std_logic;
    SDRAM_CSn   : out std_logic := '1';
	SDRAM_RASn  : out std_logic := '1';
	SDRAM_CASn  : out std_logic := '1';
	SDRAM_WEn   : out std_logic := '1';
    SDRAM_DQM   : out std_logic := '0';

    SDRAM_A     : out   std_logic_vector(12 downto 0);
    SDRAM_BA    : out   std_logic_vector(1 downto 0);
    SDRAM_DQ    : inout std_logic_vector(7 downto 0) := (others => 'Z'));
end ext_mem_ctrl_v5;    

-- ADDR: 25 24 23 ...
--        0  X  X ... SDRAM (32MB)

architecture Gideon of ext_mem_ctrl_v5 is
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
        ( X"0032", c_cmd_mode_reg ), -- mode register, burstlen=4, writelen=4, CAS lat = 3
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ),
        ( X"0000", c_cmd_refresh ) );

    signal not_clock_2x   : std_logic;
    signal not_clock      : std_logic;

    type t_state is (boot, init, idle, sd_read, sd_read_2, sd_read_3, sd_write, sd_write_2, sd_write_3);


    signal sdram_d_o      : std_logic_vector(7 downto 0);
    signal sdram_t_o      : std_logic_vector(7 downto 0);
    
    signal rdata          : std_logic_vector(15 downto 0);
    signal rdata_lo       : std_logic_vector(7 downto 0);
    signal rdata_hi       : std_logic_vector(7 downto 0);
    
--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";

--    attribute register_duplication : string;
--    attribute register_duplication of mem_a_i  : signal is "no";

    attribute iob : string;
    attribute iob of SDRAM_RASn : signal is "true";
    attribute iob of SDRAM_CASn : signal is "true";
    attribute iob of SDRAM_WEn  : signal is "true";
    attribute iob of SDRAM_BA   : signal is "true";
    attribute iob of SDRAM_A    : signal is "true";
    attribute iob of SDRAM_CKE  : signal is "false";

    attribute IFD_DELAY_VALUE : string;
    attribute IFD_DELAY_VALUE of SDRAM_DQ : signal is "1";

    type t_output is record
        sdram_cmd       : std_logic_vector(3 downto 0);
        sdram_cke       : std_logic;
        sdram_a         : std_logic_vector(12 downto 0);
        sdram_ba        : std_logic_vector(1 downto 0);

        tri             : std_logic_vector(1 downto 0);
        wmask_16        : std_logic_vector(1 downto 0);
        wdata_16        : std_logic_vector(15 downto 0);
    end record;
    
    type t_internal_state is record
        state           : t_state;
        enable_sdram    : std_logic;
        col_addr        : std_logic_vector(9 downto 0);
        bank_addr       : std_logic_vector(1 downto 0);
        refresh_cnt     : integer range 0 to SDRAM_Refr_period-1;
        refr_delay      : integer range 0 to 3;
        delay           : integer range 0 to 7;
        do_refresh      : std_logic;
        refresh_inhibit : std_logic;
        tag             : std_logic_vector(req.tag'range);
        rack            : std_logic;
        rack_tag        : std_logic_vector(req.tag'range);
        dack            : std_logic;
        dack_tag        : std_logic_vector(req.tag'range);
        dack_pre        : std_logic;
        dack_tag_pre    : std_logic_vector(req.tag'range);
        boot_cnt        : integer range 0 to SDRAM_WakeupTime-1;
        init_cnt        : integer range 0 to c_init_array'high;
        wdata           : std_logic_vector(31 downto 0);
        wmask           : std_logic_vector(3 downto 0);
    end record;
    
    constant c_internal_state_init : t_internal_state := (
        state           => boot,
        enable_sdram    => '0',
        col_addr        => (others => '0'),
        bank_addr       => (others => '0'),
        refresh_cnt     => SDRAM_Refr_period-1,
        refr_delay      => 3,
        delay           => 7,
        do_refresh      => '0',
        refresh_inhibit => '0',
        tag             => (others => '0'),
        rack            => '0',
        rack_tag        => (others => '0'),
        dack            => '0',
        dack_tag        => (others => '0'),
        dack_pre        => '0',
        dack_tag_pre    => (others => '0'),
        boot_cnt        => SDRAM_WakeupTime-1,
        init_cnt        => c_init_array'high,
        wdata           => (others => '0'),
        wmask           => (others => '0')
    );    

    signal outp         : t_output;
    signal cur          : t_internal_state := c_internal_state_init;
    signal nxt          : t_internal_state;
begin
    is_idle <= '1' when cur.state = idle else '0';

    resp.data     <= rdata & rdata_hi & rdata_lo;
    resp.rack     <= cur.rack;
    resp.rack_tag <= cur.rack_tag;
    resp.dack_tag <= cur.dack_tag;
    
    process(req, inhibit, cur)
        procedure send_refresh_cmd is
        begin
            outp.sdram_cmd  <= c_cmd_refresh;
            nxt.do_refresh <= '0';
            nxt.refr_delay <= 3; 
        end procedure;

        procedure accept_req is
        begin
            nxt.rack       <= '1';
            nxt.rack_tag   <= req.tag;
            nxt.tag        <= req.tag;
            nxt.wdata      <= req.data;
            nxt.wmask      <= not req.byte_en;
            
            nxt.col_addr   <= std_logic_vector(req.address( 9 downto  0)); -- 10 column bits
            nxt.bank_addr  <= std_logic_vector(req.address(11 downto 10)); --  2 bank bits
            outp.sdram_ba  <= std_logic_vector(req.address(11 downto 10)); --  2 bank bits
            outp.sdram_a   <= std_logic_vector(req.address(24 downto 12)); -- 13 row bits
            outp.sdram_cmd <= c_cmd_active;
        end procedure;
    begin
        nxt <= cur; -- default no change

        nxt.rack         <= '0';
        nxt.rack_tag     <= (others => '0');
        nxt.dack_pre     <= '0';
        nxt.dack_tag_pre <= (others => '0');
        nxt.dack         <= cur.dack_pre;
        nxt.dack_tag     <= cur.dack_tag_pre;

        outp.sdram_cmd    <= c_cmd_inactive;
        outp.sdram_cke    <= cur.enable_sdram;
        outp.sdram_ba     <= (others => 'X');
        outp.sdram_a      <= (others => 'X');
        outp.tri          <= "11";
        outp.wmask_16     <= "00";
        outp.wdata_16     <= (others => 'X');
        
        if cur.refr_delay /= 0 then
            nxt.refr_delay <= cur.refr_delay - 1;
        end if;

        if cur.delay /= 0 then
            nxt.delay <= cur.delay - 1;
        end if;

        if inhibit='1' then
            nxt.refresh_inhibit <= '1';
        end if;

        case cur.state is
        when boot =>
            nxt.refresh_inhibit <= '0';
            nxt.enable_sdram <= '1';
            if cur.refresh_cnt = 0 then
                nxt.boot_cnt <= cur.boot_cnt - 1;
                if cur.boot_cnt = 1 then
                    nxt.state <= init;
                end if;
            elsif g_simulation then
                nxt.state <= idle;
            end if;

        when init =>
            nxt.do_refresh <= '0';
            outp.sdram_a  <= c_init_array(cur.init_cnt).addr(12 downto 0);
            outp.sdram_ba <= c_init_array(cur.init_cnt).addr(14 downto 13);
            outp.sdram_cmd(3) <= '1';
            outp.sdram_cmd(2 downto 0) <= c_init_array(cur.init_cnt).cmd(2 downto 0);
            if cur.delay = 0 then
                nxt.delay <= 7;
                if cur.init_cnt = c_init_array'high then
                    nxt.state <= idle;
                else
                    outp.sdram_cmd(3) <= '0';
                    nxt.init_cnt <= cur.init_cnt + 1;
                end if;
            end if;

        when idle =>
            -- first cycle after inhibit goes 1, should not be a refresh
            -- this enables putting cartridge images in sdram, because we guarantee the first access after inhibit to be a cart cycle
            if cur.do_refresh='1' and cur.refresh_inhibit='0' then
                send_refresh_cmd;
            elsif inhibit='0' then -- make sure we are allowed to start a new cycle
                if req.request='1' and cur.refr_delay = 0 then
                    accept_req;
                    nxt.refresh_inhibit <= '0';
                    if req.read_writen = '1' then
                        nxt.state <= sd_read;
                    else
                        nxt.state <= sd_write;
                    end if;
                end if;
            end if;
                
        when sd_read =>
            outp.sdram_ba <= cur.bank_addr;
            outp.sdram_a(12 downto 11) <= "00";
            outp.sdram_a(10) <= '1'; -- auto precharge
            outp.sdram_a(9 downto 0) <= cur.col_addr;
            outp.sdram_cmd <= c_cmd_read;
            nxt.state <= sd_read_2;
            
        when sd_read_2 =>
            nxt.state <= sd_read_3;
        
        when sd_read_3 =>
            nxt.dack_pre <= '1';
            nxt.dack_tag_pre <= cur.tag;
            nxt.state <= idle;
                    
        when sd_write =>
            outp.sdram_ba <= cur.bank_addr;
            outp.sdram_a(12 downto 11) <= "00";
            outp.sdram_a(10) <= '1'; -- auto precharge
            outp.sdram_a(9 downto 0) <= cur.col_addr;
            outp.sdram_cmd <= c_cmd_write;
            outp.wdata_16 <= cur.wdata(31 downto 24) & "XXXXXXXX";
            outp.wmask_16 <= cur.wmask(3) & "0";
            outp.tri      <= "01";
            nxt.state <= sd_write_2;

        when sd_write_2 =>
            outp.tri  <= "00";
            outp.wdata_16 <= cur.wdata(15 downto 8) & cur.wdata(23 downto 16);
            outp.wmask_16 <= cur.wmask(1) & cur.wmask(2);
            nxt.state <= sd_write_3;
                            
        when sd_write_3 =>
            outp.tri      <= "10";
            outp.wdata_16 <= "XXXXXXXX" & cur.wdata(7 downto 0);
            outp.wmask_16 <= "0" & cur.wmask(0);
            nxt.state <= idle;

        when others =>
            null;

        end case;

        if cur.refresh_cnt = SDRAM_Refr_period-1 then
            nxt.do_refresh  <= '1';
            nxt.refresh_cnt <= 0;
        else
            nxt.refresh_cnt <= cur.refresh_cnt + 1;
        end if;
    end process;
    
    process(clock)
    begin
        if rising_edge(clock) then
            cur <= nxt;

            SDRAM_A    <= outp.sdram_a;
            SDRAM_BA   <= outp.sdram_ba;
            SDRAM_RASn <= outp.sdram_cmd(2);
            SDRAM_CASn <= outp.sdram_cmd(1);
            SDRAM_WEn  <= outp.sdram_cmd(0);
            SDRAM_CKE  <= cur.enable_sdram;
            rdata      <= rdata_hi & rdata_lo;

            if reset='1' then
                cur.state        <= boot;
                cur.delay        <= 0;
                cur.tag          <= (others => '0');
                cur.do_refresh   <= '0';
                cur.boot_cnt     <= SDRAM_WakeupTime-1;
                cur.init_cnt     <= 0;
                cur.enable_sdram <= '1';
                cur.refresh_inhibit <= '0';
            end if;
        end if;
    end process;
    
    not_clock_2x <= not clk_2x;
    not_clock    <= not clock;
    
    clkout: FDDRRSE
	port map (
		CE => '1',
		C0 => clk_2x,
		C1 => not_clock_2x,
		D0 => '0',
		D1 => cur.enable_sdram,
		Q  => SDRAM_CLK,
		R  => '0',
		S  => '0' );

    select_out: FDDRRSE
	port map (
		CE => '1',
		C0 => clock,
		C1 => not_clock,
		D0 => outp.sdram_cmd(3),
		D1 => '1',
		Q  => SDRAM_CSn,
		R  => '0',
		S  => '0' );

    r_data: for i in 0 to 7 generate
        i_in: IDDR2
    	generic map (
    		DDR_ALIGNMENT => "NONE",
    		SRTYPE        => "SYNC"	)
    	port map (
    		Q0 => rdata_hi(i),
    		Q1 => rdata_lo(i),
    		C0 => clock,
    		C1 => not_clock,
    		CE => '1',
    		D =>  SDRAM_DQ(i),
    		R =>  reset,
    		S =>  '0');

        i_out: ODDR2
    	generic map (
    		DDR_ALIGNMENT => "NONE",
    		SRTYPE        => "SYNC" )
    	port map (
    		Q  => sdram_d_o(i),
    		C0 => clock,
    		C1 => not_clock,
    		CE => '1',
    		D0 => outp.wdata_16(8+i),
    		D1 => outp.wdata_16(i),
    		R  => reset,
    		S  => '0' );

        i_out_t: ODDR2
        generic map (
            DDR_ALIGNMENT => "NONE",
            SRTYPE        => "SYNC" )
        port map (
            Q  => sdram_t_o(i),
            C0 => clock,
            C1 => not_clock,
            CE => '1',
            D0 => outp.tri(1),
            D1 => outp.tri(0),
            R  => reset,
            S  => '0' );

        SDRAM_DQ(i) <= sdram_d_o(i) when sdram_t_o(i)='0' else 'Z';

    end generate;

    mask_out: ODDR2
    generic map (
        DDR_ALIGNMENT => "NONE",
        SRTYPE        => "SYNC" )
    port map (
        Q  => SDRAM_DQM,
        C0 => clock,
        C1 => not_clock,
        CE => '1',
        D0 => outp.wmask_16(1),
        D1 => outp.wmask_16(0),
        R  => reset,
        S  => '0' );

end Gideon;

-- 100 MHz
-- ACT to READ: tRCD = 20 ns ( = 2 CLKs)
-- ACT to PRCH: tRAS = 44 ns ( = 5 CLKs)
-- ACT to ACT:  tRC  = 66 ns ( = 7 CLKs)
-- ACT to ACTb: tRRD = 15 ns ( = 2 CLKs)
-- PRCH time;   tRP  = 20 ns ( = 2 CLKs)
-- wr. recov.   tWR=8ns+1clk ( = 2 CLKs) (starting from last data word)

-- CL=2
--      0 1 2 3 4 5 6 7 8 9 
-- BL1  A - R - - P + -      precharge on odd clock
--      - - - - D d d -
-- +: ONLY if same bank, a new ACT command can be given here. Otherwise we don't meet tRC.

-- BL4  A - r - - - p - 
--      - - - - D D D D

-- BL1W A - W - - P + -  (precharge on odd clock)
--      - - D - - - - -

-- BL4W A - W - - - p -
--      - - D D D D - -

-- Conclusion: In order to meet tRC, without checking for the bank, we always need 80 ns.
-- In order to optimize to 60 ns (using 20 ns logic ticks), we need to add both bank
-- number checking, as well as differentiate between 1 byte and 4 bytes. I think that
-- it is not worthwhile at this point to implement this, so we will use a very rigid
-- 4-tick schedule: one fits all
