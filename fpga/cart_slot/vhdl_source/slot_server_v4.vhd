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
    g_clock_freq    : natural := 50_000_000;
    g_tag_slot      : std_logic_vector(7 downto 0) := X"08";
    g_tag_reu       : std_logic_vector(7 downto 0) := X"10";
    g_ram_base_reu  : std_logic_vector(27 downto 0) := X"1000000"; -- should be on 16M boundary, or should be limited in size
    g_ram_base_cart : std_logic_vector(27 downto 0) := X"0EF0000"; -- should be on a 64K boundary
    g_rom_base_cart : std_logic_vector(27 downto 0) := X"0F00000"; -- should be on a 1M boundary
    g_kernal_base   : std_logic_vector(27 downto 0) := X"0EA8000"; -- should be on a 32K boundary 
    g_register_addr : boolean := false;
    g_timing_meas   : boolean := false;
    g_direct_dma    : boolean := false;
    g_ext_freeze_act: boolean := false;
    g_cartreset_init: std_logic := '0';
    g_boot_stop     : boolean := false;
    g_big_endian    : boolean := false;
    g_kernal_repl   : boolean := true;
    g_control_read  : boolean := true;
    g_command_intf  : boolean := true;
    g_ram_expansion : boolean := true;
    g_extended_reu  : boolean := false;
    g_sampler       : boolean := false;
    g_sampler_voices: natural := 8;
    g_sampler_16bit : boolean := true;
    g_acia          : boolean := false;
    g_eeprom        : boolean := true;
    g_implement_sid : boolean := true;
    g_sid_voices    : natural := 3;
    g_8voices       : boolean := false;
    g_measure_timing: boolean := false;
    g_vic_copper    : boolean := false );

port (
    clock           : in  std_logic;
    reset           : in  std_logic;

    -- Cartridge pins
    VCCDET          : in    std_logic := '1';

    dotclk_i        : in    std_logic;
    phi2_i          : in    std_logic;
    io1n_i          : in    std_logic;
    io2n_i          : in    std_logic;
    romln_i         : in    std_logic;
    romhn_i         : in    std_logic;

    dman_o          : out   std_logic := '1';
    ba_i            : in    std_logic := '0';
    rstn_i          : in    std_logic := '1';
    rstn_o          : out   std_logic := '1';

    slot_addr_o     : out   unsigned(15 downto 0);
    slot_addr_i     : in    unsigned(15 downto 0) := (others => '1');
    slot_addr_tl    : out   std_logic;
    slot_addr_th    : out   std_logic;
                    
    slot_data_o     : out   std_logic_vector(7 downto 0);
    slot_data_i     : in    std_logic_vector(7 downto 0) := (others => '1');
    slot_data_t     : out   std_logic;
                    
    rwn_o           : out   std_logic;
    rwn_i           : in    std_logic;

    ultimax         : out   std_logic;
    exromn_i        : in    std_logic := '1';
    exromn_o        : out   std_logic;
    gamen_i         : in    std_logic := '1';
    gamen_o         : out   std_logic;
                    
    irqn_i          : in    std_logic := '1';
    irqn_o          : out   std_logic;
    nmin_i          : in    std_logic := '1';
    nmin_o          : out   std_logic;

    -- other hardware pins
    BUFFER_ENn      : out   std_logic;
    sense           : in    std_logic;

    buttons         : in    std_logic_vector(2 downto 0);
    btn_freeze      : in    std_logic;
    btn_reset       : in    std_logic;
    cart_led_n      : out   std_logic;

    trigger_1       : out   std_logic;
    trigger_2       : out   std_logic;

    -- debug / freezer
    freeze_activate : in    std_logic := '0';
    freezer_state   : out   std_logic_vector(1 downto 0);
    sync            : out   std_logic;
    sw_trigger      : out   std_logic;
    debug_data      : out   std_logic_vector(31 downto 0);
    debug_valid     : out   std_logic;
    debug_select    : in    std_logic_vector(2 downto 0) := "000";

    -- audio output
    sid_left         : out signed(17 downto 0);
    sid_right        : out signed(17 downto 0);
    samp_left        : out signed(17 downto 0);
    samp_right       : out signed(17 downto 0);

    -- timing input / output
    tick_4MHz       : in    std_logic;
    phi2_tick       : out   std_logic;
    c64_stopped     : out   std_logic;

    -- master on memory bus
    mem_refr_inhibit: out   std_logic;
    mem_reqs_inhibit: out   std_logic;
    mem_req         : out   t_mem_req_32;
    mem_resp        : in    t_mem_resp_32;

    direct_dma_req  : out   t_dma_req := c_dma_req_init;
    direct_dma_resp : in    t_dma_resp := c_dma_resp_init;

    -- slave on io bus
    io_req          : in    t_io_req;
    io_resp         : out   t_io_resp;
    io_irq_cmd      : out   std_logic;
    io_irq_acia     : out   std_logic );

end slot_server_v4;    

