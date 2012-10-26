library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.dma_bus_pkg.all;
use work.slot_bus_pkg.all;
use work.cart_slot_pkg.all;

entity slot_server_v4 is
generic (
    g_tag_slot      : std_logic_vector(7 downto 0) := X"08";
    g_tag_reu       : std_logic_vector(7 downto 0) := X"10";
    g_ram_base_reu  : unsigned(27 downto 0) := X"1000000"; -- should be on 16M boundary, or should be limited in size
    g_ram_base_cart : unsigned(27 downto 0) := X"0F70000"; -- should be on a 64K boundary
    g_rom_base_cart : unsigned(27 downto 0) := X"0F80000"; -- should be on a 512K boundary
    g_control_read  : boolean := true;
    g_command_intf  : boolean := true;
    g_ram_expansion : boolean := true;
    g_extended_reu  : boolean := false;
    g_sampler       : boolean := false;
    g_implement_sid : boolean := true;
    g_sid_voices    : natural := 3;
    g_vic_copper    : boolean := false );

port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- Cartridge pins
    RSTn            : inout std_logic;
    IRQn            : inout std_logic;
    NMIn            : inout std_logic;
    PHI2            : in    std_logic;
    IO1n            : in    std_logic;
    IO2n            : in    std_logic;
    DMAn            : out   std_logic := '1';
    BA              : in    std_logic := '0';
    ROMLn           : in    std_logic;
    ROMHn           : in    std_logic;
    GAMEn           : inout std_logic;
    EXROMn          : inout std_logic;
    RWn             : inout std_logic;
    ADDRESS         : inout std_logic_vector(15 downto 0);
    DATA            : inout std_logic_vector(7 downto 0);

    -- other hardware pins
    BUFFER_ENn      : out   std_logic;

	buttons 		: in    std_logic_vector(2 downto 0);
    cart_led_n      : out   std_logic;
    
    trigger_1       : out   std_logic;
    trigger_2       : out   std_logic;

    -- debug
    freezer_state   : out   std_logic_vector(1 downto 0);

    -- audio output
    sid_pwm_left    : out   std_logic := '0';
    sid_pwm_right   : out   std_logic := '0';
    samp_pwm_left   : out   std_logic := '0';
    samp_pwm_right  : out   std_logic := '0';

    -- timing output
    phi2_tick       : out   std_logic;
	c64_stopped		: out   std_logic;
	
    -- master on memory bus
    memctrl_inhibit : out   std_logic;
    mem_req         : out   t_mem_req;
    mem_resp        : in    t_mem_resp;
    
    -- slave on io bus
    io_req          : in    t_io_req;
    io_resp         : out   t_io_resp );

end slot_server_v4;    

