-------------------------------------------------------------------------------
-- Title      : External Memory controller for DDR2 SDRAM using Lattice Primitives
-------------------------------------------------------------------------------
-- Description: This module implements a simple quarter rate memory controller.
--              User interface is 32 bit (single beat), externally 4x 8 bit.
--              Because the CPU can only run at 50 MHz in the lattice speed
--              grade -6, the external memory should run at a multiple of that.
--              External memory should be >125 MHz, if the DDR2 DLL is used.
--              A logical multiple of the base clock is 200 MHz, with 400 MHz
--              transfers. See 'ddr2_ctrl_logic' for the timings.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;

entity ddr2_ctrl is
generic (
    SDRAM_Refr_delay   : integer := 13;
    SDRAM_Refr_period  : integer := 781 );
port (
    -- 200 MHz input
    mem_clock   : in  std_logic := '0';
    reset       : in  std_logic := '0';
    
    -- Clock for the I/O and memory bus
    io_clock    : in  std_logic := '0';
    io_reset    : in  std_logic := '0';
    io_req      : in  t_io_req := c_io_req_init;
    io_resp     : out t_io_resp;

    inhibit     : in  std_logic := '0';
    is_idle     : out std_logic;

    req         : in  t_mem_req_32 := c_mem_req_32_init;
    resp        : out t_mem_resp_32;

    -- 100 MHz output
    mem_clock_half  : out std_logic;

    -- DDR2 signals
	SDRAM_CLK	: out std_logic;
	SDRAM_CLKn  : out std_logic;

    -- command group
	SDRAM_CKE	: out std_logic := '0'; -- bit 5
    SDRAM_ODT   : out std_logic := '0';
    SDRAM_CSn   : out std_logic := '1';
	SDRAM_RASn  : out std_logic := '1';
	SDRAM_CASn  : out std_logic := '1';
	SDRAM_WEn   : out std_logic := '1'; -- bit 0

    -- address group
    SDRAM_A     : out std_logic_vector(13 downto 0);
    SDRAM_BA    : out std_logic_vector(2 downto 0);

    -- data group
    SDRAM_DM    : inout std_logic := 'Z';
    SDRAM_DQ    : inout std_logic_vector(7 downto 0) := (others => 'Z');

    -- dqs
    SDRAM_DQS   : inout std_logic := 'Z');
end entity;    


architecture Gideon of ddr2_ctrl is
    -- Between Controller and PHY
    signal ctrl_clock         : std_logic; -- 100 MHz
    signal addr_first         : std_logic_vector(21 downto 0);
    signal addr_second        : std_logic_vector(21 downto 0);
    signal csn                : std_logic_vector(1 downto 0);
    signal wdata              : std_logic_vector(31 downto 0);
    signal wdata_t            : std_logic_vector(1 downto 0);
    signal wdata_m            : std_logic_vector(3 downto 0);
    signal dqs_o              : std_logic_vector(3 downto 0);
    signal dqs_t              : std_logic_vector(1 downto 0);
    signal read               : std_logic_vector(1 downto 0);
    signal rdata              : std_logic_vector(31 downto 0);
    signal rdata_valid        : std_logic := '0';

    -- Custom commands
    signal ext_addr         : std_logic_vector(15 downto 0) := (others => '0');
    signal ext_cmd          : std_logic_vector(3 downto 0) := "1111";
    signal ext_cmd_valid    : std_logic := '0';
    signal ext_cmd_done     : std_logic;

    -- PHY controls
    signal clock_enable       : std_logic := '0';
    signal odt_enable         : std_logic := '0';
    signal refresh_enable     : std_logic := '0';
    signal delay_loadn        : std_logic_vector(1 downto 0) := "11";
    signal delay_step         : std_logic_vector(1 downto 0) := "00";
    signal delay_dir          : std_logic := '1';
    signal stop               : std_logic := '0';
    signal uddcntln           : std_logic := '1';
    signal freeze             : std_logic := '0';
    signal pause              : std_logic := '0';
    signal read_delay         : std_logic_vector(1 downto 0) := "00";
    signal readclksel         : std_logic_vector(2 downto 0) := "000";
    signal burstdet           : std_logic;
    signal dll_lock           : std_logic;
    signal ctrl_reset         : std_logic;
    signal ctrl_req           : t_io_req;
    signal ctrl_resp          : t_io_resp;