architecture structural of slot_server_v4 is
    -- Synchronized input signals
    signal dotclk_c        : std_logic;
    signal phi2_c          : std_logic;
    signal io1n_c          : std_logic;
    signal io2n_c          : std_logic;
    signal romln_c         : std_logic;
    signal romhn_c         : std_logic;
    signal ba_c            : std_logic := '0';
    signal rstn_c          : std_logic := '1';
    signal slot_addr_c     : unsigned(15 downto 0) := (others => '1');
    signal slot_data_c     : std_logic_vector(7 downto 0) := (others => '1');
    signal rwn_c           : std_logic;
    signal exromn_c        : std_logic := '1';
    signal gamen_c         : std_logic := '1';
    signal irqn_c          : std_logic := '1';
    signal nmin_c          : std_logic := '1';

    -- Xilinx attributes
    attribute register_duplication : string;
    attribute register_duplication of dotclk_c    : signal is "no";
    attribute register_duplication of phi2_c      : signal is "no";
    attribute register_duplication of io1n_c      : signal is "no";
    attribute register_duplication of io2n_c      : signal is "no";
    attribute register_duplication of romln_c     : signal is "no";
    attribute register_duplication of romhn_c     : signal is "no";
    attribute register_duplication of ba_c        : signal is "no";
    attribute register_duplication of rwn_c       : signal is "no";
    attribute register_duplication of rstn_c      : signal is "no";
    attribute register_duplication of slot_addr_c : signal is "no";
    attribute register_duplication of slot_data_c : signal is "no";
    attribute register_duplication of exromn_c    : signal is "no";
    attribute register_duplication of gamen_c     : signal is "no";
    attribute register_duplication of irqn_c      : signal is "no";
    attribute register_duplication of nmin_c      : signal is "no";

    -- Lattice attributes
    attribute syn_replicate                     : boolean;
    attribute syn_replicate of dotclk_c         : signal is false;
    attribute syn_replicate of phi2_c           : signal is false;
    attribute syn_replicate of io1n_c           : signal is false;
    attribute syn_replicate of io2n_c           : signal is false;
    attribute syn_replicate of romln_c          : signal is false;
    attribute syn_replicate of romhn_c          : signal is false;
    attribute syn_replicate of ba_c             : signal is false;
    attribute syn_replicate of rwn_c            : signal is false;
    attribute syn_replicate of rstn_c           : signal is false;
    attribute syn_replicate of slot_addr_c      : signal is false;
    attribute syn_replicate of slot_data_c      : signal is false;
    attribute syn_replicate of exromn_c         : signal is false;
    attribute syn_replicate of gamen_c          : signal is false;
    attribute syn_replicate of irqn_c           : signal is false;
    attribute syn_replicate of nmin_c           : signal is false;

    -- Altera attributes
    attribute dont_replicate                    : boolean;
    attribute dont_replicate of dotclk_c        : signal is true;
    attribute dont_replicate of phi2_c          : signal is true;
    attribute dont_replicate of io1n_c          : signal is true;
    attribute dont_replicate of io2n_c          : signal is true;
    attribute dont_replicate of romln_c         : signal is true;
    attribute dont_replicate of romhn_c         : signal is true;
    attribute dont_replicate of ba_c            : signal is true;
    attribute dont_replicate of rwn_c           : signal is true;
    attribute dont_replicate of rstn_c          : signal is true;
    attribute dont_replicate of slot_addr_c     : signal is true;
    attribute dont_replicate of slot_data_c     : signal is true;
    attribute dont_replicate of exromn_c        : signal is true;
    attribute dont_replicate of gamen_c         : signal is true;
    attribute dont_replicate of irqn_c          : signal is true;
    attribute dont_replicate of nmin_c          : signal is true;

    signal phi2_tick_i     : std_logic;
    signal phi2_fall       : std_logic;
    signal phi2_recovered  : std_logic;
    signal prepare_dma     : std_logic;
    signal vic_cycle       : std_logic;
    signal dma_data_out    : std_logic;
    signal do_sample_addr  : std_logic;
    signal do_sample_io    : std_logic;
    signal do_io_event     : std_logic;
    signal do_probe_end    : std_logic;
    signal reqs_inhibit    : std_logic;
    signal clear_inhibit   : std_logic;
    signal slave_dout      : std_logic_vector(7 downto 0);
    signal slave_dtri      : std_logic := '0';

    signal master_dout     : std_logic_vector(7 downto 0);
    signal master_dtri     : std_logic := '0';

    signal address_tri_l   : std_logic;
    signal address_tri_h   : std_logic;
    signal address_out     : std_logic_vector(15 downto 0);

    signal rwn_out         : std_logic;

    signal control         : t_cart_control;
    signal status          : t_cart_status := (others => '0');

    signal allow_serve     : std_logic;

    -- interface with freezer (cartridge) logic
    signal serve_enable    : std_logic := '0'; -- from cartridge emulation logic
    signal serve_inhibit   : std_logic := '0';
    signal serve_vic       : std_logic := '0';
    signal serve_vic_f     : std_logic := '0';
    signal serve_128       : std_logic := '0';
    signal serve_rom       : std_logic := '0'; -- ROML or ROMH
    signal serve_io1       : std_logic := '0'; -- IO1n
    signal serve_io2       : std_logic := '0'; -- IO2n
    signal allow_write     : std_logic := '0';

    -- timing measurement
    signal measure_data    : std_logic_vector(7 downto 0) := X"FF";
    signal timing_data     : std_logic_vector(31 downto 0);
    signal timing_trigger  : std_logic;

    -- kernal replacement logic
    signal kernal_area     : std_logic := '0';
    signal kernal_probe    : std_logic := '0';
    signal kernal_addr_out : std_logic := '0';
    signal force_ultimax   : std_logic := '0';

    signal cpu_write       : std_logic;
    signal epyx_timeout    : std_logic;

    signal reu_dma_n       : std_logic := '1'; -- direct from REC
    signal cmd_if_freeze    : std_logic := '0'; -- same function as reu_dma_n, but then from CI

    signal reset_button     : std_logic;
    signal freeze_button    : std_logic;

    signal actual_c64_reset : std_logic;
    
    signal dma_n            : std_logic := '1';
    signal nmi_n            : std_logic := '1';
    signal irq_n            : std_logic := '1';
    signal exrom_n          : std_logic := '1';
    signal game_n           : std_logic := '1';

    signal freezer_ena      : std_logic;
    signal unfreeze         : std_logic;
    signal freeze_trig      : std_logic;
    signal freeze_active    : std_logic;

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
    signal io_req_acia      : t_io_req;
    signal io_resp_acia     : t_io_resp := c_io_resp_init;
    signal io_req_eeprom    : t_io_req;
    signal io_resp_eeprom   : t_io_resp := c_io_resp_init;
    
    signal dma_req_io       : t_dma_req;
    signal dma_resp_io      : t_dma_resp := c_dma_resp_init;
    signal dma_req_reu      : t_dma_req;
    signal dma_resp_reu     : t_dma_resp := c_dma_resp_init;
    signal dma_req_copper   : t_dma_req;
    signal dma_resp_copper  : t_dma_resp := c_dma_resp_init;
    signal dma_req          : t_dma_req;
    signal dma_resp         : t_dma_resp := c_dma_resp_init;

    signal write_ff00       : std_logic;
    signal slot_req         : t_slot_req;
    signal slot_resp        : t_slot_resp := c_slot_resp_init;
    signal slot_resp_reu    : t_slot_resp := c_slot_resp_init;
    signal slot_resp_cart   : t_slot_resp := c_slot_resp_init;
    signal slot_resp_sid    : t_slot_resp := c_slot_resp_init;
    signal slot_resp_cmd    : t_slot_resp := c_slot_resp_init;
    signal slot_resp_samp   : t_slot_resp := c_slot_resp_init;
    signal slot_resp_acia   : t_slot_resp := c_slot_resp_init;
    
    signal mem_req_reu      : t_mem_req   := c_mem_req_init; 
    signal mem_resp_reu     : t_mem_resp  := c_mem_resp_init;
    signal mem_req_samp     : t_mem_req   := c_mem_req_init;
    signal mem_resp_samp    : t_mem_resp  := c_mem_resp_init;

    signal mem_req_32_slot  : t_mem_req_32  := c_mem_req_32_init; 
    signal mem_resp_32_slot : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_reu   : t_mem_req_32  := c_mem_req_32_init; 
    signal mem_resp_32_reu  : t_mem_resp_32 := c_mem_resp_32_init;
    signal mem_req_32_samp  : t_mem_req_32  := c_mem_req_32_init;
    signal mem_resp_32_samp : t_mem_resp_32 := c_mem_resp_32_init;
    
    signal mem_rack_slot    : std_logic;
    signal mem_dack_slot    : std_logic;

    signal phi2_tick_avail  : std_logic;