architecture structural of slot_server_v4 is

    signal phi2_tick_i     : std_logic;
    signal phi2_recovered  : std_logic;
    signal vic_cycle       : std_logic;    
    signal do_sample_addr  : std_logic;
    signal do_sample_io    : std_logic;
    signal do_io_event     : std_logic;
    signal timing_inhibit  : std_logic;

    signal slave_dout      : std_logic_vector(7 downto 0);
    signal slave_dtri      : std_logic := '0';
    
    signal master_dout     : std_logic_vector(7 downto 0);
    signal master_dtri     : std_logic := '0';

    signal address_tri_l   : std_logic;
    signal address_tri_h   : std_logic;
    signal address_out     : std_logic_vector(15 downto 0);

    signal rwn_tri         : std_logic;
    signal rwn_out         : std_logic;

    signal control         : t_cart_control;
    signal status          : t_cart_status;

    signal allow_serve     : std_logic;

    -- interface with freezer (cartridge) logic
    signal serve_enable    : std_logic := '0'; -- from cartridge emulation logic
    signal serve_vic       : std_logic := '0';
    signal serve_rom       : std_logic := '0'; -- ROML or ROMH
    signal serve_io1       : std_logic := '0'; -- IO1n
    signal serve_io2       : std_logic := '0'; -- IO2n
    signal allow_write     : std_logic := '0';
    
    signal cpu_write       : std_logic;
    signal epyx_timeout    : std_logic;
    
    signal reu_dma_n       : std_logic := '1'; -- direct from REC

    signal mask_buttons     : std_logic := '0';
    signal reset_button     : std_logic;
    signal freeze_button    : std_logic;
	
    signal actual_c64_reset : std_logic;
    
    signal nmi_n            : std_logic := '1';
    signal irq_n            : std_logic := '1';
    signal exrom_n          : std_logic := '1';
    signal game_n           : std_logic := '1';

    signal unfreeze         : std_logic;
    signal freeze_trig      : std_logic;
    signal freeze_act       : std_logic;

    signal io_req_dma       : t_io_req;
    signal io_resp_dma      : t_io_resp := c_io_resp_init;
    signal io_req_peri      : t_io_req;
    signal io_resp_peri     : t_io_resp := c_io_resp_init;
    signal io_req_sid       : t_io_req;
    signal io_resp_sid      : t_io_resp := c_io_resp_init;
    signal io_req_regs      : t_io_req;
    signal io_resp_regs     : t_io_resp := c_io_resp_init;
    signal io_req_cmd       : t_io_req;
    signal io_resp_cmd      : t_io_resp := c_io_resp_init;
    signal io_req_copper    : t_io_req;
    signal io_resp_copper   : t_io_resp := c_io_resp_init;
    signal io_req_samp_cpu  : t_io_req;
    signal io_resp_samp_cpu : t_io_resp := c_io_resp_init;
    
    signal dma_req_io       : t_dma_req;
    signal dma_resp_io      : t_dma_resp := c_dma_resp_init;
    signal dma_req_reu      : t_dma_req;
    signal dma_resp_reu     : t_dma_resp := c_dma_resp_init;
    signal dma_req_copper   : t_dma_req;
    signal dma_resp_copper  : t_dma_resp := c_dma_resp_init;
    signal dma_req          : t_dma_req;
    signal dma_resp         : t_dma_resp := c_dma_resp_init;

    signal slot_req         : t_slot_req;
    signal slot_resp        : t_slot_resp := c_slot_resp_init;
    signal slot_resp_reu    : t_slot_resp := c_slot_resp_init;
    signal slot_resp_cart   : t_slot_resp := c_slot_resp_init;
    signal slot_resp_sid    : t_slot_resp := c_slot_resp_init;
    signal slot_resp_cmd    : t_slot_resp := c_slot_resp_init;
    signal slot_resp_samp   : t_slot_resp := c_slot_resp_init;
    
    signal mem_req_slot     : t_mem_req   := c_mem_req_init; 
    signal mem_resp_slot    : t_mem_resp  := c_mem_resp_init;
    signal mem_req_reu      : t_mem_req   := c_mem_req_init; 
    signal mem_resp_reu     : t_mem_resp  := c_mem_resp_init;
    signal mem_req_samp     : t_mem_req   := c_mem_req_init;
    signal mem_resp_samp    : t_mem_resp  := c_mem_resp_init;
    
--    signal mem_req_trace    : t_mem_req;
--    signal mem_resp_trace   : t_mem_resp;
    
    signal mem_rack_slot    : std_logic;
    signal mem_dack_slot    : std_logic;

    signal sid_sample_left  : signed(17 downto 0);
    signal sid_sample_right : signed(17 downto 0);
    signal sample_L         : signed(17 downto 0);
    signal sample_R         : signed(17 downto 0);

