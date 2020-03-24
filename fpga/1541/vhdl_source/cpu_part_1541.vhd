library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity cpu_part_1541 is
generic (
    g_tag       : std_logic_vector(7 downto 0) := X"02";
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
    
    -- memory interface
    mem_req         : out t_mem_req;
    mem_resp        : in  t_mem_resp;
    mem_busy        : out std_logic;

    -- Debug port
    debug_data      : out std_logic_vector(31 downto 0);
    debug_valid     : out std_logic;
    
    -- configuration
    bank_is_ram     : in  std_logic_vector(7 downto 1);
    
    -- Parallel cable pins
    via1_port_a_o   : out std_logic_vector(7 downto 0);
    via1_port_a_i   : in  std_logic_vector(7 downto 0);
    via1_port_a_t   : out std_logic_vector(7 downto 0);
    via1_ca2_o      : out std_logic;
    via1_ca2_i      : in  std_logic;
    via1_ca2_t      : out std_logic;
    via1_cb1_o      : out std_logic;
    via1_cb1_i      : in  std_logic;
    via1_cb1_t      : out std_logic;

    -- drive pins
    power           : in  std_logic;
    drive_address   : in  std_logic_vector(1 downto 0);
    motor_on        : out std_logic;
    mode            : out std_logic;
    write_prot_n    : in  std_logic;
    step            : out std_logic_vector(1 downto 0);
    soe             : out std_logic;
    rate_ctrl       : out std_logic_vector(1 downto 0);
    byte_ready      : in  std_logic;
    sync            : in  std_logic;
    
    drv_rdata       : in  std_logic_vector(7 downto 0);
    drv_wdata       : out std_logic_vector(7 downto 0);
    
    act_led         : out std_logic );
    
end cpu_part_1541;

