--------------------------------------------------------------------------------
-- Entity: tb_mem_io
-- Date:2022-03-26  
-- Author: gideon     
--
-- Description: Tries to run Lattice Mem_IO in ModelSim
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

library ECP5U;
use ECP5U.components.all;

entity tb_mem_io is
end entity;

architecture arch of tb_mem_io is
    constant g_addr_width   : natural := 8;
    constant g_data_width   : natural := 8;
    constant g_mask_width   : natural := 1;
    
    signal sys_reset          : std_logic;
    signal sys_clock          : std_logic;
    signal sys_clock_2x       : std_logic;
    signal sys_clock_4x       : std_logic := '0';
    signal clock_enable       : std_logic := '1';
    signal delay_step         : std_logic_vector(g_data_width-1 downto 0) := (others => '0');
    signal delay_load         : std_logic := '1';
    signal delay_dir          : std_logic := '1';
    signal stop               : std_logic := '0';
    signal uddcntln           : std_logic := '1';
    signal freeze             : std_logic := '0';
    signal pause              : std_logic := '0';
    signal readclksel         : std_logic_vector(2 downto 0) := "000";
    signal read               : std_logic_vector(1 downto 0) := "00";
    signal datavalid          : std_logic;
    signal burstdet           : std_logic;
    signal dll_lock           : std_logic;
    signal addr_first         : std_logic_vector(g_addr_width-1 downto 0);
    signal addr_second        : std_logic_vector(g_addr_width-1 downto 0);
    signal csn                : std_logic_vector(1 downto 0);
    signal wdata              : std_logic_vector(4*g_data_width-1 downto 0);
    signal wdata_t            : std_logic_vector(1 downto 0);
    signal wdata_m            : std_logic_vector(4*g_mask_width-1 downto 0);
    signal dqs_o              : std_logic_vector(4*g_mask_width-1 downto 0);
    signal dqs_t              : std_logic_vector(1 downto 0);
    signal rdata              : std_logic_vector(4*g_data_width-1 downto 0);
    signal mem_clk_p          : std_logic;
    signal mem_clk_n          : std_logic;
    signal mem_addr           : std_logic_vector(g_addr_width-1 downto 0);
    signal mem_csn            : std_logic;
    signal mem_dqs            : std_logic_vector(g_mask_width-1 downto 0);
    signal mem_dm             : std_logic_vector(g_mask_width-1 downto 0);
    signal mem_dq             : std_logic_vector(g_data_width-1 downto 0);
    signal my_net             : std_logic := '1';
    signal count              : unsigned(7 downto 0) := (others => '0');
    signal read_active        : integer := 0;
begin
    PUR_INST: entity work.P port map (PUR => my_net);
    GSR_INST: entity work.G port map (GSR => my_net);
    my_net <= '0', '1' after 50 ns;
    
    mem_dq <= std_logic_vector(count) when read_active = 2 else (others => '0') when read_active = 1 else (others => 'Z');
    mem_dqs(0) <= mem_clk_p when read_active = 2 else '0' when read_active = 1 else 'Z';
    
    sys_clock_4x <= not sys_clock_4x after 2.5 ns; -- 5 ns clock = 200 MHz
    
    i_mut: entity work.mem_io
    generic map(
        g_data_width     => 8,
        g_mask_width     => 1,
        g_addr_width     => 8
    )
    port map(
        sys_reset        => sys_reset,
        sys_clock        => sys_clock,
        sys_clock_2x     => sys_clock_2x,
        sys_clock_4x     => sys_clock_4x,
        clock_enable     => clock_enable,
        delay_step       => delay_step,
        delay_load       => delay_load,
        delay_dir        => delay_dir,
        delay_rdloadn    => delay_load,
        delay_wrloadn    => delay_load,
        stop             => stop,
        uddcntln         => uddcntln,
        freeze           => freeze,
        pause            => pause,
        readclksel       => readclksel,
        read             => read,
        datavalid        => datavalid,
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
        mem_clk_p        => mem_clk_p,
        mem_clk_n        => mem_clk_n,
        mem_addr         => mem_addr,
        mem_csn          => mem_csn,
        mem_dqs          => mem_dqs,
        mem_dm           => mem_dm,
        mem_dq           => mem_dq
    );

    sys_reset <= '1', '0' after 100 ns;

    process
    begin
        addr_first <= X"55";
        addr_second <= X"00";
        csn <= "11";
        wdata   <= X"12345678";
        wdata_t <= "11";
        wdata_m <= "0000"; 
        dqs_o <= "0000";
        dqs_t <= "11";
        read <= "00";
        wait until dll_lock = '1';
        wait until sys_clock_2x = '1';
        delay_load <= '0';
        wait until sys_clock_2x = '1';
        delay_load <= '1';
        wait until sys_clock_2x = '1';
        wait until uddcntln = '1';    
        -- do some single writes
        wait until sys_clock_2x = '1';
        wdata_t <= "00";
        dqs_t <= "00";
        dqs_o <= "1010";
        wait until sys_clock_2x = '1';
        dqs_o <= "1100";
        dqs_t <= "10";
        wdata_t <= "11";
        wait until sys_clock_2x = '1';
        dqs_t <= "11";
        for i in 1 to 1 loop
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            read <= "10";
            wait until sys_clock_2x = '1';
            read <= "11";
            wait until sys_clock_2x = '1';
            read_active <= 1;
            wait until sys_clock_2x = '1';
            read_active <= 2;
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            read <= "01";
            wait until sys_clock_2x = '1';
            read <= "00";
            wait until sys_clock_2x = '1';
            wait until sys_clock_2x = '1';
            wait for 4 ns;                
            read_active <= 0;
            read <= "00";
        end loop;
        wait;        
    end process;
    
    process(mem_clk_p) 
    begin
        -- Any edge
        count <= count + 1;
    end process;
    
    process
    begin
        uddcntln <= '1';
        wait for 1.4 us;
        for i in 1 to 10 loop
            uddcntln <= '0';
            wait for 100 ns;
            uddcntln <= '1';
            wait for 200 ns;
        end loop;        
        wait;
    end process;
    
end architecture;

