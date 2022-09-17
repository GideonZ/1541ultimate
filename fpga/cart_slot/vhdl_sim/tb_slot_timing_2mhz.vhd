library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.slot_bus_pkg.all;

entity tb_slot_timing_2mhz is
end entity;

architecture tb of tb_slot_timing_2mhz is
    signal clock           : std_logic := '1';
    signal ctrl_clock      : std_logic := '1';
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
    signal clear_inhibit   : std_logic;
    signal do_sample_addr  : std_logic;
    signal do_probe_end    : std_logic;
    signal do_sample_io    : std_logic;
    signal do_io_event     : std_logic;
    constant pal_period     : time := 1016 ns;
    constant ntsc_period    : time := 978 ns;
    signal phi2_period     : time := pal_period / 2;
    signal stop             : boolean := false;
    signal concurrent_req   : t_mem_req_32 := c_mem_req_32_init;
    signal concurrent_resp  : t_mem_resp_32;
    signal cart_req         : t_mem_req_32 := c_mem_req_32_init;
    signal cart_resp        : t_mem_resp_32;
    signal cart_dack        : std_logic;
    signal cart_rack        : std_logic;
    signal mem_req          : t_mem_req_32;
    signal mem_resp         : t_mem_resp_32;
    signal mem_req_2x       : t_mem_req_32;
    signal mem_resp_2x      : t_mem_resp_32;
    signal slot_req         : t_slot_req;
    signal slot_resp        : t_slot_resp := c_slot_resp_init;
    signal mem_inhibit_2x   : std_logic;
    signal read             : std_logic_vector(1 downto 0);
    signal rdata_valid      : std_logic;
    signal RWn              : std_logic := '1';
    signal data             : std_logic_vector(7 downto 0);
    signal data_o           : std_logic_vector(7 downto 0);
    signal data_t           : std_logic;
    signal read_d           : std_logic_vector(5 downto 0) := (others => '0');

    signal mintime_cpu      : time := 1000 ns;
    signal maxtime_cpu      : time := 0 ns;
    signal mintime_vic      : time := 1000 ns;
    signal maxtime_vic      : time := 0 ns;
