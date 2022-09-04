-------------------------------------------------------------------------------
-- Title      : Startup
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

entity startup is
generic (
    g_simulation    : boolean := false );
port (
    ref_clock     : in  std_logic;

    phase_sel     : in  std_logic_vector(1 downto 0);
    phase_dir     : in  std_logic;
    phase_step    : in  std_logic;
    phase_loadreg : in  std_logic;

    start_clock   : out std_logic;
    start_reset   : out std_logic;
    mem_clock     : out std_logic;
    mem_ready     : in  std_logic;
    
    ctrl_clock    : in  std_logic;
    ctrl_reset    : out std_logic;
    restart       : in  std_logic := '0'; -- pulse to request reboot
    button        : in  std_logic := '0';
                  
    sys_clock     : out std_logic;
    sys_reset     : out std_logic;
                  
    audio_clock   : out std_logic;
    audio_reset   : out std_logic;
                  
    eth_clock     : out std_logic;
    eth_reset     : out std_logic;

    clock_24      : out std_logic );
end entity;

architecture boot of startup is
    component pll1
    port (
        RST : in  std_logic;
        CLKI: in  std_logic;
        CLKOP: out  std_logic; 
        CLKOS: out  std_logic;
        CLKOS2: out  std_logic; 
        CLKOS3: out  std_logic; 
        ENCLKOS: in  std_logic; 
        ENCLKOS2: in  std_logic; 
        ENCLKOS3: in  std_logic; 
        PHASESEL: in  std_logic_vector(1 downto 0); 
        PHASEDIR: in  std_logic; 
        PHASESTEP: in  std_logic; 
        PHASELOADREG: in  std_logic; 
        LOCK: out  std_logic);
    end component;

    component OSCG
    port (
        OSC : out std_logic );
    end component;
    
    signal count    : unsigned(19 downto 0) := (others => '0');
    type t_state is (wait_ref_stable, do_pll_reset, wait_pll_lock, pll_locked_delay, start_clocks, wait_mem_ready, run); 
    signal state    : t_state := wait_ref_stable;

    signal audio_clock_i        : std_logic;
    signal sys_clock_i          : std_logic;
    signal sys_reset_i          : std_logic;
    signal pll_reset            : std_logic := '1';
    signal ctrl_reset_c         : std_logic;    
    signal ref_restart          : std_logic;
    signal ref_start_reset      : std_logic;
    signal ref_sys_reset        : std_logic;
    signal ref_pll_locked       : std_logic;
    signal ref_mem_ready        : std_logic;
    signal ref_button           : std_logic;
    signal pll_locked           : std_logic;
    signal enable_clocks        : std_logic := '0';
    signal start_clock_i        : std_logic := '0';
begin
    i_reset_sync: entity work.pulse_synchronizer
    port map (
        clock_in  => ctrl_clock,
        pulse_in  => restart,
        clock_out => ref_clock,
        pulse_out => ref_restart
    );

    i_button_sync: entity work.level_synchronizer
    generic map ('0')
    port map (
        clock       => ref_clock,
        reset       => '0',
        input       => button,
        input_c     => ref_button
    );

    process(ref_clock)
    begin
        if rising_edge(ref_clock) then
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
                ref_sys_reset <= '1';
                enable_clocks <= '1';
            
                if ((count = X"0FFFF" and not g_simulation) or
                    (count = X"000FF" and g_simulation)) then
                    state <= wait_mem_ready;
                end if;                
                
            when wait_mem_ready =>
                pll_reset <= '0';
                ref_start_reset <= '0';
                ref_sys_reset <= '1';
                enable_clocks <= '1';

                if ref_mem_ready = '1' and 
                    ((count >= 131072 and not g_simulation) or 
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

            if ref_restart = '1' or ref_button = '1' then
                count <= (others => '0');
                state <= wait_ref_stable;
            end if;
        end if;
    end process;
    
    i_pll: pll1
    port map (
        RST    => pll_reset,
        CLKI   => ref_clock,     -- 50 MHz
        CLKOP  => mem_clock,     -- 200 MHz
        CLKOS  => audio_clock_i, -- 12.245 MHz (47.831 kHz sample rate)
        CLKOS2 => sys_clock_i,   -- 50 MHz
        CLKOS3 => clock_24,      -- 24 MHz
        ENCLKOS  => enable_clocks, 
        ENCLKOS2 => enable_clocks, 
        ENCLKOS3 => enable_clocks, 
        PHASESEL  => phase_sel, 
        PHASEDIR  => phase_dir, 
        PHASESTEP => phase_step, 
        PHASELOADREG => phase_loadreg, 
        LOCK   => pll_locked );

    i_osc: OSCG
    port map (
        OSC => start_clock_i );
    -- start_clock_i <= sys_clock_i;    

    audio_clock <= audio_clock_i;
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
        input       => ref_sys_reset,
        input_c     => sys_reset_i
    );

    sys_reset <= sys_reset_i;

    i_pll_locked_sync: entity work.level_synchronizer
    generic map ('0')
    port map(
        clock       => ref_clock,
        input       => pll_locked,
        input_c     => ref_pll_locked
    );

    i_mem_ready_sync: entity work.level_synchronizer
    generic map ('0')
    port map(
        clock       => ref_clock,
        input       => mem_ready,
        input_c     => ref_mem_ready
    );
    
    process(ctrl_clock)
    begin
        if rising_edge(ctrl_clock) then
            ctrl_reset_c <= sys_reset_i;
            ctrl_reset   <= ctrl_reset_c;
        end if;
    end process;

    eth_clock   <= ref_clock; -- dedicated pin

--    i_24_reset: entity work.level_synchronizer
--    generic map ('1')
--    port map (
--        clock       => clock_24,
--        input       => sys_reset,
--        input_c     => aux_reset  );

    i_audio_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => audio_clock_i,
        input       => sys_reset_i,
        input_c     => audio_reset  );
    
    i_eth_reset: entity work.level_synchronizer
    generic map ('1')
    port map (
        clock       => ref_clock,
        input       => sys_reset_i,
        input_c     => eth_reset  );


end architecture;
    