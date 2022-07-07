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

entity ddr2_ctrl_fast is
generic (
    SDRAM_Refr_delay   : integer := 9;
    SDRAM_Refr_period  : integer := 488 );
port (
    ref_clock   : in  std_logic := '0';
    ref_reset   : in  std_logic := '0';

    sys_clock_o : out std_logic;
    sys_reset_o : out std_logic;

    user_clock_1  : out std_logic := '0';
    user_clock_2  : out std_logic := '0';
    user_clock_3  : out std_logic := '0';

    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';

    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

    inhibit     : in  std_logic;
    is_idle     : out std_logic;
    cal_done    : out std_logic;

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

architecture Gideon of ddr2_ctrl_fast is

    signal phasecounterselect : std_logic_vector(2 downto 0);
    signal phasestep          : std_logic_vector(3 downto 0);
    signal phaseupdown        : std_logic;
    signal phasedone          : std_logic;
    signal mode               : std_logic_vector(3 downto 0);

    signal addr_first         : std_logic_vector(22 downto 0);
    signal addr_second        : std_logic_vector(22 downto 0);
    signal wdata              : std_logic_vector(31 downto 0);
    signal wdata_m            : std_logic_vector(3 downto 0);
    signal wdata_t            : std_logic_vector(1 downto 0);
    signal read               : std_logic_vector(1 downto 0);
    signal read_d             : std_logic_vector(5 downto 0);
    signal rdata_valid        : std_logic;

    signal phy_wdata          : std_logic_vector(35 downto 0);
    signal phy_rdata          : std_logic_vector(35 downto 0);
    signal phy_write          : std_logic;
    signal clock_enable       : std_logic;
    signal odt_enable         : std_logic;
    signal refresh_enable     : std_logic;
    signal rdata              : std_logic_vector(31 downto 0);
    signal ext_addr         : std_logic_vector(15 downto 0);
    signal ext_cmd          : std_logic_vector(3 downto 0);
    signal ext_cmd_valid    : std_logic;
    signal ext_cmd_done     : std_logic;
begin

    i_ctrl: entity work.ddr2_ctrl_logic
    port map(
        clock             => clock,
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
        wdata             => wdata,
        wdata_t           => wdata_t,
        wdata_m           => wdata_m,
        dqs_o             => open,
        dqs_t             => open,
        read              => read,
        rdata             => rdata,
        rdata_valid       => rdata_valid
    );

    -- Interleave dq and dqm
    phy_wdata <= wdata_m(3) & wdata(31 downto 24) & wdata_m(2) & wdata(23 downto 16) & 
                 wdata_m(1) & wdata(15 downto 08) & wdata_m(0) & wdata(07 downto 00);

    rdata     <= phy_rdata(34 downto 27) & phy_rdata(25 downto 18) & phy_rdata(16 downto 9) & phy_rdata(7 downto 0);

    read_d      <= read_d(read_d'high-1 downto 0) & read(0) when rising_edge(clock);
    rdata_valid <= read_d(2 + to_integer(unsigned(mode(3 downto 2)))) when rising_edge(clock);    
    phy_write   <= not wdata_t(0);

    i_phy: entity work.mem_io
    generic map (
        g_data_width       => 9,
        g_addr_cmd_width   => 23
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
        mode               => mode(1 downto 0),
        addr_first         => addr_first,
        addr_second        => addr_second,
        wdata              => phy_wdata,
        wdata_oe           => phy_write,
        rdata              => phy_rdata,

        user_clock_1       => user_clock_1,
        user_clock_2       => user_clock_2,
        user_clock_3       => user_clock_3,

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
                when X"5" =>
                    io_resp.data(0) <= phasedone;
                when X"D" =>
                    io_resp.data(3 downto 0) <= X"2"; -- Write Recovery
                when X"E" =>
                    io_resp.data(3 downto 0) <= X"1"; -- Additive Latency
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
                    mode <= io_req.data(3 downto 0);
                when X"9" =>
                    phasecounterselect <= io_req.data(2 downto 0);
                    phaseupdown        <= io_req.data(3);
                    phasestep          <= io_req.data(7 downto 4);
                when X"C" =>
                    clock_enable       <= io_req.data(0);
                    odt_enable         <= io_req.data(1);
                    refresh_enable     <= io_req.data(2);
                    if io_req.data(7) = '1' then 
                        cal_done <= '1';
                    end if;
                when others =>
                    null;
                end case;
            end if;           
            if reset = '1' then
                cal_done <= '0';
                mode <= "0000";
                clock_enable <= '0';
                odt_enable <= '0';
                refresh_enable <= '0';
                phasecounterselect <= "000";
                phaseupdown <= '0';
                phasestep <= (others => '0');
                ext_cmd_valid <= '0';
            end if;                     
        end if;
    end process;

end Gideon;

