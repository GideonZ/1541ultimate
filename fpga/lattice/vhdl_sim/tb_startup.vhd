-------------------------------------------------------------------------------
-- Title      : Startup Testbench
-------------------------------------------------------------------------------
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Just a small combination of some modules to visualize its
--              behavior in Lattice Modelsim
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity G is
port (GSR : in std_logic);
end entity;
architecture mine of G is
    signal GSRNET : std_logic;
begin
    GSRNET <= GSR;
end architecture;

--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity P is
port (PUR : in std_logic);
end entity;
architecture mine of P is
    signal PURNET : std_logic;
begin
    PURNET <= PUR;
end architecture;

--------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    use work.io_bus_bfm_pkg.all;
    
entity tb_startup is
end entity;

architecture tb of tb_startup is
    signal my_net       : std_logic;
    signal ref_clock    : std_logic := '0';
    -- Startup
    signal restart     : std_logic;
    signal start_clock : std_logic;
    signal start_reset : std_logic;
    signal start_ready : std_logic;
    signal toggle_reset  : std_logic;
    signal toggle_check  : std_logic;
    signal mem_clock   : std_logic := '0';
    signal sys_clock   : std_logic;
    signal sys_reset    : std_logic;
    signal ctrl_clock  : std_logic;
    signal ctrl_reset  : std_logic := '0';
    
    signal audio_clock  : std_logic;
    signal audio_reset  : std_logic;
    signal eth_clock    : std_logic;
    signal eth_reset    : std_logic;
    signal clock_24     : std_logic;
    
    -- Clock for the I/O and memory bus
    signal io_clock    : std_logic := '0';
    signal io_reset    : std_logic := '0';
    signal io_req      : t_io_req := c_io_req_init;
    signal io_resp     : t_io_resp;

    signal inhibit     : std_logic := '0';
    signal is_idle     : std_logic;
    signal reset_out   : std_logic := '0';
    signal req         : t_mem_req_32 := c_mem_req_32_init;
    signal resp        : t_mem_resp_32;

    -- PLL alignment
    signal phase_sel       : std_logic_vector(1 downto 0) := "00";
    signal phase_dir       : std_logic := '0';
    signal phase_step      : std_logic := '0';
    signal phase_loadreg   : std_logic := '0';

    -- DDR2 signals
    signal SDRAM_CLK   : std_logic;
    signal SDRAM_CLKn  : std_logic;
    signal SDRAM_CKE   : std_logic := '0'; -- bit 5
    signal SDRAM_ODT   : std_logic := '0';
    signal SDRAM_CSn   : std_logic := '1';
    signal SDRAM_RASn  : std_logic := '1';
    signal SDRAM_CASn  : std_logic := '1';
    signal SDRAM_WEn   : std_logic := '1'; -- bit 0
    signal SDRAM_TEST1 : std_logic := '1'; -- pattern
    signal SDRAM_TEST2 : std_logic := '1'; -- data_valid
    signal SDRAM_A     : std_logic_vector(13 downto 0);
    signal SDRAM_BA    : std_logic_vector(2 downto 0);
    signal SDRAM_DM    : std_logic := 'Z';
    signal SDRAM_DQ    : std_logic_vector(7 downto 0) := (others => 'Z');
    signal SDRAM_DQS   : std_logic := 'Z';
begin
    PUR_INST: entity work.P port map (PUR => my_net);
    GSR_INST: entity work.G port map (GSR => my_net);
    my_net <= '0', '1' after 50 ns;

    ref_clock <= not ref_clock after 10 ns; -- 50 MHz
    
    i_startup: entity work.startup
    generic map (true)
    port map(
        ref_clock     => ref_clock,
        phase_sel     => phase_sel,
        phase_dir     => phase_dir,
        phase_step    => phase_step,
        phase_loadreg => phase_loadreg,
        start_clock   => start_clock,
        start_reset   => start_reset,
        mem_ready     => start_ready,
        mem_clock     => mem_clock,
        ctrl_clock    => ctrl_clock,
        ctrl_reset    => ctrl_reset,
        restart       => restart,
        sys_clock     => sys_clock,
        sys_reset     => sys_reset,
        audio_clock   => audio_clock,
        audio_reset   => audio_reset,
        eth_clock     => eth_clock,
        eth_reset     => eth_reset,
        clock_24      => clock_24
    );

    i_ddr: entity work.ddr2_ctrl
    generic map(
        g_register_rdata  => true
    )
    port map(
        start_clock       => start_clock,
        start_reset       => start_reset,
        start_ready       => start_ready,

        toggle_reset      => toggle_reset,
        toggle_check      => toggle_check,

        mem_clock         => mem_clock,
        mem_clock_half    => ctrl_clock,
        sys_clock         => sys_clock,

        ctrl_clock        => ctrl_clock,
        ctrl_reset        => ctrl_reset,

        io_clock          => sys_clock,
        io_reset          => sys_reset,
        io_req            => io_req,
        io_resp           => io_resp,

        inhibit           => '0',
        is_idle           => open,
        reset_out         => restart,

        req               => req,
        resp              => resp,

        phase_sel         => phase_sel,
        phase_dir         => phase_dir,
        phase_step        => phase_step,
        phase_loadreg     => phase_loadreg,

        SDRAM_CLK         => SDRAM_CLK,
        SDRAM_CLKn        => SDRAM_CLKn,
        SDRAM_CKE         => SDRAM_CKE,
        SDRAM_ODT         => SDRAM_ODT,
        SDRAM_CSn         => SDRAM_CSn,
        SDRAM_RASn        => SDRAM_RASn,
        SDRAM_CASn        => SDRAM_CASn,
        SDRAM_WEn         => SDRAM_WEn,
        SDRAM_TEST1       => SDRAM_TEST1,
        SDRAM_TEST2       => SDRAM_TEST2,
        SDRAM_A           => SDRAM_A,
        SDRAM_BA          => SDRAM_BA,
        SDRAM_DM          => SDRAM_DM,
        SDRAM_DQ          => SDRAM_DQ,
        SDRAM_DQS         => SDRAM_DQS
    );
    
    i_io_bfm: entity work.io_bus_bfm
    generic map (
        g_name => "io"
    )
    port map (
        clock  => sys_clock,
        req    => io_req,
        resp   => io_resp
    );
    
    process
        variable io : p_io_bus_bfm_object;
    begin
        wait until sys_reset = '0';
        bind_io_bus_bfm(named => "io", pntr => io );
        wait until sys_clock = '1';
        io_write(io, X"C", X"01" );
        io_write(io, X"C", X"09" );
        io_write(io, X"0", X"AA" );
        io_write(io, X"1", X"55" );
        io_write(io, X"2", X"00" );
        wait for 10 us;
        io_write(io, X"B", X"20" ); -- restart
    end process;
    

end architecture;
