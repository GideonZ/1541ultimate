-------------------------------------------------------------------------------
-- Title      : External Memory controller for DDR2 SDRAM
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single burst memory controller.
--              User interface is 32 bit (single beat), externally 4x 8 bit.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;

entity ddr2_ctrl is
generic (
    SDRAM_Refr_period  : integer := 488 );
port (
    ref_clock   : in  std_logic := '0';
    ref_reset   : in  std_logic := '0';

    sys_clock_o : out std_logic;
    sys_reset_o : out std_logic;

    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';

    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

    inhibit     : in  std_logic;
    is_idle     : out std_logic;

    req         : in  t_mem_req_32;
    resp        : out t_mem_resp_32;

	SDRAM_CLK	: inout std_logic;
	SDRAM_CLKn  : inout std_logic;

    -- command group
	SDRAM_CKE	: out   std_logic := '0'; -- bit 5
    SDRAM_ODT   : out   std_logic := '0';
    SDRAM_CSn   : out   std_logic := '1';
	SDRAM_RASn  : out   std_logic := '1';
	SDRAM_CASn  : out   std_logic := '1';
	SDRAM_WEn   : out   std_logic := '1'; -- bit 0

    -- address group
    SDRAM_A     : out   std_logic_vector(13 downto 0);
    SDRAM_BA    : out   std_logic_vector(1 downto 0);

    -- data group
    SDRAM_DM    : inout std_logic := 'Z';
    SDRAM_DQ    : inout std_logic_vector(7 downto 0) := (others => 'Z');

    -- dqs
    SDRAM_DQS   : inout std_logic := 'Z');
end entity;    

-- 16 + 6 = 22 address/command timing
-- 8 + 1 = 9 data timing

-- ADDR: 25 24 23 ...
--        0  X  X ... SDRAM (32MB)

