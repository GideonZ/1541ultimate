library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity mm_drive_cpu is
generic (
    g_disk_tag  : std_logic_vector(7 downto 0) := X"03";
    g_cpu_tag   : std_logic_vector(7 downto 0) := X"02";
    g_ram_base  : unsigned(27 downto 0) := X"0060000" );
port (
    clock       : in  std_logic;
    falling     : in  std_logic;
    rising      : in  std_logic;
    reset       : in  std_logic;
    tick_1kHz   : in  std_logic;
    tick_4MHz   : in  std_logic;

    -- Drive Type
    drive_type  : in  natural range 0 to 2 := 0; -- 0 = 1541, 1 = 1571, 2 = 1581

    -- serial bus pins
    atn_o       : out std_logic; -- open drain
    atn_i       : in  std_logic;
    clk_o       : out std_logic; -- open drain
    clk_i       : in  std_logic;    
    data_o      : out std_logic; -- open drain
    data_i      : in  std_logic;
    fast_clk_o  : out std_logic; -- open drain
    fast_clk_i  : in  std_logic;

    -- Parallel cable connection
    par_data_o  : out std_logic_vector(7 downto 0);
    par_data_t  : out std_logic_vector(7 downto 0);
    par_data_i  : in  std_logic_vector(7 downto 0);
    par_hsout_o : out std_logic;
    par_hsout_t : out std_logic;
    par_hsout_i : in  std_logic;
    par_hsin_o  : out std_logic;
    par_hsin_t  : out std_logic;
    par_hsin_i  : in  std_logic;

    -- Debug port
    debug_data      : out std_logic_vector(31 downto 0);
    debug_valid     : out std_logic;

    -- Configuration
    extra_ram       : in  std_logic := '0';
    
    -- memory interface
    mem_req_cpu     : out t_mem_req;
    mem_resp_cpu    : in  t_mem_resp;
    mem_req_disk    : out t_mem_req;
    mem_resp_disk   : in  t_mem_resp;
    mem_busy        : out std_logic;

    -- I/O bus to access WD177x
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic;

    -- Sound
    motor_sound_on  : out std_logic;

    -- drive pins
    power           : in  std_logic;
    drive_address   : in  std_logic_vector(1 downto 0);
    write_prot_n    : in  std_logic;
    byte_ready      : in  std_logic;
    sync            : in  std_logic;
    rdy_n           : in  std_logic;
    disk_change_n   : in  std_logic;
    track_0         : in  std_logic;
    track           : in  unsigned(6 downto 0);

    motor_on        : out std_logic;
    mode            : out std_logic;
    stepper_en      : out std_logic;
    step            : out std_logic_vector(1 downto 0);
    rate_ctrl       : out std_logic_vector(1 downto 0);
    side            : out std_logic;
    two_MHz         : out std_logic;
    
    drv_rdata       : in  std_logic_vector(7 downto 0);
    drv_wdata       : out std_logic_vector(7 downto 0);
    
    power_led       : out std_logic;
    act_led         : out std_logic );
    
end entity;

