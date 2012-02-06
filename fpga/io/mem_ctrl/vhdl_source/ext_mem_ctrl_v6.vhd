-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 16 bit (burst of 2), externally 4x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library unisim;
    use unisim.vcomponents.all;

library work;
    use work.mem_bus_pkg.all;

entity ext_mem_ctrl_v6 is
generic (
    g_simulation       : boolean := false;
    g_read_fifo        : boolean := false;
    q_tcko_data        : time := 100 ps;
    A_Width            : integer := 13;
    SDRAM_WakeupTime   : integer := 40;     -- refresh periods
    SDRAM_Refr_period  : integer := 375 );
port (
    clock       : in  std_logic := '0';
    clk_2x      : in  std_logic := '0';
    reset       : in  std_logic := '0';

    inhibit     : in  std_logic := '0';
    is_idle     : out std_logic;

--    clk_4x      : in  std_logic := '0';
--    dummy       : out std_logic;

    req         : in  t_mem_burst_16_req;
    resp        : out t_mem_burst_16_resp;

	SDRAM_CLK	: out std_logic;
	SDRAM_CKE	: out std_logic := '0';
    SDRAM_CSn   : out std_logic := '1';
	SDRAM_RASn  : out std_logic := '1';
	SDRAM_CASn  : out std_logic := '1';
	SDRAM_WEn   : out std_logic := '1';
    SDRAM_DQM   : out std_logic := '0';

    SDRAM_BA    : out   std_logic_vector(1 downto 0);
    SDRAM_A     : out   std_logic_vector(A_Width-1 downto 0);
    SDRAM_DQ    : inout std_logic_vector(7 downto 0) := (others => 'Z'));

end ext_mem_ctrl_v6;    


