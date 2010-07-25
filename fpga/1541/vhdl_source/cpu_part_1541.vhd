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
    clock_en    : in  std_logic;
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
    
    -- trace out
    cpu_pc          : out std_logic_vector(15 downto 0);
    
    -- configuration
    bank_is_ram     : in  std_logic_vector(7 downto 0);
    
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
    track_is_0      : in  std_logic;
    
    drv_rdata       : in  std_logic_vector(7 downto 0);
    drv_wdata       : out std_logic_vector(7 downto 0);
    
    act_led         : out std_logic );
    
end cpu_part_1541;

architecture structural of cpu_part_1541 is
    signal cpu_write        : std_logic;
    signal cpu_wdata        : std_logic_vector(7 downto 0);
    signal cpu_rdata        : std_logic_vector(7 downto 0);
    signal cpu_addr         : std_logic_vector(16 downto 0);
    signal cpu_irqn         : std_logic;
    signal ext_rdata        : std_logic_vector(7 downto 0) := X"00";
    signal io_rdata         : std_logic_vector(7 downto 0);
    signal via1_data        : std_logic_vector(7 downto 0);
    signal via2_data        : std_logic_vector(7 downto 0);
    signal ram_en           : std_logic;
    signal via1_wen         : std_logic;
    signal via1_ren         : std_logic;
    signal via2_wen         : std_logic;
    signal via2_ren         : std_logic;

    signal via1_port_a_o    : std_logic_vector(7 downto 0);
    signal via1_port_a_t    : std_logic_vector(7 downto 0);
    signal via1_port_a_i    : std_logic_vector(7 downto 0);

    signal via1_port_b_o    : std_logic_vector(7 downto 0);
    signal via1_port_b_t    : std_logic_vector(7 downto 0);
    signal via1_port_b_i    : std_logic_vector(7 downto 0);

    signal via1_ca1         : std_logic;
    signal via1_ca2         : std_logic;
    signal via1_cb1         : std_logic;
    signal via1_cb2         : std_logic;
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
    signal bank_is_io       : std_logic_vector(7 downto 0);
    signal io_select        : std_logic;
    signal rdata_mux        : std_logic;
    signal cpu_ready    : std_logic;
    signal need_cycle   : unsigned(2 downto 0);
    signal done_cycle   : unsigned(2 downto 0);
    type   t_mem_state  is (idle, cpubusy, newcycle, extcycle);
    signal mem_state    : t_mem_state;

    signal clock_en_d   : std_logic;
    signal clock_en_dd  : std_logic;

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

    mem_rack  <= '1' when mem_resp.rack_tag = g_tag else '0';
    mem_dack  <= '1' when mem_resp.dack_tag = g_tag else '0';

    cpu: entity work.cpu6502(cycle_exact)
    port map (
        cpu_clk     => clock,
        cpu_reset   => reset,    
    
        cpu_ready   => cpu_ready,
        cpu_write   => cpu_write,
        
        cpu_wdata   => cpu_wdata,
        cpu_rdata   => cpu_rdata,
        cpu_addr    => cpu_addr,
        cpu_pc      => cpu_pc,
        
        IRQn        => cpu_irqn, -- IRQ interrupt (level sensitive)
        NMIn        => '1',
    
        SOn         => byte_ready );


    via1: entity work.via6522
    port map (
        clock       => clock,
        clock_en    => cpu_ready,
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
                            
        ca2_o       => via1_ca2,
        ca2_i       => via1_ca2,
        ca2_t       => open,
                            
        cb1_o       => via1_cb1,
        cb1_i       => via1_cb1,
        cb1_t       => open,
                            
        cb2_o       => via1_cb2,
        cb2_i       => via1_cb2,
        cb2_t       => open,
                            
        irq         => via1_irq  );
    
    via2: entity work.via6522
    port map (
        clock       => clock,
        clock_en    => cpu_ready,
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

    cpu_irqn <= not(via1_irq or via2_irq);


    -- Fetch ROM byte
    process(clock)
    begin
        if rising_edge(clock) then
            if clock_en='1' then
                need_cycle <= need_cycle + 1;
            end if;

            bank_is_io <= "0000" & not bank_is_ram(3 downto 1) & '1';
            mem_addr(25 downto 16) <= g_ram_base(25 downto 16);
            mem_addr(15 downto  0) <= unsigned(cpu_addr(15 downto 0));
            
            clock_en_d <= clock_en;
            clock_en_dd <= clock_en_d;

            cpu_ready <= '0';
            
            case mem_state is
            when idle =>
                if need_cycle /= done_cycle then
                    cpu_ready <= '1';
                    mem_state <= cpubusy;
                end if;
            
            when cpubusy =>
                mem_state <= newcycle;
            
            when newcycle => -- we have a new address now
                io_select <= '0';
                if bank_is_io(to_integer(unsigned(cpu_addr(15 downto 13))))='1' then
                    rdata_mux  <= '1'; -- io
                    if cpu_addr(12)='0' then -- lower 4K of IO block is possibly RAM
                        mem_request <= '1';
                        mem_state <= extcycle;
                        mem_addr(14 downto 13) <= "00"; -- cause mirroring
                    else
                        io_select  <= '1';
                        done_cycle <= done_cycle + 1;
                        mem_state  <= idle;
                    end if;
                elsif cpu_write='0' or bank_is_ram(to_integer(unsigned(cpu_addr(15 downto 13))))='1' then -- ram is writeable, rom is not
                    rdata_mux <= '0';
                    mem_request   <= '1';
                    mem_state <= extcycle;
                else -- write to rom -> ignore
                    done_cycle <= done_cycle + 1;
                    mem_state  <= idle;
                end if;
            
            when extcycle =>
                if mem_rack='1' then
                    mem_request <= '0';
                    if cpu_write='1' then
                        done_cycle <= done_cycle + 1;
                        mem_state  <= idle;
                    end if;                    
                end if;                        
                if mem_dack='1' and cpu_write='0' then -- only for reads
                    ext_rdata  <= mem_resp.data;
                    done_cycle <= done_cycle + 1;
                    mem_state  <= idle;
                end if;
            
            when others =>
                null;
            end case;                        

            if reset='1' then
                rdata_mux   <= '0';
                io_select   <= '0';
                cpu_ready   <= '0';
                mem_request <= '0';
                mem_state   <= idle;
                need_cycle  <= "000";
                done_cycle  <= "000";
            end if;
        end if;
    end process;

    mem_rwn   <= not cpu_write;
    mem_wdata <= cpu_wdata; 

    -- address decoding and data muxing
    with cpu_addr(12 downto 10) select io_rdata <= 
        ext_rdata when "000",
        ext_rdata when "001",
        via1_data when "110",
        via2_data when "111",
        X"FF"     when others;

    cpu_rdata <= io_rdata when rdata_mux='1' else ext_rdata;

    via1_wen <= '1' when cpu_write='1' and cpu_ready='1' and io_select='1' and cpu_addr(12 downto 10)="110" else '0';
    via1_ren <= '1' when cpu_write='0' and cpu_ready='1' and io_select='1' and cpu_addr(12 downto 10)="110" else '0';
    
    via2_wen <= '1' when cpu_write='1' and cpu_ready='1' and io_select='1' and cpu_addr(12 downto 10)="111" else '0';
    via2_ren <= '1' when cpu_write='0' and cpu_ready='1' and io_select='1' and cpu_addr(12 downto 10)="111" else '0';

    
    -- correctly attach the VIA pins to the outside world
    
    -- pull up when not driven...
    via1_port_a_i(7 downto 1) <= via1_port_a_o(7 downto 1) or not via1_port_a_t(7 downto 1);
    via1_port_a_i(0)          <= track_is_0;
    
    via1_ca1 <= not atn_i;
    via1_port_b_i(7) <= not atn_i;
    -- the following bits should read 0 when the jumper is closed (drive select = 0) or when driven low by the VIA itself
    via1_port_b_i(6) <= drive_address(1) and (not via1_port_b_t(6) or via1_port_b_o(6)); -- drive select
    via1_port_b_i(5) <= drive_address(0) and (not via1_port_b_t(5) or via1_port_b_o(5)); -- drive select;
    via1_port_b_i(4) <= via1_port_b_o(4) or not via1_port_b_t(4); -- atn a     - PUP
    via1_port_b_i(3) <= via1_port_b_o(3) or not via1_port_b_t(3); -- clock out - PUP
    via1_port_b_i(2) <= not clk_i;
    via1_port_b_i(1) <= via1_port_b_o(1) or not via1_port_b_t(1); -- data out  - PUP
    via1_port_b_i(0) <= not data_i;

--    auto_o <= not power or via1_port_b_i(4);
    data_o <= not power or ((not via1_port_b_i(1)) and (not (via1_port_b_i(4) xor via1_port_b_i(7))));
    clk_o  <= not power or not via1_port_b_i(3);
    atn_o  <= '1';
    
    -- Do the same for VIA 2
    via2_port_b_i(7) <= sync;
    via2_port_b_i(6) <= via2_port_b_o(6) or not via2_port_b_t(6);    
    via2_port_b_i(5) <= via2_port_b_o(5) or not via2_port_b_t(5);    
    via2_port_b_i(4) <= write_prot_n;    
    via2_port_b_i(3) <= via2_port_b_o(3) or not via2_port_b_t(3);    
    via2_port_b_i(2) <= via2_port_b_o(2) or not via2_port_b_t(2);    
    via2_port_b_i(1) <= via2_port_b_o(1) or not via2_port_b_t(1);    
    via2_port_b_i(0) <= via2_port_b_o(0) or not via2_port_b_t(0);    
    via2_cb1_i       <= via2_cb1_o       or not via2_cb1_t;
    via2_cb2_i       <= via2_cb2_o       or not via2_cb2_t;
    via2_ca2_i       <= via2_ca2_o       or not via2_ca2_t;
    
    act_led      <= not via2_port_b_i(3) or not power;
    mode         <= via2_cb2_i;
    step(0)      <= via2_port_b_i(0);
    step(1)      <= via2_port_b_i(1);
    motor_on     <= via2_port_b_i(2) and power;
    soe          <= via2_ca2_i;
    rate_ctrl(0) <= via2_port_b_i(5);
    rate_ctrl(1) <= via2_port_b_i(6);
       

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