architecture structural of mm_drive_cpu is
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
    signal cia_port_b_o     : std_logic_vector(7 downto 0);
    signal cia_port_b_t     : std_logic_vector(7 downto 0);
    signal cia_sp_o         : std_logic;
    signal cia_sp_i         : std_logic;
    signal cia_sp_t         : std_logic;
    signal cia_cnt_o        : std_logic;
    signal cia_cnt_i        : std_logic;
    signal cia_cnt_t        : std_logic;
    signal cia_irq          : std_logic;
    signal cia_pc_o         : std_logic;
    signal cia_flag_i       : std_logic;

    signal via1_port_a_o    : std_logic_vector(7 downto 0);
    signal via1_port_a_t    : std_logic_vector(7 downto 0);
    signal via1_ca2_o       : std_logic;
    signal via1_ca2_t       : std_logic;
    signal via1_cb1_o       : std_logic;
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
    signal my_fast_data_out : std_logic;
    signal fast_clk_o_i     : std_logic;
    signal cpu_clk_en       : std_logic;
    signal cpu_rising       : std_logic;
    type   t_mem_state  is (idle, newcycle, extcycle);
    signal mem_state        : t_mem_state;
    signal ext_sel          : std_logic;

    -- "old" style signals
    signal mem_request     : std_logic;
    signal mem_addr        : unsigned(25 downto 0);
    signal mem_rwn         : std_logic;
    signal mem_rack        : std_logic;
    signal mem_dack        : std_logic;
    signal mem_wdata       : std_logic_vector(7 downto 0);

    type t_drive_mode_bundle is record
        -- address decode
        via1_sel        : std_logic;
        via2_sel        : std_logic;
        wd_sel          : std_logic;
        cia_sel         : std_logic;
        open_sel        : std_logic;

        -- internal
        cia_port_a_i    : std_logic_vector(7 downto 0);
        cia_port_b_i    : std_logic_vector(7 downto 0);
        cia_flag_i      : std_logic;
        via1_port_a_i   : std_logic_vector(7 downto 0);
        via1_cb1_i      : std_logic;
        via1_ca2_i      : std_logic;
        fast_ser_dir    : std_logic;
        soe             : std_logic;
        step            : std_logic_vector(1 downto 0);
        stepper_en      : std_logic;
        wd_stepper      : std_logic;

        -- Parallel cable
        par_data_o      : std_logic_vector(7 downto 0);
        par_data_t      : std_logic_vector(7 downto 0);
        par_hsout_o     : std_logic;
        par_hsout_t     : std_logic;
        par_hsin_o      : std_logic;
        par_hsin_t      : std_logic;

        -- export
        side            : std_logic;
        two_MHz         : std_logic;
        act_led         : std_logic;
        power_led       : std_logic;
        motor_on        : std_logic;
        motor_sound_on  : std_logic;
        clk_o           : std_logic;
        data_o          : std_logic;
        atn_o           : std_logic;
    end record;

    type t_drive_mode_bundles is array(natural range <>) of t_drive_mode_bundle;
    signal m    : t_drive_mode_bundles(0 to 2);
    signal mm   : t_drive_mode_bundle;

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
        port_a_i    => mm.via1_port_a_i,
                                
        port_b_o    => via1_port_b_o,
        port_b_t    => via1_port_b_t,
        port_b_i    => via1_port_b_i,
    
        -- handshake pins
        ca1_i       => via1_ca1,
                            
        ca2_o       => via1_ca2_o,
        ca2_i       => mm.via1_ca2_i,
        ca2_t       => via1_ca2_t,
                            
        cb1_o       => via1_cb1_o,
        cb1_i       => mm.via1_cb1_i,
        cb1_t       => via1_cb1_t,
                            
        cb2_o       => via1_cb2_o,
        cb2_i       => via1_cb2_i, -- not used
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
        ca1_i       => so_n,
                            
        ca2_o       => via2_ca2_o,
        ca2_i       => via2_ca2_i, -- used as output (SOE)
        ca2_t       => via2_ca2_t,
                            
        cb1_o       => via2_cb1_o,
        cb1_i       => via2_cb1_i, -- not used
        cb1_t       => via2_cb1_t,
                            
        cb2_o       => via2_cb2_o,
        cb2_i       => via2_cb2_i, -- used as output (MODE)
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
        port_a_i    => mm.cia_port_a_i,
        
        port_b_o    => cia_port_b_o, -- unused
        port_b_t    => cia_port_b_t,
        port_b_i    => mm.cia_port_b_i,
    
        -- serial pin
        sp_o        => cia_sp_o, -- Burst mode IEC data
        sp_i        => cia_sp_i,
        sp_t        => cia_sp_t,
        
        cnt_i       => cia_cnt_i, -- Burst mode IEC clock
        cnt_o       => cia_cnt_o,
        cnt_t       => cia_cnt_t,
        
        pc_o        => cia_pc_o,
        flag_i      => mm.cia_flag_i,
        irq         => cia_irq );

    cpu_irqn   <= not(via1_irq or via2_irq or cia_irq);

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
        
        motor_en     => mm.motor_sound_on,
        stepper_en   => mm.wd_stepper,
        cur_track    => track,
        step         => m(2).step,

        mem_req      => mem_req_disk,
        mem_resp     => mem_resp_disk,

        io_req       => io_req,
        io_resp      => io_resp,
        io_irq       => io_irq
    );

    cpu_clk_en <= falling;
    cpu_rising <= rising;


    mem_busy   <= '0' when mem_state = idle else '1';
    -- Fetch ROM / RAM byte
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
                mem_rwn   <= not cpu_write;
                mem_wdata <= cpu_wdata; 

                if cpu_addr(15) = '1' then -- ROM Area, which is not overridden as RAM
                    if cpu_write = '0' then
                        mem_request <= '1';
                        mem_state <= extcycle;
                    else -- writing to rom -> ignore
                        mem_state  <= idle;
                    end if;
                elsif ext_sel = '1' then -- RAM ONLY!
                    if extra_ram = '0' then
                        if (drive_type = 0) or (drive_type = 1) then
                            mem_addr(14 downto 11) <= "0000"; -- 2K RAM
                        else
                            mem_addr(14 downto 13) <= "00"; -- 8K RAM 
                        end if;    
                    end if;
                    mem_request <= '1';
                    mem_state <= extcycle;
                else
                    mem_state <= idle;
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


    -- Select drive type
    mm <= m(drive_type);

    -- True for all drives
    via1_ren <= mm.via1_sel and not cpu_write;
    via2_ren <= mm.via2_sel and not cpu_write;
    cia_ren  <= mm.cia_sel  and not cpu_write;
    wd_ren   <= mm.wd_sel   and not cpu_write;

    via1_wen <= mm.via1_sel and cpu_write;
    via2_wen <= mm.via2_sel and cpu_write;
    cia_wen  <= mm.cia_sel  and cpu_write;
    wd_wen   <= mm.wd_sel   and cpu_write;

    -- read data muxing
    process(mm.via1_sel, mm.via2_sel, mm.cia_sel, mm.wd_sel, ext_rdata, via1_data, via2_data, cia_data, wd_data)
        variable rdata : std_logic_vector(7 downto 0);
    begin
        ext_sel <= '0';
        rdata := X"FF";
        if mm.via1_sel = '1' then  rdata := rdata and via1_data;  end if;
        if mm.via2_sel = '1' then  rdata := rdata and via2_data;  end if;
        if mm.cia_sel  = '1' then  rdata := rdata and cia_data;   end if;
        if mm.wd_sel   = '1' then  rdata := rdata and wd_data;    end if;
        -- "else"
        if mm.via1_sel = '0' and mm.via2_sel = '0' and mm.cia_sel = '0' and mm.wd_sel = '0' and mm.open_sel = '0' then
            rdata := rdata and ext_rdata;
            ext_sel <= '1';
        end if;
        cpu_rdata <= rdata;
    end process;

    -- DRIVE SPECIFICS

    -- Address decoding 1541
    m(0).via1_sel <= '1' when cpu_addr(12 downto 10)="110" and cpu_addr(15)='0' and (extra_ram='0' or cpu_addr(14 downto 13)="00") else '0';
    m(0).via2_sel <= '1' when cpu_addr(12 downto 10)="111" and cpu_addr(15)='0' and (extra_ram='0' or cpu_addr(14 downto 13)="00") else '0';
    m(0).open_sel <= '1' when (cpu_addr(12) xor cpu_addr(11)) = '1' and cpu_addr(15) = '0' and extra_ram = '0' else '0';
    m(0).cia_sel  <= '0';
    m(0).wd_sel   <= '0';
    
    -- Address decoding 1571
    m(1).via1_sel <= '1' when cpu_addr(15 downto 10)="000110" else '0';
    m(1).via2_sel <= '1' when cpu_addr(15 downto 10)="000111" else '0';
    m(1).open_sel <= '1' when (cpu_addr(15 downto 11)="00001" or cpu_addr(15 downto 11) = "00010") and extra_ram = '0' else '0';
    m(1).cia_sel  <= '1' when cpu_addr(15 downto 14)="01" else '0';
    m(1).wd_sel   <= '1' when cpu_addr(15 downto 13)="001" else '0';

    -- Address decoding 1581
    m(2).via1_sel <= '0';
    m(2).via2_sel <= '0';
    m(2).open_sel <= '1' when cpu_addr(15 downto 13)="001" and extra_ram = '0' else '0'; -- 2000
    m(2).cia_sel  <= '1' when cpu_addr(15 downto 13)="010" else '0'; -- 4000
    m(2).wd_sel   <= '1' when cpu_addr(15 downto 13)="011" else '0'; -- 6000

    -- Control signals 1541
    m(0).side         <= '0';
    m(0).two_MHz      <= '0';
    m(0).fast_ser_dir <= '0'; -- by setting this to input, the fast_clk_o is never driven, and because the CIA is never
                              -- selected, receiving fast_clk_i will never cause issues
    m(0).soe          <= via2_ca2_i;
    m(0).cia_port_a_i <= cia_port_a_o or not cia_port_a_t; -- don't care
    m(0).cia_port_b_i <= cia_port_b_o or not cia_port_b_t; -- don't care
    m(0).cia_flag_i   <= '1';
    m(0).power_led    <= '1';
    
    -- Control signals 1571
    m(1).side         <= m(1).via1_port_a_i(2);
    m(1).two_MHz      <= m(1).via1_port_a_i(5);
    m(1).fast_ser_dir <= m(1).via1_port_a_i(1);
    m(1).soe          <= via2_ca2_i;
    m(1).cia_port_a_i <= cia_port_a_o or not cia_port_a_t; -- CIA ports are not used
    -- m(1).cia_port_b_i <= cia_port_b_o or not cia_port_b_t; -- CIA ports are not used
    m(1).power_led    <= '1';
    
    -- Control signals 1581
    m(2).side         <= m(2).cia_port_a_i(0);
    m(2).two_MHz      <= '1';
    m(2).fast_ser_dir <= m(2).cia_port_b_i(5);
    m(2).soe          <= '0';

