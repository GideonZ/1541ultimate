--------------------------------------------------------------------------------
-- Entity: tb_mem_io
-- Date:2022-03-26  
-- Author: gideon     
--
-- Description: Tries to run the controller in simulation with attached PHY
--------------------------------------------------------------------------------
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

entity tb_ctrl_with_phy is
end entity;

architecture arch of tb_ctrl_with_phy is
    
    signal reset              : std_logic;
    signal ctrl_clock         : std_logic := '0';
    signal mem_clock          : std_logic := '0';
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
    signal count              : unsigned(7 downto 0) := (others => '0');

    signal req                : t_mem_req_32 := (
        tag         => X"00",
        request     => '0',
        read_writen => '1',
        address     => (others => '0'),
        byte_en     => "1111",
        data        => X"CCBBAA00"
    );
    signal resp               : t_mem_resp_32;

    -- PHY controls
    signal clock_enable       : std_logic := '1';
    signal delay_step         : std_logic_vector(7 downto 0) := (others => '0');
    signal delay_load         : std_logic := '1';
    signal delay_rdloadn      : std_logic := '1';
    signal delay_wrloadn      : std_logic := '1';
    signal delay_dir          : std_logic := '1';
    signal stop               : std_logic := '0';
    signal uddcntln           : std_logic := '1';
    signal freeze             : std_logic := '0';
    signal pause              : std_logic := '0';
    signal readclksel         : std_logic_vector(2 downto 0) := "000";
    signal burstdet           : std_logic;
    signal dll_lock           : std_logic;
    signal ready              : std_logic := '0';
    -- ddr2 signals
    signal SDRAM_CLK          : std_logic;
    signal SDRAM_CLKn         : std_logic;
    signal SDRAM_CKE          : std_logic := '0';
    signal SDRAM_ODT          : std_logic := '0';
    signal SDRAM_CSn          : std_logic := '1';
    signal SDRAM_RASn         : std_logic := '1';
    signal SDRAM_CASn         : std_logic := '1';
    signal SDRAM_WEn          : std_logic := '1'; -- bit 0
    signal SDRAM_A            : std_logic_vector(13 downto 0);
    signal SDRAM_BA           : std_logic_vector(2 downto 0);
    signal SDRAM_DM           : std_logic := 'Z';
    signal SDRAM_DQ           : std_logic_vector(7 downto 0) := (others => 'Z');
    signal SDRAM_DQS          : std_logic := 'Z';

    signal my_net             : std_logic := '0';
begin
    PUR_INST: entity work.P port map (PUR => my_net);
    GSR_INST: entity work.G port map (GSR => my_net);
    my_net <= '0', '1' after 50 ns;

    mem_clock <= not mem_clock after 2.5 ns; -- 5 ns clock = 200 MHz    
    reset <= '1', '0' after 100 ns;

    i_mut: entity work.ddr2_ctrl_logic
    port map(
        clock             => ctrl_clock,
        reset             => reset,
        enable_sdram      => '1',
        refresh_en        => '1',
        inhibit           => '0',
        is_idle           => open,
        req               => req,
        resp              => resp,
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
        
--    p_pattern: process(ctrl_clock)
--        variable rw : std_logic := '0';
--        variable rq : std_logic := '1';
--        variable num_writes : integer := 0;
--    begin
--        if rising_edge(ctrl_clock) then
--            if ready='1' then
--                req.request <= rq;
--                rw := req.address(4);
--                req.read_writen <= rw;
--
--                if resp.rack = '1' then
----                    if req.read_writen = '0' and num_writes = 2 then
----                        rq := '0';
----                        req.request <= '0';
----                    end if;                        
--                    req.address <= req.address + 4;
--                    if rw = '0' and rq = '1' then
--                        req.data <= std_logic_vector(unsigned(req.data) + 1);
--                        num_writes := num_writes + 1;
--                    end if; 
--                end if;
--            end if;
--        end if;
--    end process;

    p_pattern: process
    begin
        wait until ready = '1';
        wait until ctrl_clock = '1';

        for i in 1 to 4 loop
            req.read_writen <= '0';
            req.request <= '1';
            req.byte_en <= "1001";
            L1: while true loop
                wait until ctrl_clock = '1';
                if resp.rack = '1' then
                    exit L1;
                end if;
            end loop;    
            req.request <= '0';
            req.address <= req.address + 4;
            req.data <= std_logic_vector(unsigned(req.data) + 1);
        end loop;
        wait;
    end process;

    b_sdram_read: block
        constant c_cas_latency  : natural := 3;
        constant c_additive_lat : natural := 2;
        constant c              : natural := 2*(c_cas_latency + c_additive_lat);
        type t_bytes is array (natural range <>) of std_logic_vector(7 downto 0);
        signal dqs_queue        : std_logic_vector(c+3 downto 0) := (others => '0');
        signal dqst_queue       : std_logic_vector(c+3 downto 0) := (others => '1');
        signal read_queue       : std_logic_vector(c+3 downto 0) := (others => '0');
        signal data_queue       : t_bytes(c+3 downto 0) := (others => (others => '0'));
    begin        
        process(SDRAM_CLK)
        begin
            if rising_edge(SDRAM_CLK) then
                data_queue <= X"AA" & data_queue(data_queue'high downto 1);
                read_queue <= '0' & read_queue(read_queue'high downto 1);
                dqs_queue <= '0' & dqs_queue(dqs_queue'high downto 1);
                dqst_queue <= '1' & dqst_queue(dqst_queue'high downto 1);
                if SDRAM_CSn = '0' and SDRAM_RASn = '1' and SDRAM_CASn = '0' and SDRAM_WEn = '1' then -- Start Read
                    read_queue(c+3 downto c) <= "1111";
                    dqs_queue(c+3 downto c) <= "0101";
                    dqst_queue(c+3 downto c-2) <= "000000";                
                    data_queue(c) <= X"B" & '0' & SDRAM_BA;
                    data_queue(c+1) <= SDRAM_A(7 downto 0);
                    data_queue(c+2) <= X"CC";
                    data_queue(c+3) <= X"33";
                end if;
            elsif falling_edge(SDRAM_CLK) then
                data_queue <= X"55" & data_queue(data_queue'high downto 1);
                read_queue <= '0' & read_queue(read_queue'high downto 1);
                dqs_queue <= '0' & dqs_queue(dqs_queue'high downto 1);
                dqst_queue <= '1' & dqst_queue(dqst_queue'high downto 1);
            end if;
        end process;
        SDRAM_DQ <= data_queue(0) when read_queue(0) = '1' else (others => 'Z');
        SDRAM_DQS <= dqs_queue(0) when dqst_queue(0) = '0' else 'Z';
    end block;
    
    p_phy_startup: process
    begin
        uddcntln <= '1';
        wait until dll_lock = '1';
        wait for 0.1 us;
        delay_rdloadn <= '0';
        delay_wrloadn <= '0';
        wait for 100 ns;
        delay_rdloadn <= '1';
        delay_wrloadn <= '1';
        wait for 0.1 us;
        uddcntln <= '0';
        wait for 100 ns;
        uddcntln <= '1';
        ready <= '1';
        wait;
    end process;

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
        delay_step       => delay_step,
        delay_load       => delay_load,
        delay_dir        => delay_dir,
        delay_rdloadn    => delay_rdloadn,
        delay_wrloadn    => delay_wrloadn,
        stop             => stop,
        uddcntln         => uddcntln,
        freeze           => freeze,
        pause            => pause,
        readclksel       => readclksel,
        read             => read,
        burstdet         => burstdet,
        dll_lock         => dll_lock,
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

end architecture;
