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
-- REFR to ACT  tRFC = 127.5 ns (= 26 CLKs (130)) = 13 controller clocks
-------------------------------------------------------------------------------
-- CL=3, AL=0; quarter rate controller, max 50% utilization on reads (200 MB/s!)
-------------------------------------------------------------------------------
--      | 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 
-- BL4  | A - - R - - - - - p - - A - - R - - - - (12 clocks = 60 ns cycle time on same bank for read)
--      | - - - - A - - R - - - - - p - - A - - R - - - -
--      | - - - - - - - - A - - R - - - - - p - - A - - R - - - -
--      | - - - - - - DDDD- - DDDD- - DDDD- - DDDD
-----------------------------------------------------------------------
-- BL4W | A - - W - - - - - - -   x x x x A  (16 clocks = 80 ns cycle time on same bank for write)
--      | - - - - - DDDD- - - p - x - - - -   <- activate on clock 12 is not possible due to tWR after precharge
-- DQS  | ZZZZZZZZ0010100ZZZZZZZZZ
-- DQT  | 000000000111111000000000
-- 
-- And with AL=2, it looks like this, so that A and R can be generated in one cycle
-------------------------------------------------------------------------------
--      | 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 
-- BL4  | A R - - - - - - - p - - A R - - - - - - (12 clocks = 60 ns cycle time on same bank for read)
--      | - - - - A R - - - - - - - p - - A R - - - - - -
--      | - - - - - - - - A R - - - - - - - p - - - - - -
--      | - - - - - - DDDD- - DDDD- - DDDD- - DDDD
-----------------------------------------------------------------------
-- BL4W | A W - - - - - - - - -   x x A W -  (14 clocks = 70 ns cycle time on same bank for write)
--      | - - - - - DDDD- - - p - x - - - -   <- activate on clock 12 is not possible due to tWR after precharge
-------------------------------------------------------------------------------
-- Considering that this controller runs at 100 MHz, and could accept a command
-- at every cycle pair (separation of concerns: at this point we don't know that the controller will
-- have a wrapper around it that runs at once again half of the speed...)
-------------------------------------------------------------------------------
--      | 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 
-- BL4  | A R - - - - - - - p - - A R - - - - - - 
--      | - - A R - - - - - - - p - - A R - - - - - -
--      | - - - - A R - - - - - - - p - - - - - -
--      | - - - - - - DDDDDDDDDDDD- - - - - - DDDDDDDD
-------------------------------------------------------------------------------
-- BL4  | A R - - - - - - - p - - A R - - - - - - 
--      | - - A W - - - - - - - p - - A R - - - - - -
--      | - - - - A R - - - - - - - p - - - - - -
--     R| - - - - - - DDDD----DDDD- - - - - - DDDDDDDD
--     W| - - - - - - - DDDD    <- conflict
-------------------------------------------------------------------------------
-- BL4  | A R - - - - - - - p - - A R - - - - - - 
--      | - - x x A W - - - - - - - p - - A R - - - - - -
--      | - - - - - - A R - - - - - - - p - - - - - -
--     R| - - - - - - DDDD--------DDDD- - - - - - DDDDDDDD
--     W| - - - - - - - - - DDDD    <- conflict solved by delaying the write after read by 1 controller cycle
-------------------------------------------------------------------------------
-- Rule: Reads block bank for new command for 5 clocks, Writes block bank for new command for 6 controller clocks
-------------------------------------------------------------------------------
-- So in summary, the simplest controller could dispatch a read command every
-- 6 cycles (60 ns), and a write command every 7 cycles (70 ns). When state is
-- implemented per bank, a command can be issued every cycle, unless the bank is busy,
-- and the current cycle is not an immediate write after read condition.
-- This controller will be used from the 50 MHz domain, so in practice Commands are only
-- issued on the even or odd pairs. But since this logic runs at 100 MHz, it is implemented
-- as if a command CAN be used every cycle pair.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;

entity ddr2_ctrl_logic is
generic (
    SDRAM_Refr_delay   : integer := 13;
    SDRAM_Refr_period  : integer := 781 );
