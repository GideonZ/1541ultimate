--------------------------------------------------------------------------------
-- Entity: mem_io
-- Date:2016-07-16  
-- Author: Gideon     
--
-- Description: All Altera specific I/O stuff for DDR(2)
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library ECP5U;
use ECP5U.components.all;

entity mem_io is
    generic (
        g_data_width        : natural := 4;
        g_mask_width        : natural := 1;
        g_addr_width        : natural := 8 );
	port (
        sys_reset          : in  std_logic;
        sys_clock          : out std_logic;
        sys_clock_2x       : out std_logic;
        sys_clock_4x       : in  std_logic;

        clock_enable       : in  std_logic := '1';
        delay_step         : in  std_logic_vector(g_data_width-1 downto 0);
        delay_load         : in  std_logic;
        delay_dir          : in  std_logic;
        delay_rdloadn      : in  std_logic := '1';
        delay_wrloadn      : in  std_logic := '1';
        
        -- Calibration controls
        stop               : in    std_logic;
        uddcntln           : in    std_logic;
        freeze             : in    std_logic;
        pause              : in    std_logic;
        readclksel         : in    std_logic_vector(2 downto 0);
        read               : in    std_logic_vector(1 downto 0);
        datavalid          : out   std_logic;
        burstdet           : out   std_logic;
        dll_lock           : out   std_logic;
        
        -- inputs at sys_clock_2x (sclk = 100 MHz)
        addr_first         : in    std_logic_vector(g_addr_width-1 downto 0);
        addr_second        : in    std_logic_vector(g_addr_width-1 downto 0);
        csn                : in    std_logic_vector(1 downto 0);
        wdata              : in    std_logic_vector(4*g_data_width-1 downto 0);
        wdata_t            : in    std_logic_vector(1 downto 0);
        wdata_m            : in    std_logic_vector(4*g_mask_width-1 downto 0);
        dqs_o              : in    std_logic_vector(4*g_mask_width-1 downto 0);
        dqs_t              : in    std_logic_vector(1 downto 0);

        -- output at sys_clock_2x (sclk)
        rdata              : out   std_logic_vector(4*g_data_width-1 downto 0);
        
        -- Memory pins
		mem_clk_p          : out   std_logic;
		mem_clk_n          : out   std_logic;
		mem_addr           : out   std_logic_vector(g_addr_width-1 downto 0);
        mem_csn            : out   std_logic;
        mem_dqs            : inout std_logic_vector(g_mask_width-1 downto 0);
        mem_dm             : out   std_logic_vector(g_mask_width-1 downto 0);
		mem_dq             : inout std_logic_vector(g_data_width-1 downto 0)
    );
end entity;

architecture lattice of mem_io is
    signal ddrdel           : std_logic;
    signal sclk, sclk_i     : std_logic;
    signal eclk             : std_logic;
    signal hclk             : std_logic := '0';
    signal rst              : std_logic;

    signal mem_dqs_o        : std_logic_vector(g_mask_width-1 downto 0);
    signal mem_dqs_t        : std_logic;
    signal mem_dqs_i        : std_logic;
        
    signal rdpntr           : std_logic_vector(2 downto 0);
    signal wrpntr           : std_logic_vector(2 downto 0);
    signal dqsr90           : std_logic;
    signal dqsw270          : std_logic;
    signal dqsw0            : std_logic; 

    signal phase, srst      : std_logic;