begin
    reset_button  <= buttons(0) when control.swap_buttons='0' else buttons(2);
    freeze_button <= buttons(2) when control.swap_buttons='0' else buttons(0);

    i_split_64K: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 16,
        g_range_hi  => 16,
        g_ports     => 2 )
    port map (
        clock    => clock,
        
        req      => io_req,
        resp     => io_resp,
    
        reqs(0)  => io_req_peri, -- 4040000
        reqs(1)  => io_req_dma,  -- 4050000
        
        resps(0) => io_resp_peri,
        resps(1) => io_resp_dma );
        
    i_bridge: entity work.io_to_dma_bridge
    port map (
        clock       => clock,
        reset       => reset,
                    
        c64_stopped => status.c64_stopped,
        
        io_req      => io_req_dma,
        io_resp     => io_resp_dma,
        
        dma_req     => dma_req_io,
        dma_resp    => dma_resp_io );

    i_split_8K: entity work.io_bus_splitter
    generic map (
        g_range_lo  => 13,
        g_range_hi  => 15,
        g_ports     => 5 )
    port map (
        clock    => clock,
        
        req      => io_req_peri,
        resp     => io_resp_peri,
           
        reqs(0)  => io_req_regs,     -- 4040000
        reqs(1)  => io_req_sid,      -- 4042000
        reqs(2)  => io_req_cmd,      -- 4044000
        reqs(3)  => io_req_copper,   -- 4046000
        reqs(4)  => io_req_samp_cpu, -- 4048000
        
        resps(0) => io_resp_regs,
        resps(1) => io_resp_sid,
        resps(2) => io_resp_cmd,
        resps(3) => io_resp_copper,
        resps(4) => io_resp_samp_cpu );
        

    i_registers: entity work.cart_slot_registers
    generic map (
        g_rom_base      => g_rom_base_cart,
        g_ram_base      => g_ram_base_cart,
--        g_control_read  => g_control_read,
        g_ram_expansion => g_ram_expansion )
    port map (
        clock           => clock,
        reset           => reset,
        
        io_req          => io_req_regs,
        io_resp         => io_resp_regs,

        control         => control,
        status          => status );
        

    i_timing: entity work.slot_timing
    port map (
        clock           => clock,
        reset           => reset,

        -- Cartridge pins
        PHI2            => PHI2,
        BA              => BA,
    
        serve_vic       => serve_vic,
        serve_enable    => serve_enable,
        serve_inhibit   => status.c64_stopped,
        allow_serve     => allow_serve,
    
        phi2_tick       => phi2_tick_i,
        phi2_recovered  => phi2_recovered,
        clock_det       => status.clock_detect,
        vic_cycle       => vic_cycle,    
    
        inhibit         => timing_inhibit,
        do_sample_addr  => do_sample_addr,
        do_sample_io    => do_sample_io,
        do_io_event     => do_io_event );

    mem_req_slot.tag <= g_tag_slot;
    mem_rack_slot <= '1' when mem_resp_slot.rack_tag = g_tag_slot else '0';
    mem_dack_slot <= '1' when mem_resp_slot.dack_tag = g_tag_slot else '0';

    i_slave: entity work.slot_slave
    port map (
        clock           => clock,
        reset           => reset,
        
        -- Cartridge pins
        RSTn            => RSTn,
        IO1n            => IO1n,
        IO2n            => IO2n,
        ROMLn           => ROMLn,
        ROMHn           => ROMHn,
        GAMEn           => GAMEn,
        EXROMn          => EXROMn,
        RWn             => RWn,
        BA              => BA,
        ADDRESS         => ADDRESS,
        DATA_in         => DATA,
        DATA_out        => slave_dout,
        DATA_tri        => slave_dtri,
    
        -- interface with memory controller
        mem_req         => mem_req_slot.request,
        mem_rwn         => mem_req_slot.read_writen,
        mem_wdata       => mem_req_slot.data,
        mem_rack        => mem_rack_slot,
        mem_dack        => mem_dack_slot,
        mem_rdata       => mem_resp_slot.data,
        -- mem_addr comes from cartridge logic
    
        -- synchronized outputs
        reset_out       => actual_c64_reset,
        
        -- timing inputs
        phi2_tick       => phi2_tick_i,
        do_sample_addr  => do_sample_addr,
        do_sample_io    => do_sample_io,
        do_io_event     => do_io_event,
    
        -- interface with freezer (cartridge) logic
        allow_serve     => allow_serve,
        serve_rom       => serve_rom, -- ROML or ROMH
        serve_io1       => serve_io1, -- IO1n
        serve_io2       => serve_io2, -- IO2n
        allow_write     => allow_write,
    
        cpu_write       => cpu_write,
        epyx_timeout    => epyx_timeout,
        
        slot_req        => slot_req,
        slot_resp       => slot_resp,
        
        -- interface with hardware
        BUFFER_ENn      => BUFFER_ENn );

    i_master: entity work.slot_master_v4
    port map (
        clock           => clock,
        reset           => reset,
        
        -- Cartridge pins
        DMAn            => DMAn,
        BA              => BA,
        RWn_in          => RWn,
        RWn_out         => rwn_out,
        RWn_tri         => rwn_tri,
        
        ADDRESS_out     => address_out,
        ADDRESS_tri_h   => address_tri_h,
        ADDRESS_tri_l   => address_tri_l,
        
        DATA_in         => DATA,
        DATA_out        => master_dout,
        DATA_tri        => master_dtri,
    
        -- timing inputs
        vic_cycle       => vic_cycle,    
        phi2_recovered  => phi2_recovered,
        phi2_tick       => phi2_tick_i,
        do_sample_addr  => do_sample_addr,
        do_sample_io    => do_sample_io,
        do_io_event     => do_io_event,
        reu_dma_n       => reu_dma_n,
        
        -- request from the cpu to do a cycle on the cart bus
        dma_req         => dma_req,
        dma_resp        => dma_resp,
    
        -- system control
        stop_cond       => control.c64_stop_mode,
        c64_stop        => control.c64_stop,
        c64_stopped     => status.c64_stopped );
    

    i_freeze: entity work.freezer
    port map (
        clock           => clock,
        reset           => reset,

        RST_in          => reset_button,
        button_freeze   => freeze_button,
    
        cpu_cycle_done  => do_io_event,
        cpu_write       => cpu_write,

        freezer_state   => freezer_state,

        unfreeze        => unfreeze,
        freeze_trig     => freeze_trig,
        freeze_act      => freeze_act );


    i_cart_logic: entity work.all_carts_v4
    generic map (
        g_rom_base      => std_logic_vector(g_rom_base_cart),
        g_ram_base      => std_logic_vector(g_ram_base_cart) )
    port map (
        clock           => clock,
        reset           => reset,
        RST_in          => reset_button,
        c64_reset       => control.c64_reset,

        ethernet_enable => control.eth_enable,
        freeze_trig     => freeze_trig,
        freeze_act      => freeze_act, 
        unfreeze        => unfreeze,
        
        cart_logic      => control.cartridge_type,
        cart_kill       => control.cartridge_kill,
        epyx_timeout    => epyx_timeout,

        slot_req        => slot_req,
        slot_resp       => slot_resp_cart,

        mem_addr        => mem_req_slot.address, 
        serve_enable    => serve_enable,
        serve_vic       => serve_vic,
        serve_rom       => serve_rom, -- ROML or ROMH
        serve_io1       => serve_io1, -- IO1n
        serve_io2       => serve_io2, -- IO2n
        allow_write     => allow_write,

        irq_n           => irq_n,
        nmi_n           => nmi_n,
        exrom_n         => exrom_n,
        game_n          => game_n,
    
        CART_LEDn       => cart_led_n );


    r_sid: if g_implement_sid generate
    begin