port (
    -- 100 MHz input
    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';
    
    enable_sdram: in  std_logic := '1';
    refresh_en  : in  std_logic := '1';
    odt_enable  : in  std_logic := '0';
    offset      : in  std_logic_vector(1 downto 0) := "00";
    inhibit     : in  std_logic := '0';
    is_idle     : out std_logic;
    req         : in  t_mem_req_32 := c_mem_req_32_init;
    resp        : out t_mem_resp_32;

    -- Commands to set up the DDR2 chip (mode registers and forced precharges)
    ext_addr         : in  std_logic_vector(15 downto 0) := (others => '0');
    ext_cmd          : in  std_logic_vector(3 downto 0) := (others => '1');
    ext_cmd_valid    : in  std_logic := '0';
    ext_cmd_done     : out std_logic;

    -- Connections to PHY
    addr_first  : out std_logic_vector(23 downto 0);
    addr_second : out std_logic_vector(23 downto 0);
    wdata       : out std_logic_vector(31 downto 0);
    wdata_t     : out std_logic_vector(1 downto 0);
    wdata_m     : out std_logic_vector(3 downto 0);
    dqs_o       : out std_logic_vector(3 downto 0);
    dqs_t       : out std_logic_vector(1 downto 0);
    read        : out std_logic_vector(1 downto 0);

    rdata       : in  std_logic_vector(31 downto 0);
    rdata_valid : in  std_logic );

end entity;    


architecture Gideon of ddr2_ctrl_logic is
    constant c_cmd_inactive  : std_logic_vector(3 downto 0) := "1111";
    constant c_cmd_nop       : std_logic_vector(3 downto 0) := "0111";
    constant c_cmd_active    : std_logic_vector(3 downto 0) := "0011";
    constant c_cmd_read      : std_logic_vector(3 downto 0) := "0101";
    constant c_cmd_write     : std_logic_vector(3 downto 0) := "0100";
    constant c_cmd_bterm     : std_logic_vector(3 downto 0) := "0110";
    constant c_cmd_precharge : std_logic_vector(3 downto 0) := "0010";
    constant c_cmd_refresh   : std_logic_vector(3 downto 0) := "0001";
    constant c_cmd_mode_reg  : std_logic_vector(3 downto 0) := "0000";

    type t_address_command is record
        sdram_cmd       : std_logic_vector(3 downto 0);
        sdram_odt       : std_logic;
        sdram_cke       : std_logic;
        sdram_a         : std_logic_vector(13 downto 0);
        sdram_ba        : std_logic_vector(2 downto 0);
    end record;
    
    type t_data_path is record
        read            : std_logic_vector(1 downto 0);
        read_8          : std_logic;
        wmask           : std_logic_vector(3 downto 0);
        wdata           : std_logic_vector(31 downto 0);
        wdata_t         : std_logic_vector(1 downto 0);
        dqs_o           : std_logic_vector(3 downto 0);
        dqs_t1          : std_logic_vector(1 downto 0);
        dqs_t2          : std_logic_vector(1 downto 0);
    end record;
    
    constant c_address_init  : t_address_command := (
        sdram_cmd    => c_cmd_inactive,
        sdram_cke    => '0',
        sdram_odt    => '0',
        sdram_ba     => (others => 'X'),
        sdram_a      => (others => 'X')
    );
    
    constant c_data_path_init : t_data_path := (
        read         => "00",
        read_8       => '0',
        wmask        => "1111",
        wdata        => (others => '0'),
        wdata_t      => "11",
        dqs_o        => "0000",
        dqs_t1       => "11",
        dqs_t2       => "11"
    );
    
    constant c_max_delay : integer := 7;
    
    type t_bank_timers is array(natural range <>) of natural range 0 to c_max_delay-1;
    
    type t_internal_state is record
        refresh_cnt     : integer range 0 to SDRAM_Refr_period-1;
        refr_delay      : integer range 0 to SDRAM_Refr_delay;
        timer           : integer range 0 to SDRAM_Refr_delay;
        bank_timers     : t_bank_timers(0 to 7);
        write_recovery  : integer range 0 to c_max_delay-1;
        read_issued     : std_logic;   
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
        write_recovery  => 0,
        read_issued     => '0',
        do_refresh      => '0',
        refresh_inhibit => '0',
        tag             => (others => '0'),
        rack            => '0',
        rack_tag        => (others => '0')
    );    

    signal pattern      : unsigned(1 downto 0) := "00";
    signal addr1        : t_address_command; -- all pins in the first half cycle
    signal addr2        : t_address_command; -- all pins in the second half cycle
    signal datap        : t_data_path;
    signal dqs_t2       : std_logic_vector(1 downto 0) := "11";
    signal read_8       : std_logic;
    signal cur          : t_internal_state := c_internal_state_init;
    signal nxt          : t_internal_state;

    signal zread        : std_logic_vector(1 downto 0);
    signal yread        : std_logic_vector(1 downto 0);
    
    signal zwdata       : std_logic_vector(31 downto 0);
    signal zwdata_m     : std_logic_vector(3 downto 0);
    signal zwdata_t     : std_logic_vector(1 downto 0);
    signal zdqs_o       : std_logic_vector(3 downto 0);
    signal zdqs_t       : std_logic_vector(1 downto 0);

    signal ywdata       : std_logic_vector(31 downto 0);
    signal ywdata_m     : std_logic_vector(3 downto 0);
    signal ywdata_t     : std_logic_vector(1 downto 0);
    signal ydqs_o       : std_logic_vector(3 downto 0);
    signal ydqs_t       : std_logic_vector(1 downto 0);

    signal xwdata       : std_logic_vector(31 downto 0);
    signal xwdata_m     : std_logic_vector(3 downto 0);
    signal xwdata_t     : std_logic_vector(1 downto 0);
    signal xdqs_o       : std_logic_vector(3 downto 0);
    signal xdqs_t       : std_logic_vector(1 downto 0);

    signal wwdata_t     : std_logic_vector(1 downto 0);

    signal dack_tag     : std_logic_vector(7 downto 0);
