library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.dma_bus_pkg.all;
use work.cart_slot_pkg.all;

entity slot_server_v4 is
generic (
    g_tag_slot      : std_logic_vector(7 downto 0) := X"08";
    g_tag_reu       : std_logic_vector(7 downto 0) := X"10";
    g_ram_base_reu  : unsigned(27 downto 0) := X"1000000"; -- should be on 16M boundary, or should be limited in size
    g_ram_base_cart : unsigned(27 downto 0) := X"0F70000"; -- should be on a 64K boundary
    g_rom_base_cart : unsigned(27 downto 0) := X"0F80000"; -- should be on a 512K boundary
    g_control_read  : boolean := true;
    g_ram_expansion : boolean := true );
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
    signal do_reg_output   : std_logic := '0';
    signal cart_reg_output : std_logic;
    signal reu_reg_output  : std_logic;
    signal cart_reg_rdata  : std_logic_vector(7 downto 0);
    signal reu_reg_rdata   : std_logic_vector(7 downto 0);
    
    signal slot_addr       : std_logic_vector(15 downto 0);
    signal cpu_write       : std_logic;
    signal cpu_cycle_done  : std_logic;
    signal epyx_timeout    : std_logic;
    
    signal write_ff00      : std_logic;
    signal io_write        : std_logic;
    signal io_read         : std_logic;
    signal io_event_addr   : std_logic_vector(8 downto 0);
    signal io_rdata        : std_logic_vector(7 downto 0);
    signal io_wdata        : std_logic_vector(7 downto 0);
    signal reu_dma_n       : std_logic := '1'; -- direct from REC
    signal reu_irq         : std_logic;

    signal mask_buttons     : std_logic := '0';
    signal reset_button     : std_logic;
    signal freeze_button    : std_logic;
	
    signal c64_clock_detect : std_logic;
    signal c64_reset        : std_logic;
    signal c64_nmi          : std_logic;
    signal c64_cycle_done   : std_logic;
    signal actual_c64_reset : std_logic;
    
    signal nmi_n            : std_logic := '1';
    signal irq_n            : std_logic := '1';
    signal exrom_n          : std_logic := '1';
    signal game_n           : std_logic := '1';

    signal unfreeze         : std_logic;
    signal freeze_trig      : std_logic;
    signal freeze_act       : std_logic;

    signal dma_req_regs     : t_dma_req;
    signal dma_resp_regs    : t_dma_resp;
    signal dma_req_reu      : t_dma_req;
    signal dma_resp_reu     : t_dma_resp;
    signal dma_req          : t_dma_req;
    signal dma_resp         : t_dma_resp;

    signal mem_req_slot     : t_mem_req;
    signal mem_resp_slot    : t_mem_resp;
    signal mem_req_reu      : t_mem_req;
    signal mem_resp_reu     : t_mem_resp;

    signal mem_rack_slot    : std_logic;
    signal mem_dack_slot    : std_logic;