begin
    process(rst, sclk)
    begin
        if rst='1' then
            srst <= '1';
        elsif rising_edge(sclk) then
            srst <= '0';
        end if;
    end process;
    
    process(sys_clock_4x)
    begin
        if rising_edge(sys_clock_4x) then
            rst <= sys_reset;
            if srst = '1' then
                phase <= '1';
            else
                phase <= not phase;
            end if;
        end if;
    end process;
        
    -- Make clocks, based on 50 MHz reference. ECLK = 200 MHz, SCLK = 100 MHz, ECLK90 = 90 degree shifted ECLK
    i_eclk_sync: ECLKSYNCB port map (
        ECLKI   => sys_clock_4x,
        STOP    => stop,
        ECLKO   => eclk );

    i_dll: DDRDLLA port map (
        CLK         => eclk,
        RST         => rst,      -- from mem_sync
        UDDCNTLN    => uddcntln, -- from mem_sync
        FREEZE      => freeze,   -- from mem_sync
        DDRDEL      => ddrdel,   -- code to delay block
        LOCK        => dll_lock );   -- back to mem_sync

    i_dqsbufm: DQSBUFM
    generic map (
        DQS_LO_DEL_VAL =>  0,
        DQS_LO_DEL_ADJ => "FACTORYONLY", 
        DQS_LI_DEL_VAL =>  0,
        DQS_LI_DEL_ADJ => "FACTORYONLY")
    port map (
        DQSI        => mem_dqs_i,
        READ1       => read(1),
        READ0       => read(0), 
        READCLKSEL2 => readclksel(2),
        READCLKSEL1 => readclksel(1), 
        READCLKSEL0 => readclksel(0),
        DDRDEL      => ddrdel, -- from DDRDLLA
        ECLK        => eclk, 
        SCLK        => sclk,
        RST         => rst,
        PAUSE       => pause, 
        DYNDELAY7   => '0',
        DYNDELAY6   => '0', 
        DYNDELAY5   => '0', 
        DYNDELAY4   => '0', 
        DYNDELAY3   => '0', 
        DYNDELAY2   => '0', 
        DYNDELAY1   => '0', 
        DYNDELAY0   => '0', 
        RDLOADN     => delay_rdloadn,
        RDMOVE      => '0', 
        RDDIRECTION => delay_dir,
        WRLOADN     => delay_wrloadn, 
        WRMOVE      => '0',
        WRDIRECTION => delay_dir, 
        DQSR90      => dqsr90,
        DQSW270     => dqsw270,
        DQSW        => dqsw0, 
        RDPNTR2     => rdpntr(2),
        RDPNTR1     => rdpntr(1),
        RDPNTR0     => rdpntr(0), 
        WRPNTR2     => wrpntr(2),
        WRPNTR1     => wrpntr(1),
        WRPNTR0     => wrpntr(0), 
        DATAVALID   => datavalid,
        BURSTDET    => burstdet, 
        RDCFLAG     => open,
        WRCFLAG     => open );
    
    i_sclk: CLKDIVF
    generic map (DIV=> "2.0")
    port map (
        CLKI    => eclk,
        RST     => rst,
        ALIGNWD => '0', 
        CDIVX   => sclk_i );

    sclk <= sclk_i;
    
--    i_sysclk: CLKDIVF
--    generic map (DIV=> "2.0")
--    port map (
--        CLKI    => sclk_i,
--        RST     => rst,
--        ALIGNWD => '0', 
--        CDIVX   => hclk );

    -- Generate Clock out, which is synchronous to the clock_4x, but delayed slightly compared to the address clocked out
    -- The clock gets a delay of g_delay_clk_edge taps, to be somewhat centered in the address
    b_clock: block
        signal clk_p    : std_logic;
        signal clk_n    : std_logic;
        -- signal clk_p_d  : std_logic;
    begin
        i_clkp: ODDRX2F port map (
            D0    => '0',
            D1    => clock_enable,
            D2    => '0',
            D3    => clock_enable,
            ECLK  => eclk,
            SCLK  => sclk,
            RST   => rst,
            Q     => clk_p);
    
        i_delay_p: DELAYG generic map (DEL_MODE => "DQS_CMD_CLK") port map (A => clk_p, Z => mem_clk_p);
--        i_obuf: OBCO port map (I => clk_p_d, OT => mem_clk_p, OC => mem_clk_n );

        i_clkn: ODDRX2F port map (
            D0    => clock_enable,
            D1    => '0',
            D2    => clock_enable,
            D3    => '0',
            ECLK  => eclk,
            SCLK  => sclk,
            RST   => rst,
            Q     => clk_n);
    
        i_delay_n: DELAYG generic map (DEL_MODE => "DQS_CMD_CLK") port map (A => clk_n, Z => mem_clk_n);
--        i_obuf: OBCO port map (I => clk_p_d, OT => mem_clk_p, OC => mem_clk_n );
    end block;

    -- The address is clocked out at 100 MHz, with DDR flipflops