architecture Gideon of ext_mem_ctrl_v6 is
    type t_init is record
        addr    : std_logic_vector(15 downto 0);
        cmd     : std_logic_vector(2 downto 0);  -- we-cas-ras
    end record;
    type t_init_array is array(natural range <>) of t_init;
    constant c_init_array : t_init_array(0 to 7) := (
        ( X"0400", "010" ), -- auto precharge
        ( X"002B", "000" ), -- mode register, burstlen=8, writelen=8, CAS lat = 2, interleaved
        ( X"0000", "100" ), -- auto refresh
        ( X"0000", "100" ), -- auto refresh
        ( X"0000", "100" ), -- auto refresh
        ( X"0000", "100" ), -- auto refresh
        ( X"0000", "100" ), -- auto refresh
        ( X"0000", "100" ) );

    type t_ints is array(natural range <>) of integer;
    constant c_delays : t_ints(0 to 15) := ( 
     2, 4, 2, 3,  -- R2R (other row&other bank, other row, other bank, same row+bank)
     4, 5, 4, 5,  -- R2W
     2, 5, 2, 3,  -- W2R
     2, 4, 2, 3 );-- W2W

    type t_state is (boot, init, idle, sd_cas );
    signal state          : t_state;

    signal sdram_d_o      : std_logic_vector(SDRAM_DQ'range) := (others => '1');
    signal sdram_d_t      : std_logic_vector(SDRAM_DQ'range) := (others => '1');
    signal wdata_tri      : std_logic_vector(8 downto 0) := (others => '1');
    signal delay          : integer range 0 to 15;
    signal inhibit_d      : std_logic;
    signal mem_a_i        : std_logic_vector(SDRAM_A'range) := (others => '0');
    signal mem_ba_i       : std_logic_vector(SDRAM_BA'range) := (others => '0');
    signal cs_n_i         : std_logic := '1';
    signal col_addr       : std_logic_vector(9 downto 0) := (others => '0');
    signal refresh_cnt    : integer range 0 to SDRAM_Refr_period-1;
    signal do_refresh     : std_logic := '0';
    signal do_refresh_d   : std_logic := '0';
    signal trigger_refresh  : std_logic := '0';
    signal not_clock      : std_logic;
    signal not_clock_2x   : std_logic;
    signal rdata_lo       : std_logic_vector(7 downto 0) := (others => '0');
    signal rdata_hi       : std_logic_vector(7 downto 0) := (others => '0');
    signal rdata_hi_d     : std_logic_vector(7 downto 0) := (others => '0');
    signal wdata          : std_logic_vector(17 downto 0) := (others => '0');
    signal wdata_i        : std_logic_vector(17 downto 0) := (others => '0');
    signal wdata_av       : std_logic;
    signal fifo_wdata_in    : std_logic_vector(17 downto 0);
    signal wdqm             : std_logic_vector(1 downto 0);
    signal dqm_override     : std_logic := '1';
    
--    signal refr_delay     : integer range 0 to 7;
    signal next_delay     : integer range 0 to 7;
    signal boot_cnt       : integer range 0 to SDRAM_WakeupTime-1 := SDRAM_WakeupTime-1;
    signal init_cnt       : integer range 0 to c_init_array'high;
    signal enable_sdram   : std_logic := '1';

    signal req_i          : std_logic;
    signal rack           : std_logic;
    signal dack           : std_logic_vector(5 downto 0) := "000000";
    signal burst_start    : std_logic_vector(5 downto 0) := "000000";
    
    signal dnext          : std_logic_vector(3 downto 0) := "0000";

    signal last_bank      : std_logic_vector(1 downto 0) := "10";
    signal addr_bank      : std_logic_vector(1 downto 0);
    signal same_bank      : std_logic;
    signal last_row       : std_logic_vector(12 downto 0) := "0101011010101";
    signal addr_row       : std_logic_vector(12 downto 0);
    signal same_row       : std_logic;
    signal addr_column    : std_logic_vector(9 downto 0);
    signal next_activate  : std_logic;
        
--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";

--    attribute register_duplication : string;
--    attribute register_duplication of mem_a_i  : signal is "no";

    attribute iob : string;
    attribute iob of SDRAM_CKE  : signal is "false";
    attribute iob of SDRAM_A    : signal is "true";
    attribute iob of SDRAM_BA   : signal is "true";
    attribute iob of SDRAM_RASn : signal is "true";
    attribute iob of SDRAM_CASn : signal is "true";
    attribute iob of SDRAM_WEn  : signal is "true";

    constant c_address_width    : integer := req.address'length;
    constant c_data_width       : integer := req.data'length;

    signal cmd_fifo_data_in     : std_logic_vector(c_address_width downto 0);
    signal cmd_fifo_data_out    : std_logic_vector(c_address_width downto 0);
    
    signal rwn_fifo             : std_logic;
    signal rwn_i	            : std_logic := '1';
    signal tag_fifo             : std_logic_vector(7 downto 0);
    signal address_fifo         : std_logic_vector(c_address_width-1 downto 0);
    signal cmd_af               : std_logic;
    signal cmd_av               : std_logic;
    signal rdata_af             : std_logic := '0'; -- forced low for when there is no fifo
    signal push_cmd             : std_logic;
    signal push_read_cmd        : std_logic;
    signal crazy_index_slv      : std_logic_vector(3 downto 0);
    signal crazy_index          : integer range 0 to 15;

    signal sampled_dq           : std_logic_vector(7 downto 0);
begin

    is_idle <= '1' when state = idle else '0';

    req_i         <= cmd_av and not do_refresh_d;
    push_cmd      <= req.request and not cmd_af;
    push_read_cmd <= push_cmd and req.read_writen;
    resp.ready    <= not cmd_af;

    cmd_fifo_data_in <= req.read_writen & std_logic_vector(req.address);
    address_fifo     <= cmd_fifo_data_out(address_fifo'range);
    rwn_fifo         <= cmd_fifo_data_out(address_fifo'length);
    
    addr_bank   <= address_fifo(14 downto 13);
    addr_row    <= address_fifo(24 downto 15) & address_fifo(12 downto 10);
    addr_column <= address_fifo( 9 downto 0);

    i_command_fifo: entity work.srl_fifo
    generic map (
        Width     => c_address_width + 1,
        Depth     => 15,
        Threshold => 3)
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => rack,
        PutElement  => push_cmd,
        FlushFifo   => '0',
        DataIn      => cmd_fifo_data_in,
        DataOut     => cmd_fifo_data_out,
        SpaceInFifo => open,
        AlmostFull  => cmd_af,
        DataInFifo  => cmd_av );

    i_tag_fifo: entity work.srl_fifo
    generic map (
        Width     => 8,
        Depth     => 15,
        Threshold => 3)
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => burst_start(1),
        PutElement  => push_read_cmd,
        FlushFifo   => '0',
        DataIn      => req.request_tag,
        DataOut     => tag_fifo,
        SpaceInFifo => open,
        AlmostFull  => open,
        DataInFifo  => open );

    r_read_fifo: if g_read_fifo generate
        i_read_fifo: entity work.srl_fifo
        generic map (
            Width     => c_data_width,
            Depth     => 15,
            Threshold => 6 )
        port map (
            clock       => clock,
            reset       => reset,
            GetElement  => req.data_pop,
            PutElement  => dack(0),
            FlushFifo   => '0',
            DataIn(15 downto 8) => rdata_lo,
            DataIn(7 downto 0)  => rdata_hi,
            DataOut     => resp.data,
            SpaceInFifo => open,
            AlmostFull  => rdata_af,
            DataInFifo  => resp.rdata_av );

    end generate;

    r_read_direct: if not g_read_fifo generate
        resp.data       <= rdata_lo & rdata_hi;
        resp.rdata_av   <= dack(0);
    end generate;

    fifo_wdata_in <= req.byte_en & req.data;
    wdqm <= (others => '1') when dqm_override='1' else
            (others => '0') when dnext(0)='0' else not wdata(17 downto 16);

    i_write_fifo: entity work.SRL_fifo
    generic map (
        Width     => (c_data_width*9)/8,
        Depth     => 15,
        Threshold => 6 )
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => dnext(0),
        PutElement  => req.data_push,
        FlushFifo   => '0',
        DataIn      => fifo_wdata_in,
        DataOut     => wdata_i,
        SpaceInFifo => open,
        AlmostFull  => resp.wdata_full,
        DataInFifo  => wdata_av );
    
    wdata <= wdata_i after 1 ns;
    
    same_row  <= '1' when addr_row  = last_row else '0';
    same_bank <= '1' when addr_bank = last_bank else '0';
    crazy_index_slv <= not rwn_i & not rwn_fifo & same_row & same_bank;
    crazy_index     <= to_integer(unsigned(crazy_index_slv));
    trigger_refresh <= do_refresh_d and not (inhibit_d or inhibit);
    
    process(clock)
        procedure send_refresh_cmd is
        begin
            if next_delay = 0 then
                do_refresh <= '0';
                do_refresh_d <= '0';
                cs_n_i  <= '0' after 1 ns;
                SDRAM_RASn <= '0';
                SDRAM_CASn <= '0';
                SDRAM_WEn  <= '1'; -- Auto Refresh
                next_delay <= 3;
            end if;
        end procedure;

        procedure accept_req is
        begin
			rwn_i     <= rwn_fifo;
            col_addr  <= addr_column;
            last_bank <= addr_bank;
            last_row  <= addr_row;
            mem_a_i(addr_row'range)  <= addr_row;
            mem_ba_i    <= addr_bank;
            cs_n_i     <= '0' after 1 ns;
            SDRAM_RASn <= '0';
            SDRAM_CASn <= '1';
            SDRAM_WEn  <= '1'; -- Command = ACTIVE
            delay      <= 0;
            state      <= sd_cas;                            
        end procedure;

        procedure issue_read_or_write is
        begin
            mem_a_i(9 downto 0) <= col_addr;
            do_refresh_d <= do_refresh;

            if req_i='0' or do_refresh='1' then
                if rwn_i='0' then
                    next_delay <= 5;
                else
                    next_delay <= 4;
                end if;
                mem_a_i(10) <= '1'; -- auto precharge
                next_activate <= '1';
            else                        
                next_delay <= c_delays(crazy_index);
                mem_a_i(10) <= not (same_row and same_bank); -- do not AP when we'll continue in same row
                next_activate <= not (same_row and same_bank); -- only activate next time if we also AP.
            end if;

            if delay=0 then
                if rwn_i='0' then
                    if wdata_av='1' then
                        wdata_tri(7 downto 0) <= (others => '0') after 1 ns;
                        cs_n_i      <= '0' after 1 ns;
                        SDRAM_RASn  <= '1';
                        SDRAM_CASn  <= '0';
                        SDRAM_WEn   <= '0';
                        dnext       <= "1111" after 1 ns;
                        state       <= idle;
                    end if;
                else
                    if rdata_af='0' then
                        cs_n_i      <= '0' after 1 ns;
                        SDRAM_RASn  <= '1';
                        SDRAM_CASn  <= '0';
                        SDRAM_WEn   <= '1';
                        dack(dack'high downto dack'high-3) <= (others => '1');
                        burst_start(2) <= '1';
                        state       <= idle;
                    end if;
                end if;
            end if;
        end procedure;        

    begin
            
        if rising_edge(clock) then
            inhibit_d    <= inhibit;
            rdata_hi_d   <= rdata_hi;
            cs_n_i       <= '1' after 1 ns;
            SDRAM_CKE    <= enable_sdram;
            SDRAM_RASn   <= '1';
            SDRAM_CASn   <= '1';
            SDRAM_WEn    <= '1';

            if burst_start(1)='1' then
                resp.data_tag <= tag_fifo;
            end if;
            
            if next_delay /= 0 then
                next_delay <= next_delay - 1;
            end if;
            if delay /= 0 then
                delay <= delay - 1;
            end if;
            
            wdata_tri   <= "11" & wdata_tri(wdata_tri'high downto 2) after 1 ns;
            dack        <= '0' & dack(dack'high downto 1);
            burst_start <= '0' & burst_start(burst_start'high downto 1);
            dnext       <= '0' & dnext(dnext'high downto 1) after 1 ns;

            case state is
            when boot =>
                enable_sdram <= '1';
                if g_simulation then
                    state <= init;
                elsif refresh_cnt = 0 then
                    boot_cnt <= boot_cnt - 1;
                    if boot_cnt = 1 then
                        state <= init;
                    end if;
                end if;

            when init =>
                mem_a_i    <= c_init_array(init_cnt).addr(mem_a_i'range);
                mem_ba_i   <= (others => '0'); -- for DDR and such, maybe the upper 2/3 bits
                SDRAM_RASn <= c_init_array(init_cnt).cmd(0);
                SDRAM_CASn <= c_init_array(init_cnt).cmd(1);
                SDRAM_WEn  <= c_init_array(init_cnt).cmd(2);
                if next_delay = 0 then
                    next_delay <= 7;
                    cs_n_i <= '0' after 1 ns;
                    if init_cnt = c_init_array'high then
                        state <= idle;
                        dqm_override <= '0';
                    else
                        init_cnt <= init_cnt + 1;
                    end if;
                end if;

            when idle =>
                -- first cycle after inhibit goes 0, do not do refresh
                -- this enables putting cartridge images in sdram
				if trigger_refresh='1' then
				    send_refresh_cmd;
				elsif inhibit='0' then
				    if req_i='1' then
                        if next_activate='1' and next_delay=0 then
                            accept_req;
                        elsif next_activate='0' and next_delay=1 then
                			rwn_i     <= rwn_fifo;
                            col_addr  <= addr_column;
                            state     <= sd_cas;
                        end if;
                    else
                        do_refresh_d <= do_refresh;
                    end if;
                end if;
            
			when sd_cas =>
                issue_read_or_write;

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
                resp.data_tag <= (others => '0');
                dqm_override  <= '1';
                state         <= boot;
				wdata_tri     <= (others => '0');
				delay         <= 0;
                next_delay    <= 0;
                do_refresh    <= '0';
                do_refresh_d  <= '0';
                boot_cnt      <= SDRAM_WakeupTime-1;
                init_cnt      <= 0;
                enable_sdram  <= '1';
                next_activate <= '1';
                rwn_i         <= '1';
            end if;
        end if;
    end process;
    
    -- Generate rack; the signal that indicates that a request is going to be issued
    -- and thus taken from the command fifo.
    process(state, trigger_refresh, inhibit, req_i, next_delay, next_activate)
    begin
        rack <= '0';
        case state is
            when idle =>
                -- first cycle after inhibit goes 0, do not do refresh
                -- this enables putting cartridge images in sdram
				if trigger_refresh='1' then
				    null;
				elsif inhibit='0' and req_i='1' then
    				if next_activate='1' and next_delay = 0 then
                        rack <= '1';
                    elsif next_activate='0' and next_delay = 1 then
                        rack <= '1';
                    end if;
                end if;
            when others =>
                null;
        end case;
    end process;

    SDRAM_A       <= mem_a_i;
    SDRAM_BA      <= mem_ba_i;
    
    not_clock_2x <= not clk_2x;
    not_clock    <= not clock;
    
    clkout: FDDRRSE
	port map (
		CE => '1',
		C0 => clk_2x,
		C1 => not_clock_2x,
		D0 => '0',
		D1 => enable_sdram,
		Q  => SDRAM_CLK,
		R  => '0',
		S  => '0' );

    r_data: for i in 0 to 7 generate
        i_dout: entity work.my_ioddr
    	port map (
    		pin   => SDRAM_DQ(i),
    		clock => clock,
    		D0    => wdata(8+i),
    		D1    => wdata(i),
    		T0    => wdata_tri(1),
    		T1    => wdata_tri(0),
    		Q0    => rdata_hi(i),
    		Q1    => rdata_lo(i) );

    end generate;

    select_out: ODDR2
	generic map (
		DDR_ALIGNMENT => "NONE",
		SRTYPE        => "SYNC" )
	port map (
		CE => '1',
		C0 => clock,
		C1 => not_clock,
		D0 => '1',
		D1 => cs_n_i,
		Q  => SDRAM_CSn,
		R  => '0',
		S  => '0' );

    i_dqm_out: ODDR2
	generic map (
		DDR_ALIGNMENT => "NONE",
		SRTYPE        => "SYNC" )
	port map (
		Q  => SDRAM_DQM,
		C0 => clock,
		C1 => not_clock,
		CE => '1',
		D0 => wdqm(1),
		D1 => wdqm(0),
		R  => '0',
		S  => '0' );

end Gideon;