architecture structural of cpu_part_1541 is
    signal bank_is_ram_i    : std_logic_vector(7 downto 0);
    signal cpu_write        : std_logic;
    signal cpu_wdata        : std_logic_vector(7 downto 0);
    signal cpu_rdata        : std_logic_vector(7 downto 0);
    signal cpu_addr         : std_logic_vector(16 downto 0);
    signal cpu_irqn         : std_logic;
    signal ext_rdata        : std_logic_vector(7 downto 0) := X"00";
    signal io_rdata         : std_logic_vector(7 downto 0);
    signal via1_data        : std_logic_vector(7 downto 0);
    signal via2_data        : std_logic_vector(7 downto 0);
    signal via1_wen         : std_logic;
    signal via1_ren         : std_logic;
    signal via2_wen         : std_logic;
    signal via2_ren         : std_logic;

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
    signal io_select        : std_logic;
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
    mem_req.request     <= mem_request;
    mem_req.address     <= mem_addr;
    mem_req.read_writen <= mem_rwn;
    mem_req.data        <= mem_wdata;
    mem_req.tag         <= g_tag;
    mem_req.size        <= "00"; -- 1 byte at a time

    mem_rack  <= '1' when mem_resp.rack_tag = g_tag else '0';
    mem_dack  <= '1' when mem_resp.dack_tag = g_tag else '0';

    cpu: entity work.cpu6502(cycle_exact)
    port map (
        cpu_clk     => clock,
        cpu_clk_en  => cpu_clk_en,
        cpu_reset   => reset,    
    
        cpu_write   => cpu_write,
        
        cpu_wdata   => cpu_wdata,
        cpu_rdata   => cpu_rdata,
        cpu_addr    => cpu_addr,
        --cpu_pc      => cpu_pc,
        
        IRQn        => cpu_irqn, -- IRQ interrupt (level sensitive)
        NMIn        => '1',
    
        SOn         => byte_ready );

    -- Generate an output stream to debug internal operation of 1541 CPU
    process(clock)
    begin
        if rising_edge(clock) then
            debug_valid <= '0';
            if cpu_clk_en = '1' then
                debug_data  <= '0' & atn_i & data_i & clk_i & sync & byte_ready & cpu_irqn & not cpu_write & cpu_rdata & cpu_addr(15 downto 0);
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

    cpu_irqn   <= not(via1_irq or via2_irq);
    cpu_clk_en <= falling;
    cpu_rising <= rising;
    mem_busy   <= '0' when mem_state = idle else '1';

    bank_is_ram_i <= bank_is_ram & '0';
    
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

                -- Start by checking whether it is an extended RAM block
                if bank_is_ram_i(to_integer(unsigned(cpu_addr(15 downto 13))))='1' then
                    mem_request <= '1';
                    mem_state   <= extcycle;
                elsif cpu_addr(15) = '1' then -- ROM Area, which is not overridden as RAM
                    if cpu_write = '0' then
                        mem_request <= '1';
                        mem_state <= extcycle;
                    else -- writing to rom -> ignore
                        mem_state  <= idle;
                    end if;
                -- It's not extended RAM, not ROM, so it must be internal RAM or I/O.
                elsif cpu_addr(12 downto 11) = "00" then -- Internal RAM
                    mem_addr(14 downto 13) <= "00"; -- force mirroring for 2000-27FF, 4000-47FF and 6000-67FF.
                    mem_request <= '1';
                    mem_state <= extcycle;
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
                    ext_rdata  <= mem_resp.data;
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
    with cpu_addr(12 downto 10) select io_rdata <= 
        via1_data when "110",
        via2_data when "111",
        X"FF"     when others;

    cpu_rdata <= io_rdata when io_select='1' else ext_rdata;

    via1_wen <= '1' when cpu_write='1' and io_select='1' and cpu_addr(12 downto 10)="110" else '0';
    via1_ren <= '1' when cpu_write='0' and io_select='1' and cpu_addr(12 downto 10)="110" else '0';
    
    via2_wen <= '1' when cpu_write='1' and io_select='1' and cpu_addr(12 downto 10)="111" else '0';
    via2_ren <= '1' when cpu_write='0' and io_select='1' and cpu_addr(12 downto 10)="111" else '0';

    
    -- correctly attach the VIA pins to the outside world
    via1_ca1         <= not atn_i;
    via1_cb2_i       <= via1_cb2_o or not via1_cb2_t;
 

    -- Because Port B reads its own output when set to output, we do not need to consider the direction here
    via1_port_b_i(7) <= not atn_i;
    -- the following bits should read 0 when the jumper is closed (drive select = 0) or when driven low by the VIA itself
    via1_port_b_i(6) <= drive_address(1); -- drive select
    via1_port_b_i(5) <= drive_address(0); -- drive select;
    via1_port_b_i(4) <= '1'; -- atn a     - PUP
    via1_port_b_i(3) <= '1'; -- clock out - PUP
    via1_port_b_i(2) <= not (clk_i and (not (via1_port_b_o(3) or not via1_port_b_t(3))));
    via1_port_b_i(1) <= '1'; -- data out  - PUP
    via1_port_b_i(0) <= not (data_i and (not (via1_port_b_o(1) or not via1_port_b_t(1))) and (not ((via1_port_b_o(4) or not via1_port_b_t(4)) xor (not atn_i))));

    --auto_o <= not power or via1_port_b_i(4);
    data_o <= not power or ((not (via1_port_b_o(1) or not via1_port_b_t(1))) and (not ((via1_port_b_o(4) or not via1_port_b_t(4)) xor (not atn_i))));
    clk_o  <= not power or (not (via1_port_b_o(3) or not via1_port_b_t(3)));
    atn_o  <= '1';
    
    -- Do the same for VIA 2. Pin levels instead of output register.    
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
    
    act_led      <= not (via2_port_b_o(3) or not via2_port_b_t(3)) or not power;
    mode         <= via2_cb2_i;
    step(0)      <= via2_port_b_o(0) or not via2_port_b_t(0);
    step(1)      <= via2_port_b_o(1) or not via2_port_b_t(1);
    motor_on     <= (via2_port_b_o(2) or not via2_port_b_t(2)) and power;
    soe          <= via2_ca2_i;
    rate_ctrl(0) <= via2_port_b_o(5) or not via2_port_b_t(5);
    rate_ctrl(1) <= via2_port_b_o(6) or not via2_port_b_t(6);
       

end structural;

-- Original mapping:
-- 0000-07FF   RAM
-- 0800-17FF   open
-- 1800-1BFF   VIA 1
-- 1C00-1CFF   VIA 2
-- 2000-27FF   RAM
-- 2800-37FF   open
-- 3800-3BFF   VIA 1
-- 3C00-3CFF   VIA 2
-- 4000-47FF   RAM
-- 4800-57FF   open
-- 5800-5BFF   VIA 1
-- 5C00-5CFF   VIA 2
-- 6000-67FF   RAM
-- 6800-77FF   open
-- 7800-7BFF   VIA 1
-- 7C00-7CFF   VIA 2
-- 8000-BFFF   ROM image (mirror) 
-- C000-FFFF   ROM image 