begin
    clock <= not clock after 10 ns; -- 50 MHz
    ctrl_clock <= not ctrl_clock after 5 ns; -- 100 MHz
    reset <= '1', '0' after 100 ns;
    PHI2 <= not PHI2 after phi2_period when not stop;
    
    cart_req.tag <= X"01";
    cart_req.address <= (others => '0');
    cart_dack <= '1' when cart_resp.dack_tag = X"01" else '0';
    cart_rack <= cart_resp.rack when cart_resp.rack_tag = X"01" else '0';
    data <= data_o when data_t = '1' else (others => 'Z');

    concurrent_req.tag <= X"02";
    concurrent_req.address <= (8 => '1', others => '0');
    concurrent_req.read_writen <= '0'; -- continuous writing
    concurrent_req.data <= X"DEADBABE";
    concurrent_req.request <= '1';

    i_mem_bus_arbiter_pri_32: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_registered => false,
        g_ports      => 2
    )
    port map (
        clock   => clock,
        reset   => reset,
        inhibit => reqs_inhibit,
        reqs(0) => cart_req,
        reqs(1) => concurrent_req,
        resps(0)=> cart_resp,
        resps(1)=> concurrent_resp,
        req     => mem_req,
        resp    => mem_resp
    );

    i_slot_slave: entity work.slot_slave
    generic map (
        g_big_endian => false
    )
    port map (
        clock          => clock,
        reset          => reset,
        VCC            => '1',
        RSTn           => '1',
        IO1n           => '1',
        IO2n           => '1',
        ROMLn          => '0',
        ROMHn          => '1',
        BA             => BA,
        GAMEn          => '1',
        EXROMn         => '0',
        RWn            => RWn,
        ADDRESS        => X"A123",
        DATA_in        => data,
        DATA_out       => data_o,
        DATA_tri       => data_t,
        mem_req        => cart_req.request,
        mem_rwn        => cart_req.read_writen,
        mem_rack       => cart_rack,
        mem_dack       => cart_dack,
        mem_rdata      => cart_resp.data,
        mem_wdata      => cart_req.data,
        reset_out      => open,
        phi2_tick      => phi2_tick,
        do_sample_addr => do_sample_addr,
        do_probe_end   => do_probe_end,
        do_sample_io   => do_sample_io,
        do_io_event    => do_io_event,
        clear_inhibit  => clear_inhibit,
        dma_active_n   => '1',
        allow_serve    => allow_serve,
        serve_128      => '1',
        serve_rom      => '1',
        serve_io1      => '1',
        serve_io2      => '1',
        allow_write    => '1',
        kernal_enable  => '0',
        kernal_probe   => open,
        kernal_area    => open,
        force_ultimax  => open,
        epyx_timeout   => open,
        cpu_write      => open,
        slot_req       => slot_req,
        slot_resp      => slot_resp,
        BUFFER_ENn     => open
    );

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
        clear_inhibit  => clear_inhibit,
        do_sample_addr => do_sample_addr,
        do_probe_end   => do_probe_end,
        do_sample_io   => do_sample_io,
        do_io_event    => do_io_event
    );

    i_double_freq_bridge: entity work.memreq_halfrate
    port map(
        phase_out   => open,
        toggle_r_2x => reset,
        clock_1x    => clock,
        clock_2x    => ctrl_clock,
        reset_1x    => reset,
        mem_req_1x  => mem_req,
        mem_resp_1x => mem_resp,
        inhibit_1x  => refr_inhibit,
        mem_req_2x  => mem_req_2x,
        mem_resp_2x => mem_resp_2x,
        inhibit_2x  => mem_inhibit_2x
    );

    i_ddr2_ctrl_logic: entity work.ddr2_ctrl_logic
    port map (
        clock         => ctrl_clock,
        reset         => reset,
        enable_sdram  => '1',
        refresh_en    => '1',
        odt_enable    => '1',
        read_delay    => "11",
        inhibit       => mem_inhibit_2x,
        is_idle       => open,
        req           => mem_req_2x,
        resp          => mem_resp_2x,
        addr_first    => open,
        addr_second   => open,
        wdata         => open,
        wdata_t       => open,
        wdata_m       => open,
        dqs_o         => open,
        dqs_t         => open,
        read          => read,
        rdata         => X"12345678",
        rdata_valid   => rdata_valid
    );

    read_d <= read_d(read_d'high-1 downto 0) & read(0) when rising_edge(ctrl_clock);
    rdata_valid <= read_d(read_d'high);

    process
    begin
        -- VIC is busy, PAL mode
        -- Serve PHI2 high and PHI2 low
        serve_enable <= '1';
        serve_vic <= '1';
        BA <= '0';
        RWn <= '1';
        wait for 10 us;
        wait until PHI2 = '1';
        BA <= '1';
        for i in 0 to 250 loop
            -- read
            wait until PHI2'event;
            wait for 50 ns;
            RWn <= '1';
            -- read
            wait until PHI2'event;
            wait for 50 ns;
            RWn <= '1';
            -- read
            wait until PHI2'event;
            wait for 50 ns;
            RWn <= '1';
            -- write
            wait until PHI2'event;
            wait for 50 ns;
            RWn <= '0';
        end loop;
        wait until PHI2 = '1';
        wait for 50 ns;
        RWn <= '1';
        stop <= true;
        wait;
    end process;    

    process
    begin
        wait until PHI2 = '1';
        wait until PHI2 = '1';
        wait until PHI2 = '1';
        while true loop
            wait until PHI2 = '0';
            if RWn = '1' then
                if data_t'last_event < mintime_cpu then
                    mintime_cpu <= data_t'last_event;
                end if;
                if data_t'last_event > maxtime_cpu then
                    maxtime_cpu <= data_t'last_event;
                end if;
                if data_t'last_event < 150 ns then
                    report "CPU:" & time'image(data_t'last_event);
                end if;
            end if;
            wait until PHI2 = '1';
            if RWn = '1' then
                if data_t'last_event < mintime_vic then
                    mintime_vic <= data_t'last_event;
                end if;
                if data_t'last_event > maxtime_vic then
                    maxtime_vic <= data_t'last_event;
                end if;
                if data_t'last_event < 150 ns then
                    report "VIC:" & time'image(data_t'last_event);
                end if;
            end if;
        end loop;
    end process;

end architecture;