architecture Gideon of ddr2_ctrl is
    constant c_cmd_inactive  : std_logic_vector(3 downto 0) := "1111";
    constant c_cmd_nop       : std_logic_vector(3 downto 0) := "0111";
    constant c_cmd_active    : std_logic_vector(3 downto 0) := "0011";
    constant c_cmd_read      : std_logic_vector(3 downto 0) := "0101";
    constant c_cmd_write     : std_logic_vector(3 downto 0) := "0100";
    constant c_cmd_bterm     : std_logic_vector(3 downto 0) := "0110";
    constant c_cmd_precharge : std_logic_vector(3 downto 0) := "0010";
    constant c_cmd_refresh   : std_logic_vector(3 downto 0) := "0001";
    constant c_cmd_mode_reg  : std_logic_vector(3 downto 0) := "0000";

    type t_state is (idle, sd_read, sd_write, sd_write_2, delay_1, delay_2);

    type t_output is record
        sdram_cmd       : std_logic_vector(3 downto 0);
        sdram_odt       : std_logic;
        sdram_cke       : std_logic;
        sdram_a         : std_logic_vector(13 downto 0);
        sdram_ba        : std_logic_vector(1 downto 0);

        write           : std_logic;
        wmask           : std_logic_vector(3 downto 0);
        wdata           : std_logic_vector(31 downto 0);
    end record;
    
    type t_internal_state is record
        state           : t_state;
        col_addr        : std_logic_vector(9 downto 0);
        bank_addr       : std_logic_vector(1 downto 0);
        refresh_cnt     : integer range 0 to SDRAM_Refr_period-1;
        refr_delay      : integer range 0 to 7;
        delay           : integer range 0 to 7;
        do_refresh      : std_logic;
        refresh_inhibit : std_logic;
        tag             : std_logic_vector(req.tag'range);
        rack            : std_logic;
        rack_tag        : std_logic_vector(req.tag'range);
        wmask           : std_logic_vector(3 downto 0);
        wdata           : std_logic_vector(31 downto 0);
    end record;
    
    constant c_internal_state_init : t_internal_state := (
        state           => idle,
        col_addr        => (others => '0'),
        bank_addr       => (others => '0'),
        refresh_cnt     => 0,
        refr_delay      => 3,
        delay           => 7,
        do_refresh      => '0',
        refresh_inhibit => '0',
        tag             => (others => '0'),
        rack            => '0',
        rack_tag        => (others => '0'),
        wmask           => (others => '0'),
        wdata           => (others => '0')
    );    

    type t_tag_array is array(natural range <>) of std_logic_vector(req.tag'range);
    signal dack_pipe    : t_tag_array(4 downto 0) := (others => (others => '0'));
    
    signal outp         : t_output;
    signal cur          : t_internal_state := c_internal_state_init;
    signal nxt          : t_internal_state;

    signal phasecounterselect : std_logic_vector(2 downto 0);
    signal phasestep          : std_logic_vector(3 downto 0);
    signal phaseupdown        : std_logic;
    signal phasedone          : std_logic;
    signal mode               : std_logic_vector(1 downto 0);
    signal measurement        : std_logic_vector(11 downto 0);
    signal addr_first         : std_logic_vector(21 downto 0);
    signal addr_second        : std_logic_vector(21 downto 0);
    signal phy_wdata          : std_logic_vector(35 downto 0);
    signal phy_rdata          : std_logic_vector(35 downto 0);
    signal phy_write          : std_logic;
    signal enable_sdram       : std_logic;
    signal rdata              : std_logic_vector(31 downto 0);
    signal do_read            : std_logic;
    signal ext_addr         : std_logic_vector(15 downto 0);
    signal ext_cmd          : std_logic_vector(3 downto 0);
    signal ext_cmd_valid    : std_logic;
    signal ext_cmd_done     : std_logic;
begin
    is_idle <= '1' when cur.state = idle else '0';

    resp.data     <= rdata;
    resp.rack     <= nxt.rack;  -- was cur
    resp.rack_tag <= nxt.rack_tag; -- was cur
    resp.dack_tag <= dack_pipe(dack_pipe'high);
    
    process(req, inhibit, cur, ext_cmd, ext_cmd_valid, ext_addr, enable_sdram)
        procedure send_refresh_cmd is
        begin
            outp.sdram_cmd  <= c_cmd_refresh;
            nxt.do_refresh <= '0';
            nxt.refr_delay <= 6; 
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
            outp.sdram_a   <= std_logic_vector(req.address(25 downto 12)); -- 14 row bits
            outp.sdram_cmd <= c_cmd_active;
        end procedure;
    begin
        nxt <= cur; -- default no change

        nxt.rack         <= '0';
        nxt.rack_tag     <= (others => '0');

        outp.sdram_cmd    <= c_cmd_inactive;
        outp.sdram_cke    <= enable_sdram;
        outp.sdram_odt    <= enable_sdram;
        outp.sdram_ba     <= (others => 'X');
        outp.sdram_a      <= (others => 'X');
        outp.wmask        <= "0000";
        outp.write        <= '0';
        outp.wdata        <= (others => 'X');
        
        ext_cmd_done <= '0';
        do_read <= '0';
        
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
        when idle =>
            -- first cycle after inhibit goes 1, should not be a refresh
            -- this enables putting cartridge images in sdram, because we guarantee the first access after inhibit to be a cart cycle
            if cur.do_refresh='1' and cur.refresh_inhibit='0' then
                send_refresh_cmd;
            elsif ext_cmd_valid = '1' and cur.refr_delay = 0 then
                outp.sdram_cmd <= ext_cmd;
                outp.sdram_a <= ext_addr(13 downto 0);
                outp.sdram_ba <= ext_addr(15 downto 14);
                ext_cmd_done <= '1';
                nxt.state <= delay_1; 
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
            outp.sdram_a(13 downto 11) <= "000";
            outp.sdram_a(10) <= '1'; -- auto precharge
            outp.sdram_a(9 downto 0) <= cur.col_addr;
            outp.sdram_cmd <= c_cmd_read;
            do_read <= '1';
            nxt.state <= delay_1;
            
        when delay_1 =>
            nxt.state <= delay_2;
        
        when delay_2 =>
            nxt.state <= idle;
                    
        when sd_write =>
            outp.sdram_ba <= cur.bank_addr;
            outp.sdram_a(13 downto 11) <= "000";
            outp.sdram_a(10) <= '1'; -- auto precharge
            outp.sdram_a(9 downto 0) <= cur.col_addr;
            outp.sdram_cmd <= c_cmd_write;
            outp.wdata     <= (others => '0');
            outp.wmask     <= (others => '0');
            nxt.state <= sd_write_2;
        
        when sd_write_2 =>
            outp.wdata     <= cur.wdata;
            outp.wmask     <= cur.wmask;
            outp.write     <= '1';
            nxt.state <= delay_1;

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
            if do_read = '1' then
                dack_pipe(0) <= cur.tag;
            else
                dack_pipe(0) <= (others => '0');
            end if;
            dack_pipe(dack_pipe'high downto 1) <= dack_pipe(dack_pipe'high-1 downto 0);
            
            addr_first  <= outp.sdram_cke & outp.sdram_odt & outp.sdram_cmd & outp.sdram_ba & outp.sdram_a;
            addr_second <= outp.sdram_cke & outp.sdram_odt & "1111"         & outp.sdram_ba & outp.sdram_a;
            phy_wdata   <= outp.wmask(3) & outp.wdata(31 downto 24) &
                           outp.wmask(2) & outp.wdata(23 downto 16) &
                           outp.wmask(1) & outp.wdata(15 downto 08) &
                           outp.wmask(0) & outp.wdata(07 downto 00);
            phy_write   <= outp.write;
            
            if reset='1' then
                cur <= c_internal_state_init;
            end if;
        end if;
    end process;

    rdata       <= phy_rdata(34 downto 27) & phy_rdata(25 downto 18) & phy_rdata(16 downto 9) & phy_rdata(7 downto 0);

    i_phy: entity work.mem_io
    generic map (
        g_data_width       => 9,
        g_addr_cmd_width   => 22
    )
    port map(
        ref_clock          => ref_clock,
        ref_reset          => ref_reset,
        sys_clock          => sys_clock_o,
        sys_reset          => sys_reset_o,
        phasecounterselect => phasecounterselect,
        phasestep          => phasestep(0),
        phaseupdown        => phaseupdown,
        phasedone          => phasedone,
        mode               => mode,
        measurement        => measurement,
        addr_first         => addr_first,
        addr_second        => addr_second,
        wdata              => phy_wdata,
        wdata_oe           => phy_write,
        rdata              => phy_rdata,

        mem_clk_p          => SDRAM_CLK,
        mem_clk_n          => SDRAM_CLKn,
        mem_addr(21)       => SDRAM_CKE,
        mem_addr(20)       => SDRAM_ODT,
        mem_addr(19)       => SDRAM_CSn,
        mem_addr(18)       => SDRAM_RASn,
        mem_addr(17)       => SDRAM_CASn,
        mem_addr(16)       => SDRAM_WEn,
        mem_addr(15 downto 14) => SDRAM_BA,
        mem_addr(13 downto 0)  => SDRAM_A,
        mem_dqs            => SDRAM_DQS,
        mem_dq(8)          => SDRAM_DM,
        mem_dq(7 downto 0) => SDRAM_DQ
    );
    
    -- peripheral registers
    process(clock)
        variable local  : unsigned(3 downto 0);
    begin
        if rising_edge(clock) then
            local := io_req.address(3 downto 0);
            io_resp <= c_io_resp_init;
            if ext_cmd_done = '1' then
                ext_cmd_valid <= '0';
            end if;
            phasestep <= '0' & phasestep(3 downto 1);
             
            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local is
                when X"4" =>
                    io_resp.data <= measurement(11 downto 8) & measurement(5 downto 2);
                when X"5" =>
                    io_resp.data(0) <= phasedone;
                when others =>
                    null;
                end case;
            end if;
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case local is
                when X"0" =>
                    ext_addr(7 downto 0) <= io_req.data;
                when X"1" =>
                    ext_addr(15 downto 8) <= io_req.data;
                when X"2" =>
                    ext_cmd <= io_req.data(3 downto 0);
                    ext_cmd_valid <= '1';
                when X"8" =>
                    mode <= io_req.data(1 downto 0);
                when X"9" =>
                    phasecounterselect <= io_req.data(2 downto 0);
                    phaseupdown        <= io_req.data(3);
                    phasestep          <= io_req.data(7 downto 4);
                when X"C" =>
                    enable_sdram       <= io_req.data(0);
                when others =>
                    null;
                end case;
            end if;           
            if reset = '1' then
                mode <= "00";
                enable_sdram <= '0';
                phasecounterselect <= "000";
                phaseupdown <= '0';
                phasestep <= (others => '0');
                ext_cmd_valid <= '0';
            end if;                     
        end if;
    end process;

end Gideon;

-- 125 MHz
-- ACT to READ: tRCD = 15 ns ( = 2 CLKs (16))
-- ACT to PRCH: tRAS = 40 ns ( = 5 CLKs (40))
-- ACT to ACT:  tRC  = 55 ns ( = 7 CLKs (56))
-- ACT to ACTb: tRRD = 7.5ns ( = 1 CLKs (8))
-- PRCH time;   tRP  = 15 ns ( = 2 CLKs (16))
-- wr. recov.   tWR  = 15 ns ( = 2 CLKs (16)) (starting from last data word)

-- CL=3
--      0 1 2 3 4 5 6 7 8 9 
-- BL4  A - R - p - - -  
--      - - - - - DDDD-

-- BL4W A - W - - - - - - - 
--      - - - - DDDD- - p - 

-- Conclusion: In order to meet tRC, without checking for the bank, we always need 7 clks => 8 clks.
-- It also turns out that write with precharge needs a cycle time of 5 system ticks (10x8 ns) => 80 ns
-- Reads can be done faster; with 8 clocks for each access (4 system clocks) => 64 ns.
-- Reads in a 8-ticks stramine will result in a 25% bus utilization.  (62.5 MB/s) 
-- Note that both reads as well as writes can be issued 2 system clocks after one another IF
-- the bank bits are different! This will yield a bus utilization of 50% (125 MB/s)

-- CL=3
--      0 1 2 3 4 5 6 7 8 9 
-- BL4  A - R - p - - -  
--      - - - - - DDDD-
--      - - - - A - R - p - - -
--      - - - - - - - - - DDDD-
-- 
-- BL4W A - W - - - - - - - 
--      - - - - DDDD- - p - 
--      - - - - A - W - - - - - - -
--      - - - - - - - - DDDD- - p -

