library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity cpu_part_1571 is
generic (
    g_disk_tag  : std_logic_vector(7 downto 0) := X"03";
    g_cpu_tag   : std_logic_vector(7 downto 0) := X"02";
    g_ram_base  : unsigned(27 downto 0) := X"0060000" );
port (
    clock       : in  std_logic;
    falling     : in  std_logic;
    rising      : in  std_logic;
    reset       : in  std_logic;

    -- serial bus pins
    atn_o       : out std_logic; -- open drain
    atn_i       : in  std_logic;

    clk_o       : out std_logic; -- open drain
    clk_i       : in  std_logic;    

    data_o      : out std_logic; -- open drain
    data_i      : in  std_logic;
    
    fast_clk_o  : out std_logic; -- open drain
    fast_clk_i  : in  std_logic;

    -- Debug port
    debug_data      : out std_logic_vector(31 downto 0);
    debug_valid     : out std_logic;

    -- memory interface
    mem_req_cpu     : out t_mem_req;
    mem_resp_cpu    : in  t_mem_resp;
    mem_req_disk    : out t_mem_req;
    mem_resp_disk   : in  t_mem_resp;
    mem_busy        : out std_logic;

    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic;

    -- drive pins
    power           : in  std_logic;
    drive_address   : in  std_logic_vector(1 downto 0);
    motor_on        : out std_logic;
    mode            : out std_logic;
    write_prot_n    : in  std_logic;
    step            : out std_logic_vector(1 downto 0);
    rate_ctrl       : out std_logic_vector(1 downto 0);
    byte_ready      : in  std_logic;
    sync            : in  std_logic;
    rdy_n           : in  std_logic;
    disk_change_n   : in  std_logic;
    side_0          : out std_logic;
    two_MHz         : out std_logic;
    track_0         : in  std_logic;
    
    drv_rdata       : in  std_logic_vector(7 downto 0);
    drv_wdata       : out std_logic_vector(7 downto 0);
    
    act_led         : out std_logic );
    
end entity;

architecture structural of cpu_part_1571 is
    signal soe              : std_logic;
    signal so_n             : std_logic;
    signal cpu_write        : std_logic;
    signal cpu_wdata        : std_logic_vector(7 downto 0);
    signal cpu_rdata        : std_logic_vector(7 downto 0);
    signal cpu_addr         : std_logic_vector(16 downto 0);
    signal cpu_irqn         : std_logic;
    signal ext_rdata        : std_logic_vector(7 downto 0) := X"00";

    signal via1_data        : std_logic_vector(7 downto 0);
    signal via2_data        : std_logic_vector(7 downto 0);
    signal via1_wen         : std_logic;
    signal via1_ren         : std_logic;
    signal via2_wen         : std_logic;
    signal via2_ren         : std_logic;
    signal cia_data         : std_logic_vector(7 downto 0);
    signal cia_wen          : std_logic;
    signal cia_ren          : std_logic;

    signal wd_data          : std_logic_vector(7 downto 0);
    signal wd_wen           : std_logic;
    signal wd_ren           : std_logic;

    signal cia_port_a_o     : std_logic_vector(7 downto 0);
    signal cia_port_a_t     : std_logic_vector(7 downto 0);
    signal cia_port_a_i     : std_logic_vector(7 downto 0);
    signal cia_port_b_o     : std_logic_vector(7 downto 0);
    signal cia_port_b_t     : std_logic_vector(7 downto 0);
    signal cia_port_b_i     : std_logic_vector(7 downto 0);
    signal cia_sp_o         : std_logic;
    signal cia_sp_i         : std_logic;
    signal cia_sp_t         : std_logic;
    signal cia_cnt_o        : std_logic;
    signal cia_cnt_i        : std_logic;
    signal cia_cnt_t        : std_logic;
    signal cia_irq          : std_logic;

    signal via1_port_a_o    : std_logic_vector(7 downto 0);
    signal via1_port_a_t    : std_logic_vector(7 downto 0);
    signal via1_port_a_i    : std_logic_vector(7 downto 0);
    signal via1_ca2_o       : std_logic;
    signal via1_ca2_i       : std_logic;
    signal via1_ca2_t       : std_logic;
    signal via1_cb1_o       : std_logic;
    signal via1_cb1_i       : std_logic;
    signal via1_cb1_t       : std_logic;
    signal via1_port_b_o    : std_logic_vector(7 downto 0);
    signal via1_port_b_t    : std_logic_vector(7 downto 0);
    signal via1_port_b_i    : std_logic_vector(7 downto 0);
    signal via1_ca1         : std_logic;
    signal via1_cb2_o       : std_logic;
    signal via1_cb2_i       : std_logic;
    signal via1_cb2_t       : std_logic;
    signal via1_irq         : std_logic;
    signal via2_port_b_o    : std_logic_vector(7 downto 0);
    signal via2_port_b_t    : std_logic_vector(7 downto 0);
    signal via2_port_b_i    : std_logic_vector(7 downto 0);
    signal via2_ca2_o       : std_logic;
    signal via2_ca2_i       : std_logic;
    signal via2_ca2_t       : std_logic;
    signal via2_cb1_o       : std_logic;
    signal via2_cb1_i       : std_logic;
    signal via2_cb1_t       : std_logic;
    signal via2_cb2_o       : std_logic;
    signal via2_cb2_i       : std_logic;
    signal via2_cb2_t       : std_logic;
    signal via2_irq         : std_logic;

    -- Local signals
    signal fast_ser_dir     : std_logic;
    signal atn_ack          : std_logic;
    signal my_clk_out       : std_logic;
    signal my_data_out      : std_logic;
    signal my_fast_data_out : std_logic;

    signal cpu_clk_en       : std_logic;
    signal cpu_rising       : std_logic;
    type   t_mem_state  is (idle, newcycle, extcycle);
    signal mem_state    : t_mem_state;

    -- "old" style signals
    signal mem_request     : std_logic;
    signal mem_addr        : unsigned(25 downto 0);
    signal mem_rwn         : std_logic;
    signal mem_rack        : std_logic;
    signal mem_dack        : std_logic;
    signal mem_wdata       : std_logic_vector(7 downto 0);

