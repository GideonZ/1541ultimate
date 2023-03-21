library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_slot_timing is
end entity;

architecture tb of tb_slot_timing is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal PHI2            : std_logic := '0';
    signal BA              : std_logic := '1';
    signal serve_vic       : std_logic := '1';
    signal serve_enable    : std_logic := '1';
    signal serve_inhibit   : std_logic := '0';
    signal timing_addr     : unsigned(2 downto 0) := "011";
    signal edge_recover    : std_logic := '0';
    signal allow_serve     : std_logic;
    signal phi2_tick       : std_logic;
    signal phi2_fall       : std_logic;
    signal phi2_recovered  : std_logic;
    signal clock_det       : std_logic;
    signal vic_cycle       : std_logic;    
    signal refr_inhibit    : std_logic;
    signal reqs_inhibit    : std_logic;
    signal do_sample_addr  : std_logic;
    signal do_probe_end    : std_logic;
    signal do_sample_io    : std_logic;
    signal do_io_event     : std_logic;
    constant pal_period     : time := 1016 ns;
    constant ntsc_period    : time := 978 ns;
    signal phi2_period     : time := pal_period / 2;
    signal stop             : boolean := false;
begin
    clock <= not clock after 10 ns; -- 50 MHz
    reset <= '1', '0' after 100 ns;
    PHI2 <= not PHI2 after phi2_period when not stop;
    
    i_dut: entity work.slot_timing
    port map(
        clock          => clock,
        reset          => reset,
        PHI2           => PHI2,
        BA             => BA,
        serve_vic      => serve_vic,
        serve_enable   => serve_enable,
        serve_inhibit  => serve_inhibit,
        timing_addr    => timing_addr,
        edge_recover   => edge_recover,
        allow_serve    => allow_serve,
        phi2_tick      => phi2_tick,
        phi2_fall      => phi2_fall,
        phi2_recovered => phi2_recovered,
        clock_det      => clock_det,
        vic_cycle      => vic_cycle,
        refr_inhibit   => refr_inhibit,
        reqs_inhibit   => reqs_inhibit,
        do_sample_addr => do_sample_addr,
        do_probe_end   => do_probe_end,
        do_sample_io   => do_sample_io,
        do_io_event    => do_io_event
    );

    process
    begin
        -- VIC is busy, PAL mode
        -- Serve PHI2 high and PHI2 low
        serve_enable <= '1';
        serve_vic <= '1';
        BA <= '0';
        wait for 100 us;
        wait until PHI2 = '1';
        BA <= '1';
        wait for 100 us;
        wait until PHI2 = '1';
        serve_inhibit <= '1';
        wait for 100 us;
        serve_vic <= '0';
        serve_inhibit <= '0';
        wait for 100 us;
        wait until PHI2 = '1';
        
        -- NTSC mode
        phi2_period <= ntsc_period / 2;
        serve_enable <= '1';
        serve_vic <= '1';
        BA <= '0';
        wait for 100 us;
        wait until PHI2 = '1';
        BA <= '1';
        wait for 100 us;
        wait until PHI2 = '1';
        serve_inhibit <= '1';
        wait for 100 us;
        serve_vic <= '0';
        serve_inhibit <= '0';
        wait for 100 us;
        wait until PHI2 = '1';
                
        stop <= true;
        wait;
    end process;    
end architecture;
