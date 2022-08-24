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
    g_register_rdata   : boolean := true;
    SDRAM_Refr_delay   : integer := 13;
    SDRAM_Refr_period  : integer := 781 );
port (
    -- Startup
    start_clock : in  std_logic;
    start_reset : in  std_logic;
    start_ready : out std_logic;

    toggle_reset    : out std_logic;
    toggle_check    : in  std_logic;

    -- 200 MHz input
    mem_clock   : in  std_logic := '0';
    -- 100 MHz output
    mem_clock_half  : out std_logic; -- SCLK

    sys_clock   : in  std_logic;
    ctrl_clock  : in  std_logic;
    ctrl_reset  : in  std_logic := '0';
    
    -- Clock for the I/O and memory bus
    io_clock    : in  std_logic := '0';
    io_reset    : in  std_logic := '0';
    io_req      : in  t_io_req := c_io_req_init;
    io_resp     : out t_io_resp;

    inhibit     : in  std_logic := '0';
    is_idle     : out std_logic;

    req         : in  t_mem_req_32 := c_mem_req_32_init;
    resp        : out t_mem_resp_32;

    -- PLL alignment
    phase_sel       : out std_logic_vector(1 downto 0) := "00";
    phase_dir       : out std_logic := '0';
    phase_step      : out std_logic := '0';
    phase_loadreg   : out std_logic := '0';


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
    SDRAM_TEST1 : out std_logic := '1'; -- pattern
    SDRAM_TEST2 : out std_logic := '1'; -- data_valid

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
    component mem_sync
        port (
            start_clk   : in  std_logic;
            rst         : in  std_logic; 
            dll_lock    : in  std_logic;
            pll_lock    : in  std_logic; 
            update      : in  std_logic;
            pause       : out std_logic; 
            stop        : out std_logic;
            freeze      : out std_logic; 
            uddcntln    : out std_logic;
            dll_rst     : out std_logic; 
            ddr_rst     : out std_logic;
            ready       : out std_logic );
    end component;

    -- Between Controller and PHY
    signal addr_first         : std_logic_vector(23 downto 0);
    signal addr_second        : std_logic_vector(23 downto 0);
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

    signal update_r           : std_logic := '0';
    signal update_sync        : std_logic;
    signal dll_reset          : std_logic;
    signal buf_reset          : std_logic;
    signal ddr_reset          : std_logic;
    signal ddr_reset_sync     : std_logic;
    signal ddr_reset_ext      : std_logic := '0';
    signal ddr_reset_r        : std_logic := '0';
    signal ddr_reset_d        : std_logic := '0';
    signal ready              : std_logic;

    signal stop               : std_logic := '0';
    signal uddcntln           : std_logic := '1';
    signal freeze             : std_logic := '0';
    signal freeze_usr         : std_logic := '0';
    signal freeze_sync        : std_logic := '0';
    signal pause              : std_logic := '0';
    signal pause_usr          : std_logic := '0';
    signal pause_sync         : std_logic := '0';
    signal read_delay         : std_logic_vector(1 downto 0) := "00";
    signal readclksel         : std_logic_vector(2 downto 0) := "000";
    signal burstdet           : std_logic;
    signal dll_lock           : std_logic;
    signal ctrl_req           : t_io_req;
    signal ctrl_resp          : t_io_resp;
    signal valid_cnt          : unsigned(7 downto 0) := X"00";

    signal sync_reset_r     : std_logic;
    signal sync_reset_sync  : std_logic;
    signal sync_reset       : std_logic;
    
    signal sclk_out           : std_logic;
    signal phase_meas1        : std_logic_vector(7 downto 0) := (others => '0');     
    signal phase_meas2        : std_logic_vector(7 downto 0) := (others => '0');     
begin
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
                when X"3" =>
                    ctrl_resp.data(0) <= toggle_check;
                when X"5" =>
                    ctrl_resp.data <= phase_meas2;
                when X"6" =>
                    ctrl_resp.data <= phase_meas1;
                when X"7" =>
                    ctrl_resp.data <= std_logic_vector(valid_cnt);
                when X"8" =>
                    ctrl_resp.data(2 downto 0) <= readclksel;
                    ctrl_resp.data(4 downto 3) <= read_delay;
                when X"B" =>
                    ctrl_resp.data(1) <= pause_usr;
                    ctrl_resp.data(7) <= ready;
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
            update_r <= '0';
            ddr_reset_r <= '0';
            ddr_reset_d <= ddr_reset_r;
            toggle_reset <= '0';
            sync_reset_r <= '0';
            
            phase_step    <= '0';
            phase_loadreg <= '0'; 

            if rdata_valid = '1' then
                valid_cnt <= valid_cnt + 1;
            end if;

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
                when X"3" =>
                    toggle_reset <= '1';

                when X"4" =>
                    phase_sel <= ctrl_req.data(1 downto 0);
                    phase_dir <= ctrl_req.data(2);

                when X"5" =>
                    phase_step    <= ctrl_req.data(0);
                    phase_loadreg <= ctrl_req.data(1);
                    
                when X"7" =>
                    valid_cnt <= X"00";
                when X"8" =>
                    readclksel <= ctrl_req.data(2 downto 0);
                    read_delay <= ctrl_req.data(4 downto 3);
                when X"9" =>
                    delay_step <= ctrl_req.data(1 downto 0);
                    delay_loadn <= not ctrl_req.data(3 downto 2);
                when X"A" =>
                    delay_dir  <= ctrl_req.data(0);
                when X"B" =>
                    update_r  <= ctrl_req.data(0);
                    pause_usr <= ctrl_req.data(1);
                    freeze_usr <= ctrl_req.data(2);
                    sync_reset_r <= ctrl_req.data(6);
                    ddr_reset_r <= ctrl_req.data(7);
                when X"C" =>
                    clock_enable <= ctrl_req.data(0);
                    odt_enable <= ctrl_req.data(1);
                    refresh_enable <= ctrl_req.data(2);
                when others =>
                    null;
                end case;
            end if;           
            if ctrl_reset = '1' then
                pause_usr <= '0';
                freeze_usr <= '0';
                readclksel <= "000";
                read_delay <= "00";
                clock_enable <= '1';
                odt_enable <= '0';
                ext_cmd_valid <= '0';
                delay_dir <= '1';
                delay_loadn <= "00";
            end if;                     
        end if;
    end process;
    buf_reset <= ddr_reset_sync or ddr_reset_ext;
    ddr_reset_ext <= ddr_reset_r or ddr_reset_d when rising_edge(ctrl_clock); -- 50 MHz pulse

    i_ctrl: entity work.ddr2_ctrl_logic
    generic map (
        g_register_rdata  => g_register_rdata,
        SDRAM_Refr_delay  => SDRAM_Refr_delay,
        SDRAM_Refr_period => SDRAM_Refr_period )
    port map(
        clock             => ctrl_clock,
        reset             => ctrl_reset,
        enable_sdram      => clock_enable,
        refresh_en        => refresh_enable,
        odt_enable        => odt_enable,
        inhibit           => inhibit,
        is_idle           => is_idle,
        read_delay        => read_delay,
        
        req               => req,
        resp              => resp,

        ext_addr          => ext_addr, 
        ext_cmd           => ext_cmd, 
        ext_cmd_valid     => ext_cmd_valid, 
        ext_cmd_done      => ext_cmd_done, 

        addr_first        => addr_first,
        addr_second       => addr_second,
        wdata             => wdata,
        wdata_t           => wdata_t,
        wdata_m           => wdata_m,
        dqs_o             => dqs_o,
        dqs_t             => dqs_t,
        read              => read,
        rdata             => rdata,
        rdata_valid       => rdata_valid
    );

    i_phy: entity work.mem_io_lattice
    generic map (
        g_data_width     => 8,
        g_mask_width     => 1,
        g_addr_width     => 24
    )
    port map (
        sys_clock_4x     => mem_clock,
        start_reset      => start_reset,
        
        dll_reset        => dll_reset,
        dll_lock         => dll_lock,
        ddr_reset        => ddr_reset,
        buf_reset        => buf_reset,
        sclk_out         => sclk_out,
        
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
        addr_first       => addr_first,
        addr_second      => addr_second,
        wdata            => wdata,
        wdata_t          => wdata_t,
        wdata_m          => wdata_m,
        dqs_o            => dqs_o,
        dqs_t            => dqs_t,
        rdata            => rdata,
        datavalid        => rdata_valid,

        mem_clk_p              => SDRAM_CLK,
        mem_clk_n              => SDRAM_CLKn,
        mem_addr(23)           => SDRAM_TEST1,
        mem_addr(22)           => SDRAM_CKE,
        mem_addr(21)           => SDRAM_ODT,
        mem_addr(20)           => SDRAM_CSn,
        mem_addr(19)           => SDRAM_RASn,
        mem_addr(18)           => SDRAM_CASn,
        mem_addr(17)           => SDRAM_WEn,
        mem_addr(16 downto 14) => SDRAM_BA,
        mem_addr(13 downto 0)  => SDRAM_A,
        mem_dqs(0)             => SDRAM_DQS,
        mem_dm(0)              => SDRAM_DM,
        mem_dq(7 downto 0)     => SDRAM_DQ
    );

    SDRAM_TEST2 <= rdata_valid;
    mem_clock_half <= sclk_out;
    
    i_update_sync: entity work.pulse_synchronizer
    port map (
        clock_in  => ctrl_clock,
        pulse_in  => update_r,
        clock_out => start_clock,
        pulse_out => update_sync
    );

    i_reset_sync: entity work.pulse_synchronizer
    port map (
        clock_in  => ctrl_clock,
        pulse_in  => sync_reset_r,
        clock_out => start_clock,
        pulse_out => sync_reset_sync
    );

    sync_reset <= sync_reset_sync or start_reset;

    i_mem_sync: mem_sync
    port map (
        start_clk => start_clock,
        rst       => sync_reset,
        dll_lock  => dll_lock,
        pll_lock  => '1', -- already covered by start_reset
        update    => update_sync, -- new register bit
        pause     => pause_sync, -- to be orred with our own pause
        stop      => stop,
        freeze    => freeze_sync,
        uddcntln  => uddcntln,
        dll_rst   => dll_reset, 
        ddr_rst   => ddr_reset_sync,
        ready     => ready
    );

    pause <= pause_sync or pause_usr;
    freeze <= freeze_sync or freeze_usr;
    ddr_reset <= ddr_reset_sync when rising_edge(start_clock);
    start_ready <= ready;
    
    b_phase_measurement: block
        signal testclk1_c   : std_logic;
        signal testclk1_c2  : std_logic;        
        signal testclk2_c   : std_logic;
        signal testclk2_c2  : std_logic;        
        signal period       : unsigned(7 downto 0) := (others => '0');
        signal counter1     : unsigned(7 downto 0) := (others => '0');
        signal counter2     : unsigned(7 downto 0) := (others => '0');
    begin
        process(ctrl_clock)
        begin
            if rising_edge(ctrl_clock) then
                testclk1_c  <= sclk_out;
                testclk1_c2 <= testclk1_c;
                testclk2_c  <= sys_clock;
                testclk2_c2 <= testclk2_c;
                period <= period + 1;

                if period = 0 then
                    phase_meas1 <= std_logic_vector(counter1);
                    phase_meas2 <= std_logic_vector(counter2);
                    counter1 <= (others => '0');
                    counter2 <= (others => '0');
                else
                    if testclk1_c2 = '1' then
                        counter1 <= counter1 + 1;
                    end if;
                    if testclk2_c2 = period(0) then
                        counter2 <= counter2 + 1;
                    end if;
                end if;                
            end if;
        end process;
    end block;

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