begin
    reset_button  <= buttons(0) when control.swap_buttons='0' else buttons(2);
    freeze_button <= buttons(2) when control.swap_buttons='0' else buttons(0);

    i_registers: entity work.cart_slot_registers
    generic map (
        g_rom_base      => g_rom_base_cart,
        g_ram_base      => g_ram_base_cart,
--        g_control_read  => g_control_read,
        g_ram_expansion => g_ram_expansion )
    port map (
        clock           => clock,
        reset           => reset,
        
        io_req          => io_req,
        io_resp         => io_resp,
        
        dma_req         => dma_req_regs,
        dma_resp        => dma_resp_regs,
        
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
        do_reg_output   => do_reg_output,
    
        slot_addr       => slot_addr,
        cpu_write       => cpu_write,
        epyx_timeout    => epyx_timeout,
        
        io_write        => io_write,
        io_read         => io_read,
        io_event_addr   => io_event_addr,
        io_rdata        => io_rdata,
        io_wdata        => io_wdata,
        write_ff00      => write_ff00,
        
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
--        cart_base       => "00000",
        cart_kill       => control.cartridge_kill,

        io_read         => io_read,
        io_write        => io_write,
        io_addr         => io_event_addr,
        io_wdata        => io_wdata,
        slot_addr       => slot_addr,
        epyx_timeout    => epyx_timeout,

        reg_output      => cart_reg_output,
        reg_rdata       => cart_reg_rdata,

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


    g_reu: if g_ram_expansion generate
        signal reu_write    : std_logic := '0';
    begin
        i_reu: entity work.reu
        generic map (
            g_ram_base      => g_ram_base_reu,
            g_ram_tag       => g_tag_reu )
        port map (
            clock           => clock,
            reset           => actual_c64_reset,
            
            -- register interface
            io_write        => reu_write,
            io_read         => io_read,
            io_event_addr   => io_event_addr,
            io_wdata        => io_wdata,
            io_read_addr    => slot_addr(4 downto 0),
            io_rdata        => reu_reg_rdata,
            
            -- system interface
            irq_out         => reu_irq,
            reu_dma_n       => reu_dma_n,
            write_ff00      => write_ff00,
            size_ctrl       => control.reu_size,
            
            -- memory interface
            mem_req         => mem_req_reu,
            mem_resp        => mem_resp_reu,

            dma_req         => dma_req_reu,
            dma_resp        => dma_resp_reu );

        reu_write       <= io_write and control.reu_enable;

        -- when reu_enable is '0', the REU exists, but will never respond to writes to registers
        reu_reg_output   <= '1' when slot_addr(15 downto 8)=X"DF" and control.reu_enable='1' else '0';

    end generate;

    do_reg_output <= reu_reg_output or cart_reg_output;
    io_rdata      <= cart_reg_rdata when slot_addr(8)='0' else reu_reg_rdata;
    cpu_cycle_done <= do_io_event;

    ADDRESS(15 downto 8) <= address_out(15 downto 8) when address_tri_h='1' else (others => 'Z');
    ADDRESS(7 downto 0)  <= address_out(7 downto 0)  when address_tri_l='1' else (others => 'Z');

    RWn  <= rwn_out when rwn_tri='1' else 'Z';

    DATA <= slave_dout when (slave_dtri='1') else
            master_dout when (master_dtri='1') else (others => 'Z');

    -- open drain outputs
    IRQn     <= '0' when irq_n='0' or reu_irq='1' else 'Z';
    NMIn     <= '0' when (control.c64_nmi='1')   or (serve_enable='1' and nmi_n='0') else 'Z';
    EXROMn   <= '0' when (control.c64_exrom='1') or (serve_enable='1' and exrom_n='0') else 'Z';
    GAMEn    <= '0' when (control.c64_game='1')  or (serve_enable='1' and game_n='0') else 'Z';
    RSTn     <= '0' when (reset_button='1' and status.c64_stopped='0' and mask_buttons='0') or
                         (control.c64_reset='1') else 'Z';


    -- arbitration
    i_dma_arb: entity work.dma_bus_arbiter_pri
    generic map (
        g_ports     => 2 )
    port map (
        clock       => clock,
        reset       => reset,
        
        reqs(0)     => dma_req_regs,
        reqs(1)     => dma_req_reu,
        resps(0)    => dma_resp_regs,
        resps(1)    => dma_resp_reu,
        
        req         => dma_req,
        resp        => dma_resp );
    
    i_mem_arb: entity work.mem_bus_arbiter_pri
    generic map (
        g_ports     => 2 )
    port map (
        clock       => clock,
        reset       => reset,
        
        reqs(0)     => mem_req_slot,
        reqs(1)     => mem_req_reu,
        resps(0)    => mem_resp_slot,
        resps(1)    => mem_resp_reu,
        
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
