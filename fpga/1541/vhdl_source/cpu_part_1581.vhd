library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity cpu_part_1581 is
generic (
    g_disk_tag  : std_logic_vector(7 downto 0) := X"03";
    g_cpu_tag   : std_logic_vector(7 downto 0) := X"02";
    g_ram_base  : unsigned(27 downto 0) := X"0060000" );
port (
    clock       : in  std_logic;
    falling     : in  std_logic;
    reset       : in  std_logic;
    tick_1kHz   : in  std_logic;
    tick_4MHz   : in  std_logic;
    
    -- serial bus pins
    atn_o       : out std_logic; -- open drain
    atn_i       : in  std_logic;

    clk_o       : out std_logic; -- open drain
    clk_i       : in  std_logic;    

    data_o      : out std_logic; -- open drain
    data_i      : in  std_logic;
    
    fast_clk_o  : out std_logic; -- open drain
    fast_clk_i  : in  std_logic;

    -- memory interface
    mem_req_cpu     : out t_mem_req;
    mem_resp_cpu    : in  t_mem_resp;
    mem_req_disk    : out t_mem_req;
    mem_resp_disk   : in  t_mem_resp;
    mem_busy        : out std_logic;

    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic;

    -- track stepper interface (for audio samples)
    do_track_in     : out std_logic;
    do_track_out    : out std_logic;

    -- drive pins
    power           : in  std_logic;
    drive_address   : in  std_logic_vector(1 downto 0);
    write_prot_n    : in  std_logic;
    rdy_n           : in  std_logic;
    disk_change_n   : in  std_logic;
    side_0          : out std_logic;
    motor_on        : out std_logic;
    cur_track       : in  unsigned(6 downto 0);
    
    act_led         : out std_logic;
    power_led       : out std_logic );
    
end entity;