begin
    mem_req_cpu.request     <= mem_request;
    mem_req_cpu.address     <= mem_addr;
    mem_req_cpu.read_writen <= mem_rwn;
    mem_req_cpu.data        <= mem_wdata;
    mem_req_cpu.tag         <= g_cpu_tag;
    mem_req_cpu.size        <= "00"; -- 1 byte at a time

    mem_rack  <= '1' when mem_resp_cpu.rack_tag = g_cpu_tag else '0';
    mem_dack  <= '1' when mem_resp_cpu.dack_tag = g_cpu_tag else '0';

    cpu: entity work.cpu6502(cycle_exact)
    port map (
        cpu_clk     => clock,
        cpu_clk_en  => cpu_clk_en,
        cpu_reset   => reset,    
    
        cpu_write   => cpu_write,
        
        cpu_wdata   => cpu_wdata,
        cpu_rdata   => cpu_rdata,
        cpu_addr    => cpu_addr,
        
        IRQn        => cpu_irqn, -- IRQ interrupt (level sensitive)
        NMIn        => '1',
    
        SOn         => so_n );

    -- Generate an output stream to debug internal operation of 1541 CPU
    process(clock)
    begin
        if rising_edge(clock) then
            debug_valid <= '0';
            if cpu_clk_en = '1' then
                debug_data  <= '0' & atn_i & data_i & clk_i & sync & so_n & cpu_irqn & not cpu_write & cpu_rdata & cpu_addr(15 downto 0);
                debug_valid <= '1';
                if cpu_write = '1' then
                    debug_data(23 downto 16) <= cpu_wdata;
                end if;
            end if;
        end if;
    end process;

    via1: entity work.via6522
    port map (
        clock       => clock,
        falling     => cpu_clk_en,
        rising      => cpu_rising,
        reset       => reset,
                                
        addr        => cpu_addr(3 downto 0),
        wen         => via1_wen,
        ren         => via1_ren,
        data_in     => cpu_wdata,
        data_out    => via1_data,
                                
        -- pio --   
        port_a_o    => via1_port_a_o,
        port_a_t    => via1_port_a_t,
        port_a_i    => via1_port_a_i,
                                
        port_b_o    => via1_port_b_o,
        port_b_t    => via1_port_b_t,
        port_b_i    => via1_port_b_i,
    
        -- handshake pins
        ca1_i       => via1_ca1,
                            
        ca2_o       => via1_ca2_o,
        ca2_i       => via1_ca2_i,
        ca2_t       => via1_ca2_t,
                            
        cb1_o       => via1_cb1_o,
        cb1_i       => via1_cb1_i,
        cb1_t       => via1_cb1_t,
                            
        cb2_o       => via1_cb2_o,
        cb2_i       => via1_cb2_i,
        cb2_t       => via1_cb2_t,
                            
        irq         => via1_irq  );
    
    via2: entity work.via6522
    port map (
        clock       => clock,
        falling     => cpu_clk_en,
        rising      => cpu_rising,
        reset       => reset,
                                
        addr        => cpu_addr(3 downto 0),
        wen         => via2_wen,
        ren         => via2_ren,
        data_in     => cpu_wdata,
        data_out    => via2_data,
                                
        -- pio --   
        port_a_o    => drv_wdata,
        port_a_t    => open,
        port_a_i    => drv_rdata,
                                
        port_b_o    => via2_port_b_o,
        port_b_t    => via2_port_b_t,
        port_b_i    => via2_port_b_i,
    
        -- handshake pins
        ca1_i       => byte_ready,
                            
        ca2_o       => via2_ca2_o,
        ca2_i       => via2_ca2_i,
        ca2_t       => via2_ca2_t,
                            
        cb1_o       => via2_cb1_o,
        cb1_i       => via2_cb1_i,
        cb1_t       => via2_cb1_t,
                            
        cb2_o       => via2_cb2_o,
        cb2_i       => via2_cb2_i,
        cb2_t       => via2_cb2_t,
                            
        irq         => via2_irq  );

    i_cia1: entity work.cia_registers
    generic map (
        g_report    => false,
        g_unit_name => "CIA_1581" )
    port map (
        clock       => clock,
        falling     => falling,
        reset       => reset,
        tod_pin     => '1', -- depends on jumper
        
        addr        => unsigned(cpu_addr(3 downto 0)),
        data_in     => cpu_wdata,
        wen         => cia_wen,
        ren         => cia_ren,
        data_out    => cia_data,

        -- pio --
        port_a_o    => cia_port_a_o, -- unused
        port_a_t    => cia_port_a_t,          
        port_a_i    => cia_port_a_i,
        
        port_b_o    => cia_port_b_o, -- unused
        port_b_t    => cia_port_b_t,
        port_b_i    => cia_port_b_i,
    
        -- serial pin
        sp_o        => cia_sp_o, -- Burst mode IEC data
        sp_i        => cia_sp_i,
        sp_t        => cia_sp_t,
        
        cnt_i       => cia_cnt_i, -- Burst mode IEC clock
        cnt_o       => cia_cnt_o,
        cnt_t       => cia_cnt_t,
        
        pc_o        => open,
        flag_i      => atn_i, -- active low ATN in
        irq         => cia_irq );


    cpu_irqn   <= not(via1_irq or via2_irq);
    cpu_clk_en <= falling;
    cpu_rising <= rising;
    mem_busy   <= '0' when mem_state = idle else '1';

    -- Fetch ROM byte
    process(clock)
    begin
        if rising_edge(clock) then
            mem_addr(25 downto 16) <= g_ram_base(25 downto 16);
            
            case mem_state is
            when idle =>
                if cpu_clk_en = '1' then
                    mem_state <= newcycle;
                end if;
            
            when newcycle => -- we have a new address now
                mem_addr(15 downto  0) <= unsigned(cpu_addr(15 downto 0));

                if cpu_addr(15) = '1' then -- ROM Area, which is not overridden as RAM
                    if cpu_write = '0' then
                        mem_request <= '1';
                        mem_state <= extcycle;
                    else -- writing to rom -> ignore
                        mem_state  <= idle;
                    end if;
                -- It's not extended RAM, not ROM, so it must be internal RAM or I/O.
                elsif cpu_addr(14 downto 11) = "0000" then -- Internal RAM
                    mem_request <= '1';
                    mem_state <= extcycle;
                else -- this applies to anything 0000-07FF, thus 0800-7FFF!
                    mem_state  <= idle;
                end if;
            
            when extcycle =>
                if mem_rack='1' then
                    mem_request <= '0';
                    if cpu_write='1' then
                        mem_state  <= idle;
                    end if;                    
                end if;                        
                if mem_dack='1' and cpu_write='0' then -- only for reads
                    ext_rdata  <= mem_resp_cpu.data;
                    mem_state  <= idle;
                end if;
            
            when others =>
                null;
            end case;                        

            if reset='1' then
                mem_request <= '0';
                mem_state   <= idle;
            end if;
        end if;
    end process;

    mem_rwn   <= not cpu_write;
    mem_wdata <= cpu_wdata; 

    -- address decoding and data muxing
    process(cpu_addr, ext_rdata, via1_data, via2_data, cia_data, wd_data)
    begin
        if cpu_addr(15) = '1' then -- 8000-FFFF
            cpu_rdata <= ext_rdata;
        elsif cpu_addr(14) = '1' then -- 4000-7FFF
            cpu_rdata <= cia_data;
        elsif cpu_addr(13) = '1' then -- 2000-3FFF
            cpu_rdata <= wd_data;
        elsif cpu_addr(12 downto 10) = "110" then -- 1800-1BFF
            cpu_rdata <= via1_data;
        elsif cpu_addr(12 downto 10) = "111" then -- 1C00-1FFF 
            cpu_rdata <= via2_data;
        elsif cpu_addr(12 downto 11) = "00" then -- 0000-07FF
            cpu_rdata <= ext_rdata;
        else
            cpu_rdata <= X"FF";
        end if;
    end process;
    
    via1_wen <= '1' when cpu_write='1' and cpu_addr(15 downto 10)="000110" else '0';
    via1_ren <= '1' when cpu_write='0' and cpu_addr(15 downto 10)="000110" else '0';
    
    via2_wen <= '1' when cpu_write='1' and cpu_addr(15 downto 10)="000111" else '0';
    via2_ren <= '1' when cpu_write='0' and cpu_addr(15 downto 10)="000111" else '0';

    cia_wen <= '1' when cpu_write='1' and cpu_addr(15 downto 14)="01" and cpu_clk_en = '1' else '0';
    cia_ren <= '1' when cpu_write='0' and cpu_addr(15 downto 14)="01" and cpu_clk_en = '1' else '0';

    wd_wen  <= '1' when cpu_write='1' and cpu_addr(15 downto 13)="001" and cpu_clk_en = '1' else '0';
    wd_ren  <= '1' when cpu_write='0' and cpu_addr(15 downto 13)="001" and cpu_clk_en = '1' else '0';
    
    -- correctly attach the VIA pins to the outside world
    via1_ca1         <= not atn_i;
    via1_cb2_i       <= via1_cb2_o or not via1_cb2_t;
 
    -- Via Port A is used in the 1541 for the parallel interface (SpeedDos / DolphinDos), but in the 1571 some of the pins are connected internally
    via1_port_a_i(7) <= (via1_port_a_o(7) or not via1_port_a_t(7)) and so_n; -- Byte ready in schematic. Our byte_ready signal is not yet masked with so_e
    via1_port_a_i(6) <= (via1_port_a_o(6) or not via1_port_a_t(6)); -- ATN OUT (not connected)
    via1_port_a_i(5) <= (via1_port_a_o(5) or not via1_port_a_t(5)); -- 2 MHz mode
    via1_port_a_i(4) <= (via1_port_a_o(4) or not via1_port_a_t(4));
    via1_port_a_i(3) <= (via1_port_a_o(3) or not via1_port_a_t(3));
    via1_port_a_i(2) <= (via1_port_a_o(2) or not via1_port_a_t(2)); -- SIDE
    via1_port_a_i(1) <= (via1_port_a_o(1) or not via1_port_a_t(1)); -- SER_DIR
    via1_port_a_i(0) <= (via1_port_a_o(0) or not via1_port_a_t(0)) and not track_0;

    side_0       <= not via1_port_a_i(2);
    two_MHz      <= via1_port_a_i(5);
    fast_ser_dir <= via1_port_a_i(3); -- '1' is output

    -- Because Port B reads its own output when set to output, we do not need to consider the direction here
    via1_port_b_i(7) <= not atn_i;
    via1_port_b_i(6) <= drive_address(1); -- drive select
    via1_port_b_i(5) <= drive_address(0); -- drive select;
    via1_port_b_i(4) <= '1'; -- atn a     - PUP
    via1_port_b_i(3) <= '1'; -- clock out - PUP
    via1_port_b_i(2) <= not (clk_i and not my_clk_out);
    via1_port_b_i(1) <= '1'; -- data out  - PUP
    via1_port_b_i(0) <= not (data_i and not my_data_out and (not (atn_ack xor (not atn_i))));

    atn_ack     <= via1_port_b_o(4) or not via1_port_b_t(4);
    my_data_out <= via1_port_b_o(1) or not via1_port_b_t(1);
    my_clk_out  <= via1_port_b_o(3) or not via1_port_b_t(3);
    
    -- Do the same for VIA 2. Port A should read the pin, Port B reads the output internally, so for port B only actual input signals should be connected    
    via2_port_b_i(7) <= sync;
    via2_port_b_i(6) <= '1'; --Density
    via2_port_b_i(5) <= '1'; --Density
    via2_port_b_i(4) <= write_prot_n;    
    via2_port_b_i(3) <= '1'; -- LED
    via2_port_b_i(2) <= '1'; -- Motor
    via2_port_b_i(1) <= '1'; -- Step
    via2_port_b_i(0) <= '1'; -- Step   
    via2_cb1_i       <= via2_cb1_o       or not via2_cb1_t;
    via2_cb2_i       <= via2_cb2_o       or not via2_cb2_t;
    via2_ca2_i       <= via2_ca2_o       or not via2_ca2_t;
    
    -- CIA ports are not used
    cia_port_a_i <= cia_port_a_o or not cia_port_a_t;
    cia_port_a_i <= cia_port_b_o or not cia_port_b_t;

    act_led      <= not (via2_port_b_o(3) or not via2_port_b_t(3)) or not power;
    mode         <= via2_cb2_i;
    step(0)      <= via2_port_b_o(0) or not via2_port_b_t(0);
    step(1)      <= via2_port_b_o(1) or not via2_port_b_t(1);
    motor_on     <= (via2_port_b_o(2) or not via2_port_b_t(2)) and power;
    rate_ctrl(0) <= via2_port_b_o(5) or not via2_port_b_t(5);
    rate_ctrl(1) <= via2_port_b_o(6) or not via2_port_b_t(6);
    soe          <= via2_ca2_i;
    so_n         <= byte_ready or not soe;


    data_o <= not power or (not my_data_out and my_fast_data_out and (not (atn_ack xor (not atn_i))));
    clk_o  <= not power or not my_clk_out;
    atn_o  <= '1';

    my_fast_data_out  <= (cia_sp_o or not cia_sp_t) or not fast_ser_dir; -- active low!
    cia_sp_i          <= (cia_sp_o or not cia_sp_t) when fast_ser_dir = '1' else
                         data_i;
    
    fast_clk_o        <= (cia_cnt_o or not cia_cnt_t) or not fast_ser_dir; -- active low!
    cia_cnt_i         <= (cia_cnt_o or not cia_cnt_t) when fast_ser_dir = '1' else -- output
                         fast_clk_i; -- assume that 74LS241 wins 

    -- Floppy Controller
    i_wd177x: entity work.wd177x
    generic map (
        g_tag        => g_disk_tag
    )
    port map(
        clock        => clock,
        reset        => reset,
        addr         => unsigned(cpu_addr(1 downto 0)),
        wen          => wd_wen,
        ren          => wd_ren,
        wdata        => cpu_wdata,
        rdata        => wd_data,
        
        tick_1kHz    => '0', --tick_1kHz,
--        do_track_out => do_track_out,
--        do_track_in  => do_track_in,

        mem_req      => mem_req_disk,
        mem_resp     => mem_resp_disk,
        io_req       => io_req,
        io_resp      => io_resp,
        io_irq       => io_irq
    );

end architecture;

-- Original mapping:
-- 0000-07FF   RAM
-- 0800-17FF   open
-- 1800-1BFF   VIA 1
-- 1C00-1FFF   VIA 2
-- 2000-3FFF   WD17xx
-- 4000-7FFF   CIA 6526
-- 8000-FFFF   ROM image
 
