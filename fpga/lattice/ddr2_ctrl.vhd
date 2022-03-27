-------------------------------------------------------------------------------
-- Title      : External Memory controller for DDR2 SDRAM using Lattice Primitives
-------------------------------------------------------------------------------
-- Description: This module implements a simple quarter rate memory controller.
--              User interface is 32 bit (single beat), externally 4x 8 bit.
--              Because the CPU can only run at 50 MHz in the lattice speed
--              grade -6, the external memory should run at a multiple of that.
--              External memory should be >125 MHz, if the DDR2 DLL is used.
--              A logical multiple of the base clock is 200 MHz, with 400 MHz
--              transfers. See below for the timings.
-------------------------------------------------------------------------------
-- 200 MHz
-- ACT to READ: tRCD = 15 ns ( =  3 CLKs (15))
-- ACT to PRCH: tRAS = 45 ns ( =  9 CLKs (45))
-- ACT to ACT:  tRC  = 55 ns ( = 11 CLKs (55))
-- ACT to ACTb: tRRD = 7.5ns ( =  2 CLKs (10))
-- PRCH time;   tRP  = 15 ns ( =  3 CLKs (15))
-- wr. recov.   tWR  = 15 ns ( =  3 CLKs (15)) (WR = 3!! starting from last data word)
-- REFR to ACT  tRFC = 127.5 ns (= 26 CLKs (130)) = 7 system clocks = 140 ns
-------------------------------------------------------------------------------
-- CL=3, AL=0; quarter rate controller, max 50% utilization on reads (200 MB/s!)
-------------------------------------------------------------------------------
--      | 0 1 2 3 | 4 5 6 7 | 8 9 0 1 | 2 3 4 5 | 6 7 8 9 
-- BL4  | A - - R | - - - - | - p - - | A - - R | - - - - (12 clocks = 60 ns cycle time on same bank for read)
--      | - - - - | A - - R | - - - - | - p - - | A - - R - - - -
--      | - - - - | - - - A | - - R - | - - - - | p - - A - - R - - - -
--      | - - - - | - - DDDD| - - DDDD| - -DDDD-|  - DDDD
-------------------------------------------------------------------------------
-- BL4W | A - - W | - - - - | - - -   | x x x x | A  (16 clocks = 80 ns cycle time on same bank for write)
--      | - - - - | - DDDD- | - - p - | x - - - | -   <- activate on clock 12 is not possible due to tWR after precharge
-- DQS  | ZZZZZZZZ| 0010100Z|ZZZZZZZZZ|
-- DQT  | 00000000| 01111110|000000000|
-- 
-------------------------------------------------------------------------------
-- Because read bursts and write bursts do not overlap, no read to write delay is needed(!)
-- Rule: Reads block bank for new command for 2 low speed clocks, Writes block bank for new command for 3 low speed clocks
-------------------------------------------------------------------------------
-- So in summary, the simplest controller could dispatch a read command every
-- 3 cycles (60 ns), and a write command every 4 cycles (80 ns). When state is
-- implemented per bank, a command can be issued every cycle, unless the bank is busy.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;

entity ddr2_ctrl is
generic (
    SDRAM_Refr_delay   : integer := 7;
    SDRAM_Refr_period  : integer := 488 );
port (
    -- 200 MHz input
    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';
    
    -- Clock for the I/O and memory bus
    io_clock    : in  std_logic;
    io_reset    : in  std_logic;
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
    SDRAM_BA    : out   std_logic_vector(2 downto 0);

    -- data group
    SDRAM_DM    : inout std_logic := 'Z';
    SDRAM_DQ    : inout std_logic_vector(7 downto 0) := (others => 'Z');

    -- dqs
    SDRAM_DQS   : inout std_logic := 'Z');
end entity;    