begin
    is_idle <= '1';
    
    resp.data     <= rdata;
    resp.rack     <= nxt.rack;  -- was cur
    resp.rack_tag <= nxt.rack_tag; -- was cur
    resp.dack_tag <= dack_tag when rdata_valid = '1' else X"00";
    
    process(req, inhibit, cur, ext_cmd, ext_cmd_valid, ext_addr, enable_sdram, refresh_en, odt_enable, dqs_t2, read_8)
        procedure send_refresh_cmd is
        begin
            addr1.sdram_cmd  <= c_cmd_refresh;
            nxt.do_refresh <= '0';
            nxt.timer <= SDRAM_Refr_delay; 
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

            -- Output data by default
--            datap.wdata <= req.data;
--            datap.wmask <= not req.byte_en;
            
            -- extract address bits
            bank := to_integer(req.address(4 downto 2));
            col(1 downto 0) := offset; -- std_logic_vector(req.address( 1 downto  0)); -- 2 column bits
            col(9 downto 2) := std_logic_vector(req.address(12 downto  5)); -- 8 column bits

            addr1.sdram_ba  <= std_logic_vector(req.address(4 downto 2)); --  3 bank bits
            addr1.sdram_a   <= '0' & std_logic_vector(req.address(25 downto 13)); -- 14 row bits

            addr2.sdram_ba  <= std_logic_vector(req.address(4 downto 2));
            addr2.sdram_a   <= "0001" & col; -- '1' is auto precharge on A10

            if cur.bank_timers(bank) = 0 then
                -- Read_issued delays the read by one cycle, to avoid bus contention
                -- Since write has one cycle less latency.
                if req.read_writen = '0' and cur.read_issued = '0' then
                    addr1.sdram_cmd <= c_cmd_active;
                    addr2.sdram_cmd <= c_cmd_write;

                    -- Now
                    datap.wdata    <= req.data;
                    datap.wmask    <= not req.byte_en;
                    datap.wdata_t  <= "00";
                    datap.dqs_t1   <= "00";
                    datap.dqs_o    <= "1010"; 

                    -- Second cycle program leadout
                    datap.dqs_t2   <= "10";

                    nxt.rack       <= '1';
                    nxt.rack_tag   <= req.tag;
                    nxt.bank_timers(bank) <= 6;
                    nxt.refr_delay <= 6;
                    nxt.write_recovery <= 6;
                elsif req.read_writen = '1' and cur.write_recovery = 0 then -- read
                    -- Write recovery delay is to make sure writes have been completed
                    -- before any read command is issued.
                    addr1.sdram_cmd <= c_cmd_active;
                    addr2.sdram_cmd <= c_cmd_read;
                    datap.read <= "11"; -- might get delayed later
                    -- datap.read_8 <= '1';
                    nxt.rack       <= '1';
                    nxt.rack_tag   <= req.tag;
                    nxt.bank_timers(bank) <= 5;
                    nxt.refr_delay <= 5;
                    nxt.read_issued <= '1';
                end if;                
            end if;
        end procedure;
    begin
        nxt <= cur; -- default no change

        nxt.rack         <= '0';
        nxt.rack_tag     <= (others => '0');
        nxt.read_issued  <= '0';
        
        addr1            <= c_address_init;
        addr2            <= c_address_init;
        addr1.sdram_cke  <= enable_sdram;
        addr2.sdram_cke  <= enable_sdram;
        addr1.sdram_odt  <= odt_enable;
        addr2.sdram_odt  <= odt_enable;
        datap            <= c_data_path_init;
        datap.dqs_t1     <= dqs_t2;        
        datap.read       <= read_8 & read_8;

        ext_cmd_done <= '0';
                
        if cur.refr_delay /= 0 then
            nxt.refr_delay <= cur.refr_delay - 1;
        end if;

        if cur.timer /= 0 then
            nxt.timer <= cur.timer - 1;
        end if;

        if cur.write_recovery /= 0 then
            nxt.write_recovery <= cur.write_recovery - 1;
        end if;

        for i in 0 to 7 loop
            if cur.bank_timers(i) /= 0 then
                nxt.bank_timers(i) <= cur.bank_timers(i) - 1;
            end if;
        end loop;

        nxt.refresh_inhibit <= inhibit;

        -- first cycle after inhibit goes 1, should not be a refresh
        -- this enables putting cartridge images in sdram, because we guarantee the first access after inhibit to be a cart cycle
        if cur.do_refresh='1' and cur.refresh_inhibit='0' then
            if cur.refr_delay = 0 then
                send_refresh_cmd;
            end if;
        elsif ext_cmd_valid = '1' and cur.refr_delay = 0 then
            if ext_cmd = c_cmd_read then
                datap.read <= "11";
                datap.read_8 <= '1';
            end if;
            addr1.sdram_cmd <= ext_cmd;
            addr1.sdram_a <= ext_addr(13 downto 0);
            addr1.sdram_ba <= '0' & ext_addr(15 downto 14);

            ext_cmd_done <= '1';
            nxt.refr_delay <= SDRAM_Refr_delay-1; 
        elsif inhibit='0' then -- make sure we are allowed to start a new cycle
            if req.request='1' and cur.timer = 0 then
                accept_req;
            end if;
        end if;
                
        if cur.refresh_cnt = SDRAM_Refr_period-1 then
            nxt.do_refresh  <= refresh_en;
            nxt.refresh_cnt <= 0;
        else
            nxt.refresh_cnt <= cur.refresh_cnt + 1;
        end if;
    end process;

    i_tag_fifo: entity work.sync_fifo
    generic map(
        g_depth        => 15,
        g_data_width   => 8,
        g_threshold    => 3,
        g_storage      => "MRAM",
        g_storage_lat  => "LUT",
        g_fall_through => true
    )
    port map(
        clock          => clock,
        reset          => reset,
        rd_en          => rdata_valid,
        wr_en          => cur.read_issued,
        din            => cur.tag,
        dout           => dack_tag,
        flush          => '0',
        valid          => open
    );
    
    pattern(0) <= '1' when addr1.sdram_cmd = c_cmd_write else '0';
    pattern(1) <= '1' when addr2.sdram_cmd = c_cmd_write else '0';

    process(clock)
        constant c_pattern_0 : std_logic_vector(0 to 3) := "0010";
        constant c_pattern_1 : std_logic_vector(0 to 3) := "0100";
    begin
        if rising_edge(clock) then
            cur <= nxt;
            
            dqs_t2  <= datap.dqs_t2; -- For the next cycle
            read_8  <= datap.read_8;

            -- In all fairness, this step takes 10 ns, which might not be necessary
            addr_first  <= pattern(0) & addr1.sdram_cke & addr1.sdram_odt & addr1.sdram_cmd & addr1.sdram_ba & addr1.sdram_a;
            addr_second <= pattern(1) & addr2.sdram_cke & addr2.sdram_odt & addr2.sdram_cmd & addr2.sdram_ba & addr2.sdram_a;
            
            zwdata   <= datap.wdata;
            zwdata_t <= datap.wdata_t;
            zwdata_m <= datap.wmask;
            zdqs_o   <= datap.dqs_o;
            zdqs_t   <= datap.dqs_t1; -- Now

            ywdata   <= zwdata;
            ywdata_t <= zwdata_t;
            ywdata_m <= zwdata_m;
            ydqs_o   <= zdqs_o;
            ydqs_t   <= zdqs_t; 
            
            xwdata   <= ywdata;
            xwdata_t <= ywdata_t;
            xwdata_m <= ywdata_m;
            xdqs_o   <= ydqs_o;
            xdqs_t   <= ydqs_t; 

            wwdata_t <= xwdata_t;

            wdata   <= xwdata;
            wdata_t <= xwdata_t and wwdata_t; -- lengthen tristate by a whole burst
            wdata_m <= xwdata_m;
            dqs_o   <= xdqs_o;
            dqs_t   <= xdqs_t; 

            zread    <= datap.read;
            yread    <= zread;
            read     <= yread;

            if reset='1' then
                cur <= c_internal_state_init;
            end if;
        end if;
    end process;

--    i_wdata_delay: entity work.half_cycle_delay generic map(32) port map(clock, zwdata, wdata);
--    i_wdatt_delay: entity work.half_cycle_delay generic map( 2) port map(clock, zwdata_t, wdata_t);
--    i_wdatm_delay: entity work.half_cycle_delay generic map( 4) port map(clock, zwdata_m, wdata_m);
--    i_dqso_delay:  entity work.half_cycle_delay generic map( 4) port map(clock, zdqs_o, dqs_o);
--    i_dqst_delay:  entity work.half_cycle_delay generic map( 2) port map(clock, zdqs_t, dqs_t);

end architecture;