--    r_addr: for i in 0 to g_addr_width-1 generate
--        i_addr: ODDRX1F port map (
--            D0    => addr_first(i),
--            D1    => addr_second(i),
--            SCLK  => sclk,
--            RST   => rst,
--            Q     => mem_addr(i));
--    end generate;
--
--    i_csn: ODDRX1F port map (
--        D0    => csn(0),
--        D1    => csn(1),
--        SCLK  => sclk,
--        RST   => rst,
--        Q     => mem_csn);

    b_addr: block
        signal addr_out : std_logic_vector(addr_first'range);
        signal csn_out  : std_logic;
    begin
        addr_out <= addr_first when phase='1' else addr_second;
        csn_out  <= csn(0) when phase='1' else csn(1);
    
        r_addr: for i in 0 to g_addr_width-1 generate
            i_addr: OFS1P3BX port map (
                D     => addr_out(i),
                SCLK  => sys_clock_4x,
                SP    => '1',
                PD    => rst,
                Q     => mem_addr(i));
        end generate;
    
        i_csn: OFS1P3BX port map (
            D     => csn_out,
            SCLK  => sys_clock_4x,
            SP    => '1',
            PD    => rst,
            Q     => mem_csn);
    end block;
    
--    process(sys_clock_4x)
--    begin
--        if rising_edge(sys_clock_4x) then
--            if phase='1' then
--                mem_addr <= addr_first;
--                mem_csn  <= csn(0);
--            else
--                mem_addr <= addr_second;
--                mem_csn  <= csn(1);
--            end if;
--        end if;
--    end process;
    
    -- The data is clocked out at 200 MHz with DDR flipflops
    b_data: block
        signal mem_dq_o         : std_logic_vector(g_data_width-1 downto 0);
        signal mem_dq_i         : std_logic_vector(g_data_width-1 downto 0);
        signal mem_dq_i_delayed : std_logic_vector(g_data_width-1 downto 0);
        signal mem_dq_t         : std_logic;
    begin
        i_data_tri: TSHX2DQA
            port map (
                T0      => wdata_t(0),
                T1      => wdata_t(1),
                SCLK    => sclk, 
                ECLK    => eclk,
                DQSW270 => dqsw270,
                RST     => rst,
                Q       => mem_dq_t );
    
        r_data: for i in 0 to g_data_width-1 generate
            i_data_out: ODDRX2DQA port map (
                D0    => wdata(0*g_data_width + i),
                D1    => wdata(1*g_data_width + i),
                D2    => wdata(2*g_data_width + i),
                D3    => wdata(3*g_data_width + i),
                ECLK  => eclk,
                SCLK  => sclk,
                DQSW270 => dqsw270,
                RST   => rst,
                Q     => mem_dq_o(i) );
    
            i_data_in: IDDRX2DQA port map (
                Q0      => rdata(0*g_data_width + i),
                Q1      => rdata(1*g_data_width + i),
                Q2      => rdata(2*g_data_width + i),
                Q3      => rdata(3*g_data_width + i),
                QWL     => open,
                DQSR90  => dqsr90,
                RDPNTR2 => rdpntr(2),
                RDPNTR1 => rdpntr(1),
                RDPNTR0 => rdpntr(0),
                WRPNTR2 => wrpntr(2),
                WRPNTR1 => wrpntr(1),
                WRPNTR0 => wrpntr(0),
                ECLK    => eclk,
                SCLK    => sclk,
                RST     => rst,
                D       => mem_dq_i_delayed(i) );
    
            i_delay_dq: DELAYG generic map (DEL_MODE => "DQS_ALIGNED_X2")
            port map (
                A => mem_dq_i(i),
                Z => mem_dq_i_delayed(i) );
--                LOADN => delay_load,
--                MOVE  => delay_step(i),
--                DIRECTION => delay_dir );

            i_buf: BB port map (I => mem_dq_o(i), T => mem_dq_t, B => mem_dq(i), O => mem_dq_i(i));
    
        end generate;
    end block;
    
        
    i_dqs_tri: TSHX2DQSA
        port map (
            T0      => dqs_t(0),
            T1      => dqs_t(1),
            SCLK    => sclk, 
            ECLK    => eclk,
            DQSW    => dqsw0,
            RST     => rst,
            Q       => mem_dqs_t );

    r_mask: for i in 0 to g_mask_width-1 generate
    begin
        i_mask: ODDRX2DQA port map (
            D0    => wdata_m(0*g_mask_width + i),
            D1    => wdata_m(1*g_mask_width + i),
            D2    => wdata_m(2*g_mask_width + i),
            D3    => wdata_m(3*g_mask_width + i),
            ECLK  => eclk,
            SCLK  => sclk,
            DQSW270 => dqsw270,
            RST   => rst,
            Q     => mem_dm(i) );

        i_dqs_o: ODDRX2DQSB port map (
            D0    => dqs_o(0*g_mask_width + i),
            D1    => dqs_o(1*g_mask_width + i),
            D2    => dqs_o(2*g_mask_width + i),
            D3    => dqs_o(3*g_mask_width + i),
            ECLK  => eclk,
            SCLK  => sclk,
            DQSW  => dqsw0,
            RST   => rst,
            Q     => mem_dqs_o(i) );

        i_dqs_buf: BB port map (I => mem_dqs_o(i), T => mem_dqs_t, B => mem_dqs(i), O => mem_dqs_i );
        
    end generate;

    sys_clock_2x <= sclk_i;
    sys_clock    <= hclk;
    
end architecture;