-----------------------------------------
-- 1581 section
-----------------------------------------
    b_1581: block
        signal atn_ack      : std_logic;
        signal my_data_out  : std_logic;
        signal clock_out    : std_logic;
    begin
        m(2).cia_port_b_i(7) <= not atn_i;    -- assume that this signal (from 74LS14) wins
        m(2).cia_port_b_i(6) <= (cia_port_b_o(6) or not cia_port_b_t(6)) and write_prot_n; 
        m(2).cia_port_b_i(5) <= (cia_port_b_o(5) or not cia_port_b_t(5));
        m(2).cia_port_b_i(4) <= (cia_port_b_o(4) or not cia_port_b_t(4));
        m(2).cia_port_b_i(3) <= (cia_port_b_o(3) or not cia_port_b_t(3));
        m(2).cia_port_b_i(2) <= not clk_i;    -- assume that this signal (from 74LS14) wins
        m(2).cia_port_b_i(1) <= (cia_port_b_o(1) or not cia_port_b_t(1));
        m(2).cia_port_b_i(0) <= not data_i;
    
        m(2).cia_port_a_i(7) <= (cia_port_a_o(7) or not cia_port_a_t(7)) and disk_change_n;
        m(2).cia_port_a_i(6) <= (cia_port_a_o(6) or not cia_port_a_t(6));
        m(2).cia_port_a_i(5) <= (cia_port_a_o(5) or not cia_port_a_t(5));
        m(2).cia_port_a_i(4) <= (cia_port_a_o(4) or not cia_port_a_t(4)) and drive_address(1);   
        m(2).cia_port_a_i(3) <= (cia_port_a_o(3) or not cia_port_a_t(3)) and drive_address(0);   
        m(2).cia_port_a_i(2) <= (cia_port_a_o(2) or not cia_port_a_t(2));
        m(2).cia_port_a_i(1) <= (cia_port_a_o(1) or not cia_port_a_t(1)) and rdy_n;
        m(2).cia_port_a_i(0) <= (cia_port_a_o(0) or not cia_port_a_t(0)); 
    
        m(2).cia_flag_i   <= atn_i;  -- active low atn signal

        m(2).data_o <= not my_data_out and not (atn_ack and not atn_i) and my_fast_data_out;
        m(2).clk_o  <= not clock_out;
        m(2).atn_o  <= '1';

        -- Parallel Cable not defined
        m(2).par_data_o       <= X"FF";
        m(2).par_data_t       <= X"00";
        m(2).par_hsout_o      <= '1';
        m(2).par_hsout_t      <= '0';
        m(2).par_hsin_o       <= '1';
        m(2).par_hsin_t       <= '0';

        -- write_prot_n_i  <= cia_port_b_i(6);
        atn_ack         <= m(2).cia_port_b_i(4);
        clock_out       <= m(2).cia_port_b_i(3);
        my_data_out     <= m(2).cia_port_b_i(1);
        
        --disk_change_n_i     <= cia_port_a_i(7);
        m(2).act_led        <= m(2).cia_port_a_i(6);
        m(2).power_led      <= m(2).cia_port_a_i(5);
        --drive_address_i(1)  <= cia_port_a_i(4);
        --drive_address_i(0)  <= cia_port_a_i(3);
        m(2).motor_sound_on <= not m(2).cia_port_a_i(2);
        --rdy_n_i             <= cia_port_a_i(1);
        --side_0_i            <= cia_port_a_i(0);
    end block;

    -- correctly attach the VIA pins to the outside world
    via1_ca1         <= not atn_i;
    via1_cb2_i       <= via1_cb2_o or not via1_cb2_t;
    via2_cb1_i       <= via2_cb1_o or not via2_cb1_t;
    via2_cb2_i       <= via2_cb2_o or not via2_cb2_t;
    via2_ca2_i       <= via2_ca2_o or not via2_ca2_t;
 
    -- Via Port A is used in the 1541 for the parallel interface (SpeedDos / DolphinDos), but in the 1571 some of the pins are connected internally
    m(0).via1_cb1_i       <= par_hsin_i;
    m(1).via1_cb1_i       <= via1_cb1_o or not via1_cb1_t;
    m(2).via1_cb1_i       <= via1_cb1_o or not via1_cb1_t;
    
    m(0).par_data_o       <= via1_port_a_o;
    m(0).par_data_t       <= via1_port_a_t;
    m(0).par_hsout_o      <= via1_ca2_o;
    m(0).par_hsout_t      <= via1_ca2_t;
    m(0).par_hsin_o       <= via1_cb1_o;
    m(0).par_hsin_t       <= via1_cb1_t;
        
    m(0).via1_port_a_i    <= par_data_i;

    m(1).via1_port_a_i(7) <= (via1_port_a_o(7) or not via1_port_a_t(7)) and so_n; -- Byte ready in schematic. Our byte_ready signal is not yet masked with so_e
    m(1).via1_port_a_i(6) <= (via1_port_a_o(6) or not via1_port_a_t(6)); -- ATN OUT (not connected)
    m(1).via1_port_a_i(5) <= (via1_port_a_o(5) or not via1_port_a_t(5)); -- 2 MHz mode
    m(1).via1_port_a_i(4) <= (via1_port_a_o(4) or not via1_port_a_t(4));
    m(1).via1_port_a_i(3) <= (via1_port_a_o(3) or not via1_port_a_t(3));
    m(1).via1_port_a_i(2) <= (via1_port_a_o(2) or not via1_port_a_t(2)); -- SIDE
    m(1).via1_port_a_i(1) <= (via1_port_a_o(1) or not via1_port_a_t(1)); -- SER_DIR
    m(1).via1_port_a_i(0) <= not track_0; -- assuming that the LS14 always wins

    m(2).via1_port_a_i    <= X"FF"; -- Don't care. 
    
    m(0).via1_ca2_i  <= par_hsout_i;
    m(1).via1_ca2_i  <= write_prot_n;    -- only in 1571
    m(2).via1_ca2_i  <= '1';

    -- Do the same for VIA 2. Port A should read the pin, Port B reads the output internally, so for port B only actual input signals should be connected    
    via2_port_b_i(7) <= sync;
    via2_port_b_i(6) <= '1'; --Density
    via2_port_b_i(5) <= '1'; --Density
    via2_port_b_i(4) <= write_prot_n;    
    via2_port_b_i(3) <= '1'; -- LED
    via2_port_b_i(2) <= '1'; -- Motor
    via2_port_b_i(1) <= '1'; -- Step
    via2_port_b_i(0) <= '1'; -- Step   
    
    b1541_1571: block
        signal atn_ack          : std_logic;
        signal my_clk_out       : std_logic;
        signal my_data_out      : std_logic;
    begin
        atn_ack     <= via1_port_b_o(4) or not via1_port_b_t(4);
        my_data_out <= via1_port_b_o(1) or not via1_port_b_t(1);
        my_clk_out  <= via1_port_b_o(3) or not via1_port_b_t(3);
    
        -- Serial bus pins 1541
        m(0).data_o <= not my_data_out and (not (atn_ack xor (not atn_i)));
        m(0).clk_o  <= not my_clk_out;
        m(0).atn_o  <= '1';

        -- Serial bus pins 1571
        m(1).data_o <= not my_data_out and my_fast_data_out and (not (atn_ack xor (not atn_i)));
        m(1).clk_o  <= not my_clk_out;
        m(1).atn_o  <= '1';

        -- Because Port B reads its own output when set to output, we do not need to consider the direction here
        via1_port_b_i(7) <= not atn_i;
        via1_port_b_i(6) <= drive_address(1); -- drive select
        via1_port_b_i(5) <= drive_address(0); -- drive select;
        via1_port_b_i(4) <= '1'; -- atn a     - PUP
        via1_port_b_i(3) <= '1'; -- clock out - PUP
        via1_port_b_i(2) <= not (clk_i and not my_clk_out);
        via1_port_b_i(1) <= '1'; -- data out  - PUP
        via1_port_b_i(0) <= not (data_i and not my_data_out and (not (atn_ack xor (not atn_i))));

        -- Parallel Cable connects to 6526 port B on a 1571
        m(1).cia_port_b_i     <= par_data_i;
        m(1).cia_flag_i       <= par_hsin_i;
        
        m(1).par_data_o       <= cia_port_b_o;
        m(1).par_data_t       <= cia_port_b_t;
        m(1).par_hsout_o      <= cia_pc_o;
        m(1).par_hsout_t      <= '1'; -- PC is always output
        m(1).par_hsin_o       <= '1';
        m(1).par_hsin_t       <= '0'; -- FLAG is always input
    end block;

    m(0).act_led  <= (via2_port_b_o(3) or not via2_port_b_t(3));
    m(1).act_led  <= (via2_port_b_o(3) or not via2_port_b_t(3));
    m(0).motor_on <= (via2_port_b_o(2) or not via2_port_b_t(2));
    m(1).motor_on <= (via2_port_b_o(2) or not via2_port_b_t(2));
    m(2).motor_on <= '0'; -- disable memory access to GCR memory
    m(0).motor_sound_on <= m(0).motor_on;
    m(1).motor_sound_on <= m(1).motor_on;
    m(0).step(0)        <= via2_port_b_o(0) or not via2_port_b_t(0);
    m(0).step(1)        <= via2_port_b_o(1) or not via2_port_b_t(1);
    m(1).step(0)        <= via2_port_b_o(0) or not via2_port_b_t(0);
    m(1).step(1)        <= via2_port_b_o(1) or not via2_port_b_t(1);
    m(0).wd_stepper     <= '0';
    m(1).wd_stepper     <= '0';
    m(2).wd_stepper     <= '1';
    m(0).stepper_en     <= m(0).motor_on;
    m(1).stepper_en     <= m(1).motor_on;
    m(2).stepper_en     <= '1';
        
    mode         <= via2_cb2_i; -- don't care for 1581
    rate_ctrl(0) <= via2_port_b_o(5) or not via2_port_b_t(5); -- don't care for 1581
    rate_ctrl(1) <= via2_port_b_o(6) or not via2_port_b_t(6); -- don't care for 1581
    so_n         <= byte_ready or not mm.soe; -- soe will be '0' for 1581

    -- This applies to 1571 and 1581
    -- my_fast_data_out and fast_clk_o will be '1' for 1541, because fast_ser_dir is defined as '0' for 1541.
    my_fast_data_out  <= (cia_sp_o or not cia_sp_t) or not mm.fast_ser_dir; -- active low!
    cia_sp_i          <= (cia_sp_o or not cia_sp_t) when mm.fast_ser_dir = '1' else
                         data_i;
    
    fast_clk_o_i      <= (cia_cnt_o or not cia_cnt_t) or not mm.fast_ser_dir; -- active low!
    cia_cnt_i         <= (cia_cnt_o or not cia_cnt_t) when mm.fast_ser_dir = '1' else -- output
                         fast_clk_i; -- assume that 74LS241 wins 

    -- Export Inside drive
    stepper_en      <= mm.stepper_en;
    step            <= mm.step;
    side            <= mm.side;
    two_MHz         <= mm.two_MHz;

    -- Export to outside world
    motor_sound_on  <= power and mm.motor_sound_on;   -- internally active high, externally active high
    motor_on        <= power and mm.motor_on;         -- internally active high, externally active high
    power_led       <= not (power and mm.power_led);  -- internally active high, externally active low
    act_led         <= not (power and mm.act_led);    -- internally active high, externally active low
    clk_o           <= not power or mm.clk_o;         -- internally active low,  externally active low
    data_o          <= not power or mm.data_o;        -- internally active low,  externally active low
    atn_o           <= not power or mm.atn_o;         -- internally active low,  externally active low
    fast_clk_o      <= not power or fast_clk_o_i;     -- internally active low,  externally active low
    
    -- Parallel cable out
    par_data_o      <= mm.par_data_o;
    par_data_t      <= mm.par_data_t;
    par_hsout_o     <= mm.par_hsout_o;
    par_hsout_t     <= mm.par_hsout_t;
    par_hsin_o      <= mm.par_hsin_o;
    par_hsin_t      <= mm.par_hsin_t;

end architecture;
