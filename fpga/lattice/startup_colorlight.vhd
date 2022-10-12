-------------------------------------------------------------------------------
-- Title      : startup_colorlight
-------------------------------------------------------------------------------
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements a sequencer that resets various modules
--              in the FPGA.
--              1) PLL is held in reset until the input clock is assumed stable
--              2) State machine waits for PLL lock
--              3) Start Reset is released, which causes the mem_sync to do its thing
--              4) Wait for mem_ready, which indicates that the mem dll is in lock
--              5) SCLK must now be available, so sys_reset can be released
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity startup_colorlight is
generic (
    g_simulation    : boolean := false );
port (
    ref_clock     : in  std_logic;

    start_clock   : out std_logic;
    start_reset   : out std_logic;

    sys_clock     : out std_logic;
    sys_reset     : out std_logic;
                  
    eth_clock     : out std_logic;
    eth_reset     : out std_logic );

end entity;

architecture boot of startup_colorlight is
    component pll2
    port (
        RST : in  std_logic;
        CLKI: in  std_logic;
        CLKOP: out  std_logic; 
        CLKOS: out  std_logic;
        ENCLKOS: in  std_logic; 
        LOCK: out  std_logic);
    end component;

    component OSCG
    port (
        OSC : out std_logic );
    end component;
    
    signal count    : unsigned(19 downto 0) := (others => '0');
    type t_state is (wait_ref_stable, do_pll_reset, wait_pll_lock, pll_locked_delay, start_clocks, wait_mem_ready, run); 
    signal state    : t_state := wait_ref_stable;

    signal sys_clock_i          : std_logic;
    signal sys_reset_i          : std_logic;
    signal eth_clock_i          : std_logic;
    signal pll_reset            : std_logic := '1';
    signal ref_start_reset      : std_logic;
    signal ref_sys_reset        : std_logic;
    signal ref_pll_locked       : std_logic;
    signal pll_locked           : std_logic;
    signal enable_clocks        : std_logic := '0';
    signal start_clock_i        : std_logic := '0';
begin
    process(start_clock_i)
    begin
        if rising_edge(start_clock_i) then
            count <= count + 1;

            case state is
            when wait_ref_stable =>
                pll_reset <= '0';
                ref_start_reset <= '1';
                ref_sys_reset <= '1';
                enable_clocks <= '0';
                
                if count(7 downto 0) = X"FF" then
                    state <= do_pll_reset;
                end if;

            when do_pll_reset =>
                pll_reset <= '1';
                ref_start_reset <= '1';
                ref_sys_reset <= '1';
                enable_clocks <= '0';

                if ((count = X"FFFFF" and not g_simulation) or 
                    (count = X"00FFF" and g_simulation)) then
                    state <= wait_pll_lock;
                end if; 

            when wait_pll_lock =>
                pll_reset <= '0';
                ref_start_reset <= '1';
                ref_sys_reset <= '1';
                enable_clocks <= '0';
                count <= (others => '0');
                
                if ref_pll_locked = '1' then
                    state <= pll_locked_delay;
                end if;
            
            when pll_locked_delay =>
                pll_reset <= '0';
                ref_start_reset <= '1';
                ref_sys_reset <= '1';
                enable_clocks <= '0';

                if ((count = X"07FFF" and not g_simulation) or
                    (count = X"0007F" and g_simulation)) then
                    state <= start_clocks;
                end if;
            
            when start_clocks =>
                pll_reset <= '0';
                ref_start_reset <= '1';
                ref_sys_reset <= count(8);
                enable_clocks <= '1';
            
                if ((count = X"001FF" and not g_simulation) or
                    (count = X"000FF" and g_simulation)) then
                    state <= wait_mem_ready;
                end if;                
                
            when wait_mem_ready =>
                pll_reset <= '0';
                ref_start_reset <= '0';
                ref_sys_reset <= '1';
                enable_clocks <= '1';

                if ((count >= 131072 and not g_simulation) or 
                    (count >= 1000 and g_simulation)) then
                    state <= run;
                end if;

            when run =>
                pll_reset <= '0';
                ref_start_reset <= '0';
                ref_sys_reset <= '0';
                enable_clocks <= '1';
                
                if ref_pll_locked = '0' then
                    state <= wait_ref_stable;
                    count <= (others => '0');
                end if;                        

            when others =>
                state <= wait_ref_stable;
                count <= (others => '0');

            end case;
        end if;
    end process;
    
    i_pll: pll2
    port map (
        RST    => pll_reset,
        CLKI   => ref_clock,     -- 25 MHz
        CLKOP  => eth_clock_i,   -- 125 MHz
        CLKOS  => sys_clock_i,   -- 50 MHz
        ENCLKOS  => enable_clocks, 
        LOCK   => pll_locked );

    i_osc: OSCG
    port map (
        OSC => start_clock_i );
    -- start_clock_i <= sys_clock_i;    

    sys_clock   <= sys_clock_i;
    start_clock <= start_clock_i;

    i_start_reset_sync: entity work.level_synchronizer
    generic map ('1')
    port map(
        clock       => start_clock_i,
        input       => ref_start_reset,
        input_c     => start_reset
    );
    
    i_sys_reset_sync: entity work.level_synchronizer
    generic map ('1')
    port map(
        clock       => sys_clock_i,
        reset       => ref_sys_reset,
        input       => '0',
        input_c     => sys_reset_i
    );

    sys_reset <= sys_reset_i;

    i_pll_locked_sync: entity work.level_synchronizer
    generic map ('0')
    port map(
        clock       => start_clock_i,
        input       => pll_locked,
        input_c     => ref_pll_locked
    );

    eth_clock   <= eth_clock_i; -- dedicated pin

    i_eth_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => eth_clock_i,
        input       => sys_reset_i,
        input_c     => eth_reset  );

end architecture;
    