begin
    ctrl_reset <= io_reset when rising_edge(ctrl_clock);
    
    i_bridge: entity work.io_bus_bridge2
    generic map (
        g_addr_width => 4
    )
    port map(
        clock_a      => io_clock,
        reset_a      => io_reset,
        req_a        => io_req,
        resp_a       => io_resp,
        clock_b      => ctrl_clock,
        reset_b      => ctrl_reset,
        req_b        => ctrl_req,
        resp_b       => ctrl_resp
    );
    
    -- peripheral registers
    process(ctrl_clock)
        variable local  : unsigned(3 downto 0);
    begin
        if rising_edge(ctrl_clock) then
            local := ctrl_req.address(3 downto 0);
            ctrl_resp <= c_io_resp_init;
            if ext_cmd_done = '1' then
                ext_cmd_valid <= '0';
            end if;
             
            if ctrl_req.read = '1' then
                ctrl_resp.ack <= '1';
                case local is
                when X"B" =>
                    ctrl_resp.data(0) <= stop;
                    ctrl_resp.data(1) <= not uddcntln;
                    ctrl_resp.data(2) <= pause;
                    ctrl_resp.data(3) <= freeze;
                    ctrl_resp.data(6 downto 4) <= readclksel;
                when X"8" =>
                    ctrl_resp.data(1 downto 0) <= read_delay;
                when X"C" =>
                    ctrl_resp.data(0) <= clock_enable;
                    ctrl_resp.data(1) <= odt_enable;
                    ctrl_resp.data(2) <= refresh_enable;
                when X"D" =>
                    ctrl_resp.data(0) <= burstdet;
                when X"E" =>
                    ctrl_resp.data(0) <= dll_lock;
                when others =>
                    null;
                end case;
            end if;

            delay_step <= "00";
            delay_loadn <= "11";
            
            if ctrl_req.write = '1' then
                ctrl_resp.ack <= '1';
                case local is
                when X"0" =>
                    ext_addr(7 downto 0) <= ctrl_req.data;
                when X"1" =>
                    ext_addr(15 downto 8) <= ctrl_req.data;
                when X"2" =>
                    ext_cmd <= ctrl_req.data(3 downto 0);
                    ext_cmd_valid <= '1';
                when X"8" =>
                    read_delay <= ctrl_req.data(1 downto 0);
                when X"9" =>
                    delay_step <= ctrl_req.data(1 downto 0);
                    delay_loadn <= not ctrl_req.data(3 downto 2);
                when X"A" =>
                    delay_dir  <= ctrl_req.data(0);
                when X"B" =>
                    stop        <= ctrl_req.data(0);
                    uddcntln    <= not ctrl_req.data(1);
                    freeze      <= ctrl_req.data(2);
                    pause       <= ctrl_req.data(3);
                    readclksel  <= ctrl_req.data(6 downto 4);
                when X"C" =>
                    clock_enable <= ctrl_req.data(0);
                    odt_enable <= ctrl_req.data(1);
                    refresh_enable <= ctrl_req.data(2);
                when others =>
                    null;
                end case;
            end if;           
            if io_reset = '1' then
                read_delay <= "00";
                clock_enable <= '0';
                odt_enable <= '0';
                ext_cmd_valid <= '0';
                delay_dir <= '1';
                read_delay <= "00";
            end if;                     
        end if;
    end process;

    i_ctrl: entity work.ddr2_ctrl_logic
    port map(
        clock             => ctrl_clock,
        reset             => reset,
        enable_sdram      => clock_enable,
        refresh_en        => refresh_enable,
        odt_enable        => odt_enable,
        inhibit           => inhibit,
        is_idle           => is_idle,

        req               => req,
        resp              => resp,

        ext_addr          => ext_addr, 
        ext_cmd           => ext_cmd, 
        ext_cmd_valid     => ext_cmd_valid, 
        ext_cmd_done      => ext_cmd_done, 

        addr_first        => addr_first,
        addr_second       => addr_second,
        csn               => csn,
        wdata             => wdata,
        wdata_t           => wdata_t,
        wdata_m           => wdata_m,
        dqs_o             => dqs_o,
        dqs_t             => dqs_t,
        read              => read,
        rdata             => rdata,
        rdata_valid       => rdata_valid
    );

    i_phy: entity work.mem_io
    generic map (
        g_data_width     => 8,
        g_mask_width     => 1,
        g_addr_width     => 22
    )
    port map (
        sys_reset        => reset,
        sys_clock_2x     => ctrl_clock,
        sys_clock_4x     => mem_clock,
        
        clock_enable     => clock_enable,
        delay_dir        => delay_dir,
        delay_rdstep     => delay_step(0),
        delay_wrstep     => delay_step(1),
        delay_rdloadn    => delay_loadn(0),
        delay_wrloadn    => delay_loadn(1),
        stop             => stop,
        uddcntln         => uddcntln,
        freeze           => freeze,
        pause            => pause,
        readclksel       => readclksel,
        read             => read,
        burstdet         => burstdet,
        dll_lock         => dll_lock,
        sclk_out         => mem_clock_half,
        addr_first       => addr_first,
        addr_second      => addr_second,
        csn              => csn,
        wdata            => wdata,
        wdata_t          => wdata_t,
        wdata_m          => wdata_m,
        dqs_o            => dqs_o,
        dqs_t            => dqs_t,
        rdata            => rdata,
        datavalid        => rdata_valid,

        mem_clk_p              => SDRAM_CLK,
        mem_clk_n              => SDRAM_CLKn,
        mem_csn                => SDRAM_CSn,
        mem_addr(21)           => SDRAM_CKE,
        mem_addr(20)           => SDRAM_ODT,
        mem_addr(19)           => SDRAM_RASn,
        mem_addr(18)           => SDRAM_CASn,
        mem_addr(17)           => SDRAM_WEn,
        mem_addr(16 downto 14) => SDRAM_BA,
        mem_addr(13 downto 0)  => SDRAM_A,
        mem_dqs(0)             => SDRAM_DQS,
        mem_dm(0)              => SDRAM_DM,
        mem_dq(7 downto 0)     => SDRAM_DQ
    );

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