--    i_trce: entity work.sid_trace
--    generic map (
--        g_mem_tag   => X"CE" )
--    port map (
--        clock       => clock,
--        reset       => actual_c64_reset,
--        
--        addr        => unsigned(slot_addr(6 downto 0)),
--        wren        => sid_write,
--        wdata       => io_wdata,
--    
--        phi2_tick   => phi2_tick_i,
--        
--        io_req      => io_req_trace,
--        io_resp     => io_resp_trace,
--    
--        mem_req     => mem_req_trace,
--        mem_resp    => mem_resp_trace );


        i_sid: entity work.sid_peripheral
        generic map (
            g_num_voices  => g_sid_voices )
            
        port map (
            clock        => clock,
            reset        => reset,
            
            io_req       => io_req_sid,
            io_resp      => io_resp_sid,
            
            slot_req     => slot_req,
            slot_resp    => slot_resp_sid,
        
            start_iter   => phi2_tick_i,
            sample_left  => sid_sample_left,
            sample_right => sid_sample_right );

        i_pdm_sid_L: entity work.sigma_delta_dac --delta_sigma_2to5
        generic map (
            g_left_shift => 0,
            g_invert => true,
            g_use_mid_only => false,
            g_width => sid_sample_left'length )
        port map (
            clock   => clock,
            reset   => reset,
            
            dac_in  => sid_sample_left,
            dac_out => sid_pwm_left );
    
        i_pdm_sid_R: entity work.sigma_delta_dac --delta_sigma_2to5
        generic map (
            g_left_shift => 0,
            g_invert => true,
            g_use_mid_only => false,
            g_width => sid_sample_right'length )
        port map (
            clock   => clock,
            reset   => reset,
            
            dac_in  => sid_sample_right,
            dac_out => sid_pwm_right );

    end generate;
    
    g_cmd: if g_command_intf generate
        i_cmd: entity work.command_interface
        port map (
            clock           => clock,
            reset           => reset,
            
            -- C64 side interface
            slot_req        => slot_req,
            slot_resp       => slot_resp_cmd,
            
            -- io interface for local cpu
            io_req          => io_req_cmd, -- we get an 8K range
            io_resp         => io_resp_cmd );

    end generate;

    g_reu: if g_ram_expansion generate
    begin
        i_reu: entity work.reu
        generic map (
            g_extended      => g_extended_reu,
            g_ram_base      => g_ram_base_reu,
            g_ram_tag       => g_tag_reu )
        port map (
            clock           => clock,
            reset           => actual_c64_reset,
            
            -- register interface
            slot_req        => slot_req,
            slot_resp       => slot_resp_reu,
            
            -- system interface
            phi2_tick       => do_io_event,
            reu_dma_n       => reu_dma_n,
            size_ctrl       => control.reu_size,
            enable          => control.reu_enable,
            
            -- memory interface
            mem_req         => mem_req_reu,
            mem_resp        => mem_resp_reu,

            dma_req         => dma_req_reu,
            dma_resp        => dma_resp_reu );

    end generate;

    r_copper: if g_vic_copper generate
        i_copper: entity work.copper
        port map (
            clock       => clock,
            reset       => reset,
            
            irq_n       => IRQn,
            phi2_tick   => phi2_tick_i,
            
            trigger_1   => trigger_1,
            trigger_2   => trigger_2,

            io_req      => io_req_copper,
            io_resp     => io_resp_copper,
            
            dma_req     => dma_req_copper,
            dma_resp    => dma_resp_copper,
            
            slot_req    => slot_req,
            slot_resp   => open ); -- never required, just snoop!

    end generate;

    r_sampler: if g_sampler generate
        signal local_io_req     : t_io_req  := c_io_req_init;
        signal local_io_resp    : t_io_resp;
        signal io_req_samp      : t_io_req;
        signal io_resp_samp     : t_io_resp;
        signal irq_samp         : std_logic;
    begin
        i_io_bridge: entity work.slot_to_io_bridge
        generic map (
            g_io_base       => X"48000", -- dont care in this context
            g_slot_start    => "100100000",
            g_slot_stop     => "111111111" )
        port map (
            clock           => clock,
            reset           => reset,
            
            enable          => control.sampler_enable,
            irq_in          => irq_samp,
            
            slot_req        => slot_req,
            slot_resp       => slot_resp_samp,
            
            io_req          => local_io_req,
            io_resp         => local_io_resp );
        
        i_io_arb_sampler: entity work.io_bus_arbiter_pri
        generic map (
            g_ports     => 2 )
        port map (
            clock       => clock,
            reset       => reset,
            
            reqs(0)     => io_req_samp_cpu,
            reqs(1)     => local_io_req,
            
            resps(0)    => io_resp_samp_cpu,
            resps(1)    => local_io_resp,
            
            req         => io_req_samp,
            resp        => io_resp_samp );

        i_sampler: entity work.sampler
        generic map (
            g_num_voices    => 8 )
        port map (
            clock       => clock,
            reset       => actual_c64_reset,
            
            io_req      => io_req_samp,
            io_resp     => io_resp_samp,
            
            mem_req     => mem_req_samp,
            mem_resp    => mem_resp_samp,

            irq         => irq_samp,
            
            sample_L    => sample_L,
            sample_R    => sample_R,
            new_sample  => open );

        i_pdm_samp_L: entity work.sigma_delta_dac --delta_sigma_2to5
        generic map (
            g_left_shift => 0,
            g_invert => true,
            g_use_mid_only => false,
            g_width => 18 )
        port map (
            clock   => clock,
            reset   => reset,
            
            dac_in  => sample_L,
            dac_out => samp_pwm_left );
    
        i_pdm_samp_R: entity work.sigma_delta_dac --delta_sigma_2to5
        generic map (
            g_left_shift => 0,
            g_invert => true,
            g_use_mid_only => false,
            g_width => 18 )
        port map (
            clock   => clock,
            reset   => reset,
            
            dac_in  => sample_R,
            dac_out => samp_pwm_right );

    end generate;

    slot_resp <= or_reduce(slot_resp_reu & slot_resp_cart & slot_resp_sid & slot_resp_cmd & slot_resp_samp);

    ADDRESS(15 downto 8) <= address_out(15 downto 8) when address_tri_h='1' else (others => 'Z');
    ADDRESS(7 downto 0)  <= address_out(7 downto 0)  when address_tri_l='1' else (others => 'Z');

    RWn  <= rwn_out when rwn_tri='1' else 'Z';

    DATA <= slave_dout when (slave_dtri='1') else
            master_dout when (master_dtri='1') else (others => 'Z');

    -- open drain outputs
    IRQn     <= '0' when irq_n='0' or slot_resp.irq='1' else 'Z';
    NMIn     <= '0' when (control.c64_nmi='1')   or (nmi_n='0') else 'Z';
    EXROMn   <= '0' when (control.c64_exrom='1') or (serve_enable='1' and exrom_n='0') else 'Z';
    GAMEn    <= '0' when (control.c64_game='1')  or (serve_enable='1' and game_n='0') else 'Z';
    RSTn     <= '0' when (reset_button='1' and status.c64_stopped='0' and mask_buttons='0') or
                         (control.c64_reset='1') else 'Z';


    -- arbitration
    i_dma_arb: entity work.dma_bus_arbiter_pri
    generic map (
        g_ports     => 3 )
    port map (
        clock       => clock,
        reset       => reset,
        
        reqs(0)     => dma_req_io,
        reqs(1)     => dma_req_reu,
        reqs(2)     => dma_req_copper,
        
        resps(0)    => dma_resp_io,
        resps(1)    => dma_resp_reu,
        resps(2)    => dma_resp_copper,
        
        req         => dma_req,
        resp        => dma_resp );
    
    i_mem_arb: entity work.mem_bus_arbiter_pri
    generic map (
        g_ports     => 3 )
    port map (
        clock       => clock,
        reset       => reset,
        
        reqs(0)     => mem_req_slot,
        reqs(1)     => mem_req_reu,
        reqs(2)     => mem_req_samp,
        
--        reqs(3)     => mem_req_trace,
        resps(0)    => mem_resp_slot,
        resps(1)    => mem_resp_reu,
        resps(2)    => mem_resp_samp,
--        resps(3)    => mem_resp_trace,
        
        req         => mem_req,
        resp        => mem_resp );

    -- Delay the inhibit one clock cycle, because our
    -- arbited introduces one clock cycle delay as well.
    process(clock)
    begin
        if rising_edge(clock) then
            memctrl_inhibit <= timing_inhibit;            
        end if;
    end process;

    phi2_tick   <= phi2_tick_i;
	c64_stopped <= status.c64_stopped;
end structural;