begin
    b_sync: block
        signal dotclk_f        : std_logic;
        signal phi2_f          : std_logic;
        signal io1n_f          : std_logic;
        signal io2n_f          : std_logic;
        signal romln_f         : std_logic;
        signal romhn_f         : std_logic;
        signal ba_f            : std_logic := '0';
        signal rstn_f          : std_logic := '1';
        signal slot_addr_f     : unsigned(15 downto 0) := (others => '1');
        signal slot_data_f     : std_logic_vector(7 downto 0) := (others => '1');
        signal rwn_f           : std_logic;
        signal exromn_f        : std_logic := '1';
        signal gamen_f         : std_logic := '1';
        signal irqn_f          : std_logic := '1';
        signal nmin_f          : std_logic := '1';

        -- Xilinx attributes
        attribute register_duplication of dotclk_f    : signal is "no";
        attribute register_duplication of phi2_f      : signal is "no";
        attribute register_duplication of io1n_f      : signal is "no";
        attribute register_duplication of io2n_f      : signal is "no";
        attribute register_duplication of romln_f     : signal is "no";
        attribute register_duplication of romhn_f     : signal is "no";
        attribute register_duplication of ba_f        : signal is "no";
        attribute register_duplication of rwn_f       : signal is "no";
        attribute register_duplication of rstn_f      : signal is "no";
        attribute register_duplication of slot_addr_f : signal is "no";
        attribute register_duplication of slot_data_f : signal is "no";
        attribute register_duplication of exromn_f    : signal is "no";
        attribute register_duplication of gamen_f     : signal is "no";
        attribute register_duplication of irqn_f      : signal is "no";
        attribute register_duplication of nmin_f      : signal is "no";

        -- Lattice attributes
        attribute syn_replicate of dotclk_f         : signal is false;
        attribute syn_replicate of phi2_f           : signal is false;
        attribute syn_replicate of io1n_f           : signal is false;
        attribute syn_replicate of io2n_f           : signal is false;
        attribute syn_replicate of romln_f          : signal is false;
        attribute syn_replicate of romhn_f          : signal is false;
        attribute syn_replicate of ba_f             : signal is false;
        attribute syn_replicate of rwn_f            : signal is false;
        attribute syn_replicate of rstn_f           : signal is false;
        attribute syn_replicate of slot_addr_f      : signal is false;
        attribute syn_replicate of slot_data_f      : signal is false;
        attribute syn_replicate of exromn_f         : signal is false;
        attribute syn_replicate of gamen_f          : signal is false;
        attribute syn_replicate of irqn_f           : signal is false;
        attribute syn_replicate of nmin_f           : signal is false;

        -- Altera attributes
        attribute dont_replicate of dotclk_f        : signal is true;
        attribute dont_replicate of phi2_f          : signal is true;
        attribute dont_replicate of io1n_f          : signal is true;
        attribute dont_replicate of io2n_f          : signal is true;
        attribute dont_replicate of romln_f         : signal is true;
        attribute dont_replicate of romhn_f         : signal is true;
        attribute dont_replicate of ba_f            : signal is true;
        attribute dont_replicate of rwn_f           : signal is true;
        attribute dont_replicate of rstn_f          : signal is true;
        attribute dont_replicate of slot_addr_f     : signal is true;
        attribute dont_replicate of slot_data_f     : signal is true;
        attribute dont_replicate of exromn_f        : signal is true;
        attribute dont_replicate of gamen_f         : signal is true;
        attribute dont_replicate of irqn_f          : signal is true;
        attribute dont_replicate of nmin_f          : signal is true;

    begin
        process(clock)
        begin
            if falling_edge(clock) then
                dotclk_f    <= dotclk_i;
                phi2_f      <= phi2_i;
                io1n_f      <= io1n_i;
                io2n_f      <= io2n_i;
                romln_f     <= romln_i;
                romhn_f     <= romhn_i;
                ba_f        <= ba_i;
                rstn_f      <= rstn_i;
                slot_addr_f <= slot_addr_i;
                slot_data_f <= slot_data_i;
                rwn_f       <= rwn_i;
                exromn_f    <= exromn_i;
                gamen_f     <= gamen_i;
                irqn_f      <= irqn_i;
                nmin_f      <= nmin_i;
            end if;

            if rising_edge(clock) then
                dotclk_c    <= dotclk_f;
                phi2_c      <= phi2_f;
                io1n_c      <= io1n_f;
                io2n_c      <= io2n_f;
                romln_c     <= romln_f;
                romhn_c     <= romhn_f;
                ba_c        <= ba_f;
                rstn_c      <= rstn_f;
                slot_addr_c <= slot_addr_f;
                slot_data_c <= slot_data_f;
                rwn_c       <= rwn_f;
                exromn_c    <= exromn_f;
                gamen_c     <= gamen_f;
                irqn_c      <= irqn_f;
                nmin_c      <= nmin_f;
            end if;
        end process;
    end block b_sync;

    reset_button  <= btn_reset or  (buttons(0) and not control.swap_buttons) or (buttons(2) and control.swap_buttons);
    freeze_button <= btn_freeze or (buttons(2) and not control.swap_buttons) or (buttons(0) and control.swap_buttons);

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
    generic map (
        g_ignore_stop => true )
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
        g_ports     => 7 )
    port map (
        clock    => clock,
        
        req      => io_req_peri,
        resp     => io_resp_peri,
           
        reqs(0)  => io_req_regs,     -- 4040000
        reqs(1)  => io_req_sid,      -- 4042000
        reqs(2)  => io_req_cmd,      -- 4044000
        reqs(3)  => io_req_copper,   -- 4046000
        reqs(4)  => io_req_samp_cpu, -- 4048000
        reqs(5)  => io_req_acia,     -- 404A000
        reqs(6)  => io_req_eeprom,   -- 404C000
        
        resps(0) => io_resp_regs,
        resps(1) => io_resp_sid,
        resps(2) => io_resp_cmd,
        resps(3) => io_resp_copper,
        resps(4) => io_resp_samp_cpu,
        resps(5) => io_resp_acia,
        resps(6) => io_resp_eeprom );


    i_registers: entity work.cart_slot_registers
    generic map (
        g_timing_meas   => g_timing_meas,
        g_kernal_repl   => g_kernal_repl,
        g_boot_stop     => g_boot_stop,
        g_cartreset_init=> g_cartreset_init,
        g_ram_expansion => g_ram_expansion )
    port map (
        clock           => clock,
        reset           => reset,
        
        io_req          => io_req_regs,
        io_resp         => io_resp_regs,

        control         => control,
        status          => status );
        

    serve_inhibit <= status.c64_stopped and not control.serve_while_stopped;
    serve_vic_f   <= serve_vic or control.force_serve_vic;

    i_timing: entity work.slot_timing
    generic map (
        g_frequency     => g_clock_freq
    )
    port map (
        clock           => clock,
        reset           => reset,

        -- Cartridge pins
        PHI2            => phi2_c,
        BA              => ba_c,
    
        serve_vic       => serve_vic_f,
        serve_enable    => serve_enable,
        serve_inhibit   => serve_inhibit,
        allow_serve     => allow_serve,

        timing_phi1     => control.timing_addr_phi1,
        timing_phi2     => control.timing_addr_phi2,
        edge_recover    => control.phi2_edge_recover,
    
        phi2_tick       => phi2_tick_i,
        phi2_fall       => phi2_fall,
        phi2_recovered  => phi2_recovered,
        dma_data_out    => dma_data_out,
        clock_det       => status.clock_detect,
        vic_cycle       => vic_cycle,    
        prepare_dma     => prepare_dma,

        refr_inhibit    => mem_refr_inhibit,
        reqs_inhibit    => reqs_inhibit,
        clear_inhibit   => clear_inhibit,
        
        do_sample_addr  => do_sample_addr,
        do_sample_io    => do_sample_io,
        do_io_event     => do_io_event );

    mem_reqs_inhibit <= reqs_inhibit;
    mem_req_32_slot.tag <= g_tag_slot;
    mem_req_32_slot.byte_en <= "1000" when g_big_endian else "0001";
    mem_rack_slot <= '1' when mem_resp_32_slot.rack_tag = g_tag_slot else '0';
    mem_dack_slot <= '1' when mem_resp_32_slot.dack_tag = g_tag_slot else '0';

    timing_data(31) <= dotclk_c;
    timing_data(30) <= phi2_c;
    timing_data(29) <= phi2_recovered;
    timing_data(28) <= dma_data_out;
    timing_data(27) <= slave_dtri or master_dtri;
    timing_data(26) <= address_tri_l; 
    timing_data(25) <= address_tri_h;
    timing_data(24) <= rwn_c;
    timing_data(23 downto 16) <= slot_data_c;
    timing_data(15 downto 0)  <= std_logic_vector(slot_addr_c);

    i_slave: entity work.slot_slave
    generic map (
        g_big_endian => g_big_endian )
    port map (
        clock           => clock,
        reset           => reset,
        
        -- Cartridge pins
        VCCDET          => VCCDET,
        PHI2            => phi2_c,
        RSTn            => rstn_c,
        IO1n            => io1n_c,
        IO2n            => io2n_c,
        ROMLn           => romln_c,
        ROMHn           => romhn_c,
        GAMEn           => gamen_c,
        EXROMn          => exromn_c,
        RWn             => rwn_c,
        BA              => ba_c,
        ADDRESS         => slot_addr_c,
        DATA_in         => slot_data_c,
        DATA_out        => slave_dout,
        DATA_tri        => slave_dtri,
    
        -- interface with memory controller
        mem_req         => mem_req_32_slot.request,
        mem_rwn         => mem_req_32_slot.read_writen,
        mem_wdata       => mem_req_32_slot.data,
        mem_rack        => mem_rack_slot,
        mem_dack        => mem_dack_slot,
        mem_rdata       => mem_resp_32_slot.data,
        -- mem_addr comes from cartridge logic
    
        -- synchronized outputs
        reset_out       => actual_c64_reset,
        
        -- timing inputs
        phi2_tick       => phi2_tick_i,
        do_sample_addr  => do_sample_addr,
        do_sample_io    => do_sample_io,
        do_io_event     => do_io_event,
        do_probe_end    => do_probe_end,
        dma_active_n    => dma_n,  -- required to stop epyx cap to discharge (!)
    
        -- interface with freezer (cartridge) logic
        allow_serve     => allow_serve,
        serve_128       => serve_128, -- 8000-FFFF
        serve_rom       => serve_rom, -- ROML or ROMH
        serve_io1       => serve_io1, -- IO1n
        serve_io2       => serve_io2, -- IO2n
        allow_write     => allow_write,
        clear_inhibit   => clear_inhibit,

        -- timing measurement
        measure         => control.measure_enable,
        measure_data    => measure_data,

        -- kernal emulation
        kernal_enable   => control.kernal_enable,
        kernal_shadow   => control.kernal_shadow,
        kernal_probe    => kernal_probe,
        kernal_area     => kernal_area,
        force_ultimax   => force_ultimax,
    
        cpu_write       => cpu_write,
        epyx_timeout    => epyx_timeout,
        
        slot_req        => slot_req,
        slot_resp       => slot_resp,
        
        -- interface with hardware
        BUFFER_ENn      => BUFFER_ENn );

    -- r_measure: if g_timing_meas generate
    --     i_slot_measure: entity work.slot_measure
    --         port map (
    --             clock    => clock,
    --             reset    => reset,
    --             phi2     => phi2_c,
    --             addr     => slot_addr_c,
    --             data_out => measure_data
    --         );
    -- end generate;

    r_master: if not g_direct_dma generate
        i_master: entity work.slot_master_v4
        generic map (
            g_start_in_stopped_state => g_boot_stop )
        
        port map (
            clock           => clock,
            reset           => reset,
            
            -- Cartridge pins
            DMAn            => dma_n,
            BA              => ba_c,
            RWn_in          => rwn_c,
            RWn_out         => rwn_o,
            RWn_tri         => open,
            
            ADDRESS_out     => address_out,
            ADDRESS_tri_h   => address_tri_h,
            ADDRESS_tri_l   => address_tri_l,
            
            DATA_in         => slot_data_c,
            DATA_out        => master_dout,
            DATA_tri        => master_dtri,
        
            -- timing inputs
            vic_cycle       => vic_cycle,    
            dma_data_out    => dma_data_out,
            prepare_dma     => prepare_dma,
            phi2_recovered  => phi2_recovered,
            phi2_tick       => phi2_tick_i,
            do_sample_addr  => do_sample_addr,
            do_io_event     => do_io_event,
            reu_dma_n       => reu_dma_n,
            cmd_if_freeze   => cmd_if_freeze,
            
            -- request from the cpu to do a cycle on the cart bus
            dma_req         => dma_req,
            dma_resp        => dma_resp,
        
            -- system control
            stop_cond       => control.c64_stop_mode,
            c64_stop        => control.c64_stop,
            c64_stopped     => status.c64_stopped );

        direct_dma_req <= c_dma_req_init;

    end generate;    

    r_no_master: if g_direct_dma generate
        process(clock)
        begin
            if rising_edge(clock) then
                if phi2_fall = '1' then
                    status.c64_stopped <= control.c64_stop;
                end if;
            end if;
        end process;

        dma_n              <= not (status.c64_stopped or not reu_dma_n or cmd_if_freeze);
        RWN_out            <= '1';
        ADDRESS_out        <= (others => '1');
        ADDRESS_tri_h      <= '0';
        ADDRESS_tri_l      <= '0';
        
        direct_dma_req <= dma_req;
        dma_resp       <= direct_dma_resp;        
    end generate;

    i_freeze: entity work.freezer
    generic map (
        g_ext_activate  => g_ext_freeze_act )
    port map (
        clock           => clock,
        reset           => reset,

        RST_in          => reset_button,
        button_freeze   => freeze_button,
    
        cpu_cycle_done  => do_io_event,
        cpu_write       => cpu_write,
        activate        => freeze_activate, 

        freezer_state   => freezer_state,

        unfreeze        => unfreeze,
        freezer_ena     => freezer_ena,
        freeze_trig     => freeze_trig,
        freeze_act      => freeze_active );


    i_cart_logic: entity work.all_carts_v5
    generic map (
        g_register_addr => g_register_addr,
        g_eeprom        => g_eeprom,
        g_kernal_base   => g_kernal_base,
        g_georam_base   => g_ram_base_reu,
        g_rom_base      => g_rom_base_cart,
        g_ram_base      => g_ram_base_cart )
    port map (
        clock           => clock,
        reset           => reset,
        RST_in          => reset_button,
        c64_reset       => control.c64_reset,

        freezer_ena     => freezer_ena,
        freeze_trig     => freeze_trig,
        freeze_act      => freeze_active, 
        unfreeze        => unfreeze,
        cart_active     => status.cart_active,
        
        cart_logic      => control.cartridge_type,
        cart_variant    => control.cartridge_variant,
        cart_force      => control.cartridge_force,
        cart_kill       => control.cartridge_kill,
        epyx_timeout    => epyx_timeout,

        slot_req        => slot_req,
        slot_resp       => slot_resp_cart,

        io_req_eeprom   => io_req_eeprom,
        io_resp_eeprom  => io_resp_eeprom,

        mem_req         => mem_req_32_slot.request,
        mem_addr        => mem_req_32_slot.address, 
        serve_enable    => serve_enable,
        serve_vic       => serve_vic,
        serve_128       => serve_128, -- 8000-FFFF
        serve_rom       => serve_rom, -- ROML or ROMH
        serve_io1       => serve_io1, -- IO1n
        serve_io2       => serve_io2, -- IO2n
        allow_write     => allow_write,
        kernal_area     => kernal_area,
        kernal_enable   => control.kernal_enable,
        
        phi2            => phi2_c,
        irq_n           => irq_n,
        nmi_n           => nmi_n,
        exrom_n         => exrom_n,
        game_n          => game_n,

        CART_LEDn       => cart_led_n,
        size_ctrl       => control.reu_size );


    r_sid: if g_implement_sid generate
    begin
        i_sid: entity work.sid_peripheral
        generic map (
            g_8voices     => g_8voices,
            g_num_voices  => g_sid_voices )
            
        port map (
            clock        => clock,
            reset        => reset,
            
            io_req       => io_req_sid,
            io_resp      => io_resp_sid,
            
            slot_req     => slot_req,
            slot_resp    => slot_resp_sid,
        
            start_iter   => phi2_tick_avail,
            sample_left  => sid_left,
            sample_right => sid_right );

    end generate;
    
    r_no_sid: if not g_implement_sid generate
        i_io_dummy: entity work.io_dummy
            port map (
                clock   => clock,
                io_req  => io_req_sid,
                io_resp => io_resp_sid
            );
    end generate;

    g_cmd: if g_command_intf generate
        i_cmd: entity work.command_interface
        port map (
            clock           => clock,
            reset           => reset,
            
            -- C64 side interface
            slot_req        => slot_req,
            slot_resp       => slot_resp_cmd,
            freeze          => cmd_if_freeze,
            write_ff00      => write_ff00,
            
            -- io interface for local cpu
            io_req          => io_req_cmd, -- we get an 8K range
            io_resp         => io_resp_cmd,
            io_irq          => io_irq_cmd );

    end generate;

    write_ff00 <= '1' when slot_req.late_write='1' and slot_req.io_address=X"FF00" else '0';

    g_reu: if g_ram_expansion generate
    begin
        i_reu: entity work.reu
        generic map (
            g_extended      => g_extended_reu,
            g_ram_base      => unsigned(g_ram_base_reu),
            g_ram_tag       => g_tag_reu )
        port map (
            clock           => clock,
            reset           => actual_c64_reset,
            
            -- register interface
            slot_req        => slot_req,
            slot_resp       => slot_resp_reu,
            write_ff00      => write_ff00,

            -- system interface
            phi2_tick       => do_io_event,
            reu_dma_n       => reu_dma_n,
            inhibit         => status.c64_stopped,
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
            
            irq_n       => irqn_c,
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

    r_timing_measure: if g_measure_timing generate
        i_trace: entity work.slot_trace
        port map (
            clock   => clock,
            reset   => reset,
            data_in => timing_data,
            trigger => control.timing_trigger,
            io_req  => io_req_copper,
            io_resp => io_resp_copper
        );
    end generate;

    assert not (g_measure_timing and g_vic_copper)
        report "Timing measure module and copper cannot be enabled at the same time"
        severity failure;

    r_neither: if not g_measure_timing and not g_vic_copper generate
        io_dummy_inst: entity work.io_dummy
        port map (
            clock   => clock,
            io_req  => io_req_copper,
            io_resp => io_resp_copper
        );
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
            g_io_base       => X"048000", -- dont care in this context
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
            g_clock_freq    => g_clock_freq,
            g_support_16bit => g_sampler_16bit,
            g_num_voices    => g_sampler_voices )
        port map (
            clock       => clock,
            reset       => actual_c64_reset,
            
            io_req      => io_req_samp,
            io_resp     => io_resp_samp,
            
            mem_req     => mem_req_samp,
            mem_resp    => mem_resp_samp,

            irq         => irq_samp,
            
            sample_L    => samp_left,
            sample_R    => samp_right,
            new_sample  => open );

    end generate;

    r_no_sampler: if not g_sampler generate
        i_dummy: entity work.io_dummy
        port map (
            clock   => clock,
            io_req  => io_req_samp_cpu,
            io_resp => io_resp_samp_cpu
        );
    end generate;

    r_acia: if g_acia generate
        i_acia: entity work.acia6551
        port map (
            clock     => clock,
            reset     => reset,
            c64_reset => actual_c64_reset,
            tick      => tick_4MHz,
            slot_req  => slot_req,
            slot_resp => slot_resp_acia,
            io_req    => io_req_acia,
            io_resp   => io_resp_acia,
            io_irq    => io_irq_acia
        );
    end generate;

    r_no_acia: if not g_acia generate
        i_dummy: entity work.io_dummy
        port map (
            clock   => clock,
            io_req  => io_req_acia,
            io_resp => io_resp_acia
        );
        
    end generate;

    slot_resp <= or_reduce(slot_resp_reu & slot_resp_cart & slot_resp_sid & slot_resp_cmd &
                           slot_resp_samp & slot_resp_acia);

    p_probe_end_delay: process(clock)
        constant c_probe_time : natural := ((g_clock_freq + 5_000_000) / 10_000_000);
        variable probe_end_d : std_logic_vector(c_probe_time-1 downto 0) := (others => '0');
    begin
        if rising_edge(clock) then
            kernal_addr_out <= kernal_probe;
            do_probe_end <= probe_end_d(0);
            probe_end_d := (kernal_probe and not kernal_addr_out) & probe_end_d(probe_end_d'high downto 1);
        end if;
    end process;

    process(address_out, kernal_addr_out, kernal_probe, do_probe_end, address_tri_l, address_tri_h, slot_addr_c)
    begin
        slot_addr_o <= unsigned(address_out);
        slot_addr_tl <= address_tri_l;
        slot_addr_th <= address_tri_h;
        if kernal_addr_out='1' and kernal_probe='1' then
            slot_addr_o(12) <= slot_addr_c(12);
            slot_addr_o(15 downto 13) <= "101"; -- A000-BFFF
            slot_addr_o(14) <= do_probe_end;
            slot_addr_th <= '1';
        end if;
    end process;

    slot_data_o <= slave_dout when (slave_dtri='1') else
                   master_dout when (master_dtri='1') else
                   X"FF";     
    slot_data_t <= slave_dtri or master_dtri;            

    -- open drain outputs
    irqn_o  <= '0' when irq_n='0' or slot_resp.irq='1' else '1';
    nmin_o  <= '0' when (control.c64_nmi='1') or (nmi_n='0') or (slot_resp.nmi='1') else '1';
    rstn_o  <= '0' when (reset_button='1' and status.c64_stopped='0') or
                        (control.c64_reset='1') else '1';
    dman_o  <= '0' when (dma_n='0' or kernal_probe='1') else '1';
    
    process(control, status, exrom_n, game_n, force_ultimax, kernal_probe)
    begin
        exromn_o <= '1';
        gamen_o  <= '1';
        if (force_ultimax = '1') or (control.c64_ultimax = '1') then
            gamen_o <= '0';
        elsif kernal_probe = '1' then
            gamen_o <= '0';
            exromn_o <= '0';
        else
            if (status.cart_active='1' and exrom_n='0') then
                exromn_o <= '0';
            end if;
            if (status.cart_active='1' and game_n='0') then
                gamen_o <= '0';
            end if;
        end if;
    end process;
    
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

    
    i_conv32_reu: entity work.mem_to_mem32(route_through)
    generic map (
        g_big_endian => g_big_endian )
    port map(
        clock       => clock,
        reset       => reset,
        mem_req_8   => mem_req_reu,
        mem_resp_8  => mem_resp_reu,
        mem_req_32  => mem_req_32_reu,
        mem_resp_32 => mem_resp_32_reu );

    i_conv32_samp: entity work.mem_to_mem32(route_through)
    generic map (
        g_big_endian => g_big_endian )
    port map(
        clock       => clock,
        reset       => reset,
        mem_req_8   => mem_req_samp,
        mem_resp_8  => mem_resp_samp,
        mem_req_32  => mem_req_32_samp,
        mem_resp_32 => mem_resp_32_samp );

    i_mem_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_ports     => 3 )
    port map (
        clock       => clock,
        reset       => reset,
        
        inhibit     => reqs_inhibit,

        reqs(0)     => mem_req_32_slot,
        reqs(1)     => mem_req_32_reu,
        reqs(2)     => mem_req_32_samp,
        
        resps(0)    => mem_resp_32_slot,
        resps(1)    => mem_resp_32_reu,
        resps(2)    => mem_resp_32_samp,
        
        req         => mem_req,
        resp        => mem_resp );

    process(clock)
    begin
        if rising_edge(clock) then
            status.c64_vcc <= VCCDET;            
        end if;
    end process;
    status.exrom    <= not exromn_c;
    status.game     <= not gamen_c;
    status.reset_in <= not rstn_c;

    phi2_tick_avail <= phi2_tick_i;
    phi2_tick   <= phi2_tick_avail;
    
    c64_stopped <= status.c64_stopped;
    ultimax <= control.c64_ultimax;
    
    -- write 0x54 to $DFFE to generate a trigger
    sw_trigger  <= '1' when slot_req.io_write = '1' and slot_req.io_address(8 downto 0) = "111111110" and slot_req.data = X"54" else '0';
    -- write 0x0D to $DFFF to generate a sync
    sync        <= '1' when slot_req.io_write = '1' and slot_req.io_address(8 downto 0) = "111111111" and slot_req.data = X"0D" else '0';
    
    status.nmi  <= not nmin_i when rising_edge(clock);

    b_debug: block
        signal phi_d1       : std_logic := '0';
        signal ba_history   : std_logic_vector(2 downto 0) := (others => '0');
        alias cpu_cycle_enable  : std_logic is debug_select(0);
        alias vic_cycle_enable  : std_logic is debug_select(1);
        alias drv_enable        : std_logic is debug_select(2);
    begin 
        -- Debug Stream
        process(clock)
            variable vector_in      : std_logic_vector(31 downto 0);
        begin
            if rising_edge(clock) then
                vector_in := phi2_c & gamen_c & exromn_c & not (romhn_c and romln_c) &
                             ba_c & irqn_c & nmin_c & rwn_c &
                             slot_data_c & std_logic_vector(slot_addr_c);
    
                phi_d1 <= phi2_c;
                         
                -- BA  1 1 1 0 0 0 0 0 0 0 1 1 1
                -- BA0 1 1 1 1 0 0 0 0 0 0 0 1 1
                -- BA1 1 1 1 1 1 0 0 0 0 0 0 0 1
                -- BA2 1 1 1 1 1 1 0 0 0 0 0 0 0
                -- CPU 1 1 1 1 1 1 0 0 0 0 1 1 1 
                -- 
                debug_valid <= '0';
                debug_data  <= vector_in;    

                if phi_d1 /= phi2_c then
                    if phi_d1 = '1' then
                        ba_history <= ba_history(1 downto 0) & ba_c;
                    end if;
                    
                    if phi_d1 = '1' then
                        if (ba_c = '1' or ba_history /= "000" or drv_enable = '1') and cpu_cycle_enable = '1' then
                            debug_valid <= '1';
                        elsif vic_cycle_enable = '1' then
                            debug_valid <= '1';
                        end if;
                    elsif vic_cycle_enable = '1' then
                        debug_valid <= '1';
                    end if;
                end if;
            end if;
        end process;
    end block;
        
end structural;