-- 16 + 6 = 23 address/command timing
-- 8 + 1 = 9 data timing


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

    type t_output is record
        sdram_cmd       : std_logic_vector(3 downto 0);
        sdram_odt       : std_logic;
        sdram_cke       : std_logic;
        sdram_a         : std_logic_vector(13 downto 0);
        sdram_ba        : std_logic_vector(2 downto 0);

        read            : std_logic;
        write           : std_logic;
        wmask           : std_logic_vector(3 downto 0);
        wdata           : std_logic_vector(31 downto 0);
        dqs_o           : std_logic_vector(3 downto 0);
        dqs_t           : std_logic_vector(3 downto 0);
    end record;
    
    constant c_output_init  : t_output := (
        sdram_cmd    => c_cmd_inactive,
        sdram_cke    => '0',
        sdram_odt    => '0',
        sdram_ba     => (others => 'X'),
        sdram_a      => (others => 'X'),
    );
    
    constant c_max_delay : integer := 4;
    
    type t_bank_timers is array(natural range <>) of natural range 0 to c_max_delay-1;
    
    type t_internal_state is record
        refresh_cnt     : integer range 0 to SDRAM_Refr_period-1;
        refr_delay      : integer range 0 to SDRAM_Refr_delay-1;
        timer           : integer range 0 to c_max_delay-1;
        bank_timers     : t_bank_timers(0 to 7);   
        do_refresh      : std_logic;
        refresh_inhibit : std_logic;
        tag             : std_logic_vector(req.tag'range);
        rack            : std_logic;
        rack_tag        : std_logic_vector(req.tag'range);
    end record;
    
    constant c_internal_state_init : t_internal_state := (
        refresh_cnt     => 0,
        refr_delay      => 3,
        bank_timers     => (others => c_max_delay-1),
        timer           => c_max_delay-1,
        do_refresh      => '0',
        refresh_inhibit => '0',
        tag             => (others => '0'),
        rack            => '0',
        rack_tag        => (others => '0')

        read         => '0',
        write        => '0',
        wmask        => "1111",
        wdata        => (others => 'X'),
        dqs_o        => "0000",
        dqs_t        => "0000"
    );    

    type t_tag_array is array(natural range <>) of std_logic_vector(req.tag'range);
    signal dack_pipe    : t_tag_array(4 downto 0) := (others => (others => '0'));
    
    signal outp1        : t_output; -- all pins in the first half cycle
    signal outp2        : t_output; -- all pins in the second half cycle
    signal cur          : t_internal_state := c_internal_state_init;
    signal nxt          : t_internal_state;

    signal delay_step       : std_logic_vector(7 downto 0);
    signal delay_load       : std_logic;
    signal delay_dir        : std_logic;
    signal mode             : std_logic_vector(1 downto 0);
    signal addr1            : std_logic_vector(22 downto 0);
    signal addr2            : std_logic_vector(22 downto 0);
    signal phy_wdata        : std_logic_vector(35 downto 0);
    signal phy_rdata        : std_logic_vector(35 downto 0);
    signal phy_write        : std_logic;
    signal enable_sdram     : std_logic;
    signal rdata            : std_logic_vector(31 downto 0);
    signal ext_addr         : std_logic_vector(15 downto 0);
    signal ext_cmd          : std_logic_vector(3 downto 0);
    signal ext_cmd_valid    : std_logic;
    signal ext_cmd_done     : std_logic;
begin
    is_idle <= '1';
    
    resp.data     <= rdata;
    resp.rack     <= nxt.rack;  -- was cur
    resp.rack_tag <= nxt.rack_tag; -- was cur
    resp.dack_tag <= dack_pipe(dack_pipe'high);
    
    process(req, inhibit, cur, ext_cmd, ext_cmd_valid, ext_addr, enable_sdram)
        procedure send_refresh_cmd is
        begin
            outp.sdram_cmd  <= c_cmd_refresh;
            nxt.do_refresh <= '0';
            nxt.refr_delay <= SDRAM_Refr_delay; 
        end procedure;

        -- address pattern:
        -- A0..1 => index in burst (column bits)
        -- A2..4 => bank address
        -- A5..12 => remaining column bits (A2..A9)
        -- A13..26 => row bits (A0..A13)
        procedure accept_req is
            variable bank : natural range 0 to 7;
            variable col  : std_logic_vector(9 downto 0);
        begin
            nxt.tag        <= req.tag;

            -- Output data with delay of 3
            outp1.wdata(31 downto 24) <= req.data(7 downto 0);
            outp2.wdata( 7 downto  0) <= req.data(15 downto 8);
            outp2.wdata(15 downto  8) <= req.data(23 downto 16);
            outp2.wdata(23 downto 16) <= req.data(31 downto 24);
            outp1.wmask(3) <= not req.byte_en(0);
            outp2.wmask(0) <= not req.byte_en(1);
            outp2.wmask(1) <= not req.byte_en(2);
            outp2.wmask(2) <= not req.byte_en(3);
            
            -- extract address bits
            bank := to_integer(req_address(4 downto 2));
            col(1 downto 0) := std_logic_vector(req.address( 1 downto  0)); -- 2 column bits
            col(9 downto 2) := std_logic_vector(req.address(12 downto  5)); -- 8 column bits

            outp1.sdram_ba  <= std_logic_vector(req.address(4 downto 2)); --  3 bank bits
            outp1.sdram_a   <= std_logic_vector(req.address(26 downto 13)); -- 14 row bits

            outp2.sdram_ba  <= std_logic_vector(req.address(4 downto 2));
            outp2.sdram_a   <= "0001" & col; -- '1' is auto precharge on A10

            if cur.bank_timers(bank) = 0 then
                outp1.sdram_cmd <= c_cmd_active;
                if req.read_writen = '0' then
                    outp2.sdram_cmd <= c_cmd_write;
                    nxt.bank_timers(bank) <= 3;
                    nxt.refr_delay <= 3;
                    outp1.write <= '1';
                else
                    outp2.sdram_cmd <= c_cmd_read;
                    nxt.bank_timers(bank) <= 2;
                    nxt.refr_delay <= 2;
                    outp1.read <= '1';
                end if;                
                nxt.rack       <= '1';
                nxt.rack_tag   <= req.tag;
            end if;
        end procedure;
    begin
        nxt <= cur; -- default no change

        nxt.rack         <= '0';
        nxt.rack_tag     <= (others => '0');

        outp1            <= c_output_init;
        outp2            <= c_output_init;
        outp1.sdram_cke  <= enable_sdram;
        outp2.sdram_cke  <= enable_sdram;

        ext_cmd_done <= '0';
                
        if cur.refr_delay /= 0 then
            nxt.refr_delay <= cur.refr_delay - 1;
        end if;

        for i in 0 to 7 loop
            if cur.bank_timers(i) /= 0 then
                nxt.bank_timers(i) <= cur.bank_timers(i) - 1;
            end if;
        end loop;

        if inhibit='1' then
            nxt.refresh_inhibit <= '1';
        end if;

        -- first cycle after inhibit goes 1, should not be a refresh
        -- this enables putting cartridge images in sdram, because we guarantee the first access after inhibit to be a cart cycle
        if cur.do_refresh='1' and cur.refresh_inhibit='0' then
            send_refresh_cmd;
        elsif ext_cmd_valid = '1' and cur.refr_delay = 0 then
            outp1.sdram_cmd <= ext_cmd;
            outp1.sdram_a <= ext_addr(13 downto 0);
            outp1.sdram_ba <= '0' & ext_addr(15 downto 14);
            ext_cmd_done <= '1';
            nxt.refr_delay <= SDRAM_Refr_delay-1; 
        elsif inhibit='0' then -- make sure we are allowed to start a new cycle
            if req.request='1' and cur.refr_delay = 0 then
                accept_req;
            end if;
        end if;
                
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
            if nxt.do_read = '1' then
                dack_pipe(0) <= cur.tag;
            else
                dack_pipe(0) <= (others => '0');
            end if;
            dack_pipe(dack_pipe'high downto 1) <= dack_pipe(dack_pipe'high-1 downto 0);
            
            addr1 <= outp1.sdram_cke & outp1.sdram_odt & outp1.sdram_cmd & outp1.sdram_ba & outp1.sdram_a;
            addr2 <= outp2.sdram_cke & outp2.sdram_odt & outp2.sdram_cmd & outp2.sdram_ba & outp2.sdram_a;
            data1 <= outp1;
            data2 <= outp2;

            if sys_reset='1' then
                cur <= c_internal_state_init;
            end if;
        end if;
    end process;

    rdata       <= phy_rdata(34 downto 27) & phy_rdata(25 downto 18) & phy_rdata(16 downto 9) & phy_rdata(7 downto 0);

    i_phy: entity work.mem_io
    generic map (
        g_delay_clk_edge   => 60, -- ~2.5 ns or so
        g_data_width       => 8,
        g_mask_width       => 1,
        g_addr_cmd_width   => 23
    )
    port map (
        sys_clock          => sys_clock,    --  50 MHz
        sys_clock_2x       => sys_clock_2x, -- 100 MHz
        sys_clock_4x       => sys_clock_4x, -- 200 MHz
        sys_reset          => sys_reset,

        -- delays for DDR data inputs
        delay_step         => delay_step,
        delay_load         => delay_load,
        delay_dir          => delay_dir,

        mode               => mode,
        addr_first         => addr1,
        addr_second        => addr2,
        data_first         => data1.wdata;
        data_second        => data2.wdata;
        mask_first         => data1.wmask;
        mask_second        => data2.wmask;
        dqso_first         => data1.dqs_o;
        dqso_second        => data2.dqs_o;
        dqst_first         => data1.dqs_t;
        dqst_second        => data2.dqs_t;

        rdata              => phy_rdata,

        mem_clk_p          => SDRAM_CLK,
        mem_clk_n          => SDRAM_CLKn,
        mem_addr(22)       => SDRAM_CKE,
        mem_addr(21)       => SDRAM_ODT,
        mem_addr(20)       => SDRAM_CSn,
        mem_addr(19)       => SDRAM_RASn,
        mem_addr(18)       => SDRAM_CASn,
        mem_addr(17)       => SDRAM_WEn,
        mem_addr(16 downto 14) => SDRAM_BA,
        mem_addr(13 downto 0)  => SDRAM_A,
        mem_dqs            => SDRAM_DQS,
        mem_dm(0)          => SDRAM_DM,
        mem_dq(7 downto 0) => SDRAM_DQ
    );
    
    -- peripheral registers
    process(sys_clock)
        variable local  : unsigned(3 downto 0);
    begin
        if rising_edge(sys_clock) then
            local := io_req.address(3 downto 0);
            io_resp <= c_io_resp_init;
            if ext_cmd_done = '1' then
                ext_cmd_valid <= '0';
            end if;
             
            if io_req.read = '1' then
                io_resp.ack <= '1';
            end if;

            delay_step <= X"00";
            delay_load <= '0';
            
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
                    delay_step <= io_req.data;
                when X"A" =>
                    delay_load <= io_req.data(0);
                when X"B" =>
                    delay_dir  <= io_req.data(0);
                when X"C" =>
                    enable_sdram <= io_req.data(0);
                when others =>
                    null;
                end case;
            end if;           
            if sys_reset = '1' then
                mode <= "00";
                enable_sdram <= '0';
                ext_cmd_valid <= '0';
                delay_dir <= '1';
            end if;                     
        end if;
    end process;

end Gideon;


-- 133 MHz (U64)
-- ACT to READ: tRCD = 15 ns ( = 2 CLKs (15))
-- ACT to PRCH: tRAS = 40 ns ( = 6 CLKs (45))
-- ACT to ACT:  tRC  = 55 ns ( = 8 CLKs (60))
-- ACT to ACTb: tRRD = 7.5ns ( = 1 CLKs (7.5))
-- PRCH time;   tRP  = 15 ns ( = 2 CLKs (15))
-- wr. recov.   tWR  = 15 ns ( = 2 CLKs (15)) (starting from last data word)
-- REFR to ACT  tRFC = 127.5 ns (= 17 CLKs (127.5)) = 9 system clocks, not 7!


-- 125 MHz
-- ACT to READ: tRCD = 15 ns ( = 2 CLKs (16))
-- ACT to PRCH: tRAS = 40 ns ( = 5 CLKs (40))
-- ACT to ACT:  tRC  = 55 ns ( = 7 CLKs (56))
-- ACT to ACTb: tRRD = 7.5ns ( = 1 CLKs (8))
-- PRCH time;   tRP  = 15 ns ( = 2 CLKs (16))
-- wr. recov.   tWR  = 15 ns ( = 2 CLKs (16)) (starting from last data word)
-- REFR to ACT  tRFC = 127.5 ns (= 16 CLKs (128)) = 8 system clocks, not 7!
-- 
-- CL=3
--      0 1 2 3 4 5 6 7 8 9 
-- BL4  A - R - - - p -  
--      - - - - - DDDD-

-- BL4W A - W - - - - - - - A - W
--      - - - - DDDD- - p - 

-- Conclusion: In order to meet tRC, without checking for the bank, we always need 7 clks => 8 clks.
-- It also turns out that write with precharge needs a cycle time of 5 system ticks (10x8 ns) => 80 ns
-- Reads can be done faster; with 8 clocks for each access (4 system clocks) => 64 ns.
-- Reads in a 8-ticks stramine will result in a 25% bus utilization.  (62.5 MB/s) 
-- Note that both reads as well as writes can be issued 2 system clocks after one another IF
-- the bank bits are different! This will yield a bus utilization of 50% (125 MB/s)

-- CL=3
--      0 1 2 3 4 5 6 7 8 9 0 1
-- BL4  A - R - - p - -  
--      - - - - - DDDD-
--      - - - - A - R - p - - -
--      - - - - - - - - - DDDD-
-- 
-- BL4W A - W - - - - - - - 
--      - - - - DDDD- - p - 
--      - - - - A - W - - - - - - -
--      - - - - - - - - DDDD- - p -

-- In addition, when AL=1, the read/write commands can be shifted to the left. Four consecutive
-- reads can then be issued to differnt banks, yielding a bus utilization of 100% (250 MB/s)
-- 
-- CL=3, AL=1
--      0 1 2 3 4 5 6 7 8 9 
-- BL4  A R A R A R A R - - - - - - - -
-- B0   -   x   x   x   -   -   -   -
-- B1   -   -   x   x   x   -   -   -
-- B2   -   -   -   x   x   x   -   -
-- B3   -   -   -   -   x   x   x   -
--      - - - - p0- p1- p2- p3- - - - - (internal)  
--      - - - - - DDDDDDDDDDDDDDDD- - - 
-- BL4w A W A W A W A W - - - - - - - -
--      - - - - - - - - p - p - p - p - (internal)
--      - - - - DDDDDDDDDDDDDDDD- - - -
--  
-- One requirement: block bank ACT/READ or ACT/WRITE command for 3 low speed clocks. This encorces tRC = 64 ns (16 ns LS clock)
-- Other requirement: read to write delay to avoid bus contention (1 low speed clock

-- CL=3, AL=1; write after read, different banks
--      0 1 2 3 4 5 6 7 8 9 0 1  
-- BL4  A R - - A W A R - - - - - - -
--      - - - - p0- - - - - p2- - - (internal)  
--      - - - - - rrrr- - - - rrrr- 
--      - - - - - - - - - - p1- - - (internal)
--      - - - - - - - - wwww- - - -
--      - - - - - - - - - - p - - - p - (internal)
--  