architecture structural of cpu_part_1581 is
    signal cpu_write        : std_logic;
    signal cpu_wdata        : std_logic_vector(7 downto 0);
    signal cpu_rdata        : std_logic_vector(7 downto 0);
    signal cpu_addr         : std_logic_vector(16 downto 0);
    signal cpu_irqn         : std_logic;
    signal ext_rdata        : std_logic_vector(7 downto 0) := X"00";
    signal io_rdata         : std_logic_vector(7 downto 0);

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

    signal io_select        : std_logic;
    signal cpu_clk_en       : std_logic;
    type   t_mem_state  is (idle, newcycle, extcycle);
    signal mem_state    : t_mem_state;

    -- "old" style signals
    signal mem_request     : std_logic;
    signal mem_addr        : unsigned(25 downto 0);
    signal mem_rwn         : std_logic;
    signal mem_rack        : std_logic;
    signal mem_dack        : std_logic;
    signal mem_wdata       : std_logic_vector(7 downto 0);

    -- Local signals
    signal write_prot_n_i   : std_logic;
    signal fast_ser_dir     : std_logic;
    signal atn_ack          : std_logic;
    signal clock_out        : std_logic;
    signal my_data_out      : std_logic;
    signal my_fast_data_out : std_logic;

    signal act_led_i        : std_logic;
    signal power_led_i      : std_logic;
    signal drive_address_i  : std_logic_vector(1 downto 0);
    signal rdy_n_i          : std_logic;
    signal motor_n_i        : std_logic;
    signal side_0_i         : std_logic;
    signal motor_on_i       : std_logic;
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
        SOn         => '1' );

    cpu_irqn   <= not cia_irq;
    cpu_clk_en <= falling;
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
                io_select <= '0'; -- not active

                -- Start by checking whether it is RAM
                if cpu_addr(15 downto 13) = "000" then -- 0000-1FFF
                    mem_request <= '1';
                    mem_state   <= extcycle;
                elsif cpu_addr(15) = '1' then -- ROM Area, which is not overridden as RAM
                    if cpu_write = '0' then
                        mem_request <= '1';
                        mem_state <= extcycle;
                    else -- writing to rom -> ignore
                        mem_state  <= idle;
                    end if;
                -- It's not RAM, nor ROM, so it must be internal I/O.
                else
                    io_select <= '1';
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
                io_select   <= '0';
                mem_request <= '0';
                mem_state   <= idle;
            end if;
        end if;
    end process;

    mem_rwn   <= not cpu_write;
    mem_wdata <= cpu_wdata; 

    -- address decoding and data muxing
    -- 0000-1FFF and 8000-FFFF = external memory
    -- 2000-3FFF is free
    -- 4000-5FFF is cia
    -- 6000-7FFF is wd177x
    with cpu_addr(14 downto 13) select io_rdata <= 
        cia_data when "10",
        wd_data  when "11",
        X"FF"     when others;

    cpu_rdata <= io_rdata when io_select='1' else ext_rdata;

    cia_wen <= '1' when cpu_write='1' and io_select='1' and cpu_addr(14 downto 13)="10" else '0';
    cia_ren <= '1' when cpu_write='0' and io_select='1' and cpu_addr(14 downto 13)="10" else '0';

    wd_wen  <= '1' when cpu_write='1' and io_select='1' and cpu_addr(14 downto 13)="11" else '0';
    wd_ren  <= '1' when cpu_write='0' and io_select='1' and cpu_addr(14 downto 13)="11" else '0';

    -- I/O peripherals
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
        port_a_o    => cia_port_a_o, -- low level floppy signals
        port_a_t    => cia_port_a_t,          
        port_a_i    => cia_port_a_i,
        
        port_b_o    => cia_port_b_o, -- IEC and control
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

    -- correctly attach the cia pins to the outside world
    write_prot_n_i  <= (cia_port_b_o(6) or not cia_port_b_t(6)) and write_prot_n;
    fast_ser_dir    <= (cia_port_b_o(5) or not cia_port_b_t(5));
    atn_ack         <= (cia_port_b_o(4) or not cia_port_b_t(4));
    clock_out       <= (cia_port_b_o(3) or not cia_port_b_t(3));
    my_data_out     <= (cia_port_b_o(1) or not cia_port_b_t(1));
    
    my_fast_data_out  <= (cia_sp_o or not cia_sp_t) or not fast_ser_dir; -- active low!
    cia_sp_i          <= (cia_sp_o or not cia_sp_t) when fast_ser_dir = '1' else
                         data_i;
    
    fast_clk_o        <= (cia_cnt_o or not cia_cnt_t) or not fast_ser_dir; -- active low!
    cia_cnt_i         <= (cia_cnt_o or not cia_cnt_t) when fast_ser_dir = '1' else -- output
                         fast_clk_i; -- assume that 74LS241 wins 
     
    cia_port_b_i(7) <= not atn_i;    -- assume that this signal (from 74LS14) wins
    cia_port_b_i(6) <= write_prot_n_i; 
    cia_port_b_i(5) <= fast_ser_dir;
    cia_port_b_i(4) <= atn_ack;
    cia_port_b_i(3) <= clock_out;
    cia_port_b_i(2) <= not clk_i;    -- assume that this signal (from 74LS14) wins
    cia_port_b_i(1) <= my_data_out;
    cia_port_b_i(0) <= not data_i;

    data_o <= (not my_data_out and not (atn_ack and not atn_i) and my_fast_data_out) or not power;
    clk_o  <= not power or not clock_out;
    atn_o  <= '1';
    
    act_led_i       <= (cia_port_a_o(6) or not cia_port_a_t(6));
    power_led_i     <= (cia_port_a_o(5) or not cia_port_a_t(5));
    drive_address_i <= (cia_port_a_o(4 downto 3) or not cia_port_a_t(4 downto 3)) and drive_address;
    motor_n_i       <= (cia_port_a_o(2) or not cia_port_a_t(2));
    rdy_n_i         <= (cia_port_a_o(1) or not cia_port_a_t(1)) and rdy_n;
    side_0_i        <= (cia_port_a_o(0) or not cia_port_a_t(0));

    cia_port_a_i(7) <= disk_change_n;
    cia_port_a_i(6) <= act_led_i;
    cia_port_a_i(5) <= power_led_i;
    cia_port_a_i(4) <= drive_address_i(1);   
    cia_port_a_i(3) <= drive_address_i(0);
    cia_port_a_i(2) <= motor_n_i;
    cia_port_a_i(1) <= rdy_n_i;
    cia_port_a_i(0) <= side_0_i; 
    
    power_led    <= not (power_led_i and power);
    act_led      <= not (act_led_i and power);
    motor_on_i   <= not motor_n_i and power;
    motor_on     <= motor_on_i;
    side_0       <= side_0_i;

    -- Floppy Controller
    i_wd177x: entity work.wd177x
    generic map (
        g_tag        => g_disk_tag
    )
    port map(
        clock        => clock,
        clock_en     => cpu_clk_en,
        reset        => reset,
        tick_1kHz    => tick_1kHz,
        tick_4MHz    => tick_4MHz,

        addr         => unsigned(cpu_addr(1 downto 0)),
        wen          => wd_wen,
        ren          => wd_ren,
        wdata        => cpu_wdata,
        rdata        => wd_data,
        
        do_track_out => do_track_out,
        do_track_in  => do_track_in,
        cur_track    => cur_track,
        stepper_en   => '1',
        motor_en     => motor_on_i,
        
        mem_req      => mem_req_disk,
        mem_resp     => mem_resp_disk,
        io_req       => io_req,
        io_resp      => io_resp,
        io_irq       => io_irq
    );

end architecture;
