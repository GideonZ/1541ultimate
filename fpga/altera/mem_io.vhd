--------------------------------------------------------------------------------
-- Entity: mem_io
-- Date:2016-07-16  
-- Author: Gideon     
--
-- Description: All Altera specific I/O stuff for DDR(2)
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library altera_mf;
use altera_mf.altera_mf_components.all;

entity mem_io is
    generic (
        g_data_width        : natural := 4;
        g_addr_cmd_width    : natural := 8 );
	port (
		ref_clock     : in  std_logic;
		ref_reset     : in  std_logic;

        sys_clock     : out std_logic;
        sys_reset     : out std_logic;

        user_clock_1    : out std_logic := '0';
        user_clock_2    : out std_logic := '0';

        phasecounterselect : in  std_logic_vector(2 downto 0);
        phasestep          : in  std_logic;
        phaseupdown        : in  std_logic;
        phasedone          : out std_logic;
        mode               : in  std_logic_vector(1 downto 0);
        measurement        : out std_logic_vector(11 downto 0);
                
        addr_first         : in    std_logic_vector(g_addr_cmd_width-1 downto 0);
        addr_second        : in    std_logic_vector(g_addr_cmd_width-1 downto 0);
        wdata              : in    std_logic_vector(4*g_data_width-1 downto 0); 
        wdata_oe           : in    std_logic := '0';
        rdata              : out   std_logic_vector(4*g_data_width-1 downto 0);
        
		mem_clk_p          : inout std_logic := 'Z';
		mem_clk_n          : inout std_logic := 'Z';
		mem_addr           : out   std_logic_vector(g_addr_cmd_width-1 downto 0);
        mem_dqs            : inout std_logic := 'Z';
		mem_dq             : inout std_logic_vector(g_data_width-1 downto 0)
    );
end entity;

architecture arch of mem_io is
    signal sys_clock_pll    : std_logic;
    signal sys_clock_i      : std_logic;
    signal sys_reset_pipe   : std_logic_vector(3 downto 0);
    signal pll_locked       : std_logic;
    signal mem_measure_clock: std_logic;
    signal mem_resync_clock : std_logic;
    signal mem_addr_clock   : std_logic;
    signal mem_write_clock  : std_logic;
    signal mem_read_clock   : std_logic;
    signal not_sys_clock    : std_logic;
    signal not_addr_clock   : std_logic;
    
    signal wdata_r          : std_logic_vector(4*g_data_width-1 downto 0);
    signal wdata_oe_r       : std_logic;
    signal wdata_oe_r2      : std_logic;
    
    signal wdata_half       : std_logic_vector(2*g_data_width-1 downto 0);
    signal wdata_mux        : std_logic;
    signal mux_reset        : std_logic;
    signal rdata_h          : std_logic_vector(g_data_width-1 downto 0);
    signal rdata_l          : std_logic_vector(g_data_width-1 downto 0);

    signal rdata_r1         : std_logic_vector(2*g_data_width-1 downto 0);
    signal rdata_r2         : std_logic_vector(2*g_data_width-1 downto 0);
    signal rdata_r3         : std_logic_vector(2*g_data_width-1 downto 0);
    signal rdata_f1         : std_logic_vector(2*g_data_width-1 downto 0);
    signal rdata_f2         : std_logic_vector(2*g_data_width-1 downto 0);
    signal rdata_f3         : std_logic_vector(2*g_data_width-1 downto 0);
    
    signal dqs_oe           : std_logic;
    signal mode_r           : std_logic_vector(1 downto 0);
    signal measure_h        : std_logic;
    signal measure_l        : std_logic;
    
    
    COMPONENT altpll
    GENERIC (
        bandwidth_type      : STRING;
        clk0_divide_by      : NATURAL;
        clk0_duty_cycle     : NATURAL;
        clk0_multiply_by        : NATURAL;
        clk0_phase_shift        : STRING;
        clk1_divide_by      : NATURAL;
        clk1_duty_cycle     : NATURAL;
        clk1_multiply_by        : NATURAL;
        clk1_phase_shift        : STRING;
        clk2_divide_by      : NATURAL;
        clk2_duty_cycle     : NATURAL;
        clk2_multiply_by        : NATURAL;
        clk2_phase_shift        : STRING;
        clk3_divide_by      : NATURAL;
        clk3_duty_cycle     : NATURAL;
        clk3_multiply_by        : NATURAL;
        clk3_phase_shift        : STRING;
        clk4_divide_by      : NATURAL;
        clk4_duty_cycle     : NATURAL;
        clk4_multiply_by        : NATURAL;
        clk4_phase_shift        : STRING;
        compensate_clock        : STRING;
        inclk0_input_frequency      : NATURAL;
        intended_device_family      : STRING;
        lpm_type        : STRING;
        operation_mode      : STRING;
        pll_type        : STRING;
        port_activeclock        : STRING;
        port_areset     : STRING;
        port_clkbad0        : STRING;
        port_clkbad1        : STRING;
        port_clkloss        : STRING;
        port_clkswitch      : STRING;
        port_configupdate       : STRING;
        port_fbin       : STRING;
        port_inclk0     : STRING;
        port_inclk1     : STRING;
        port_locked     : STRING;
        port_pfdena     : STRING;
        port_phasecounterselect     : STRING;
        port_phasedone      : STRING;
        port_phasestep      : STRING;
        port_phaseupdown        : STRING;
        port_pllena     : STRING;
        port_scanaclr       : STRING;
        port_scanclk        : STRING;
        port_scanclkena     : STRING;
        port_scandata       : STRING;
        port_scandataout        : STRING;
        port_scandone       : STRING;
        port_scanread       : STRING;
        port_scanwrite      : STRING;
        port_clk0       : STRING;
        port_clk1       : STRING;
        port_clk2       : STRING;
        port_clk3       : STRING;
        port_clk4       : STRING;
        port_clk5       : STRING;
        port_clkena0        : STRING;
        port_clkena1        : STRING;
        port_clkena2        : STRING;
        port_clkena3        : STRING;
        port_clkena4        : STRING;
        port_clkena5        : STRING;
        port_extclk0        : STRING;
        port_extclk1        : STRING;
        port_extclk2        : STRING;
        port_extclk3        : STRING;
        self_reset_on_loss_lock     : STRING;
        vco_frequency_control       : STRING;
        vco_phase_shift_step        : NATURAL;
        width_clock     : NATURAL;
        width_phasecounterselect        : NATURAL
    );
    PORT (
            areset  : IN STD_LOGIC ;
            inclk   : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
            phasecounterselect  : IN STD_LOGIC_VECTOR (2 DOWNTO 0);
            phasestep   : IN STD_LOGIC ;
            phaseupdown : IN STD_LOGIC ;
            scanclk : IN STD_LOGIC ;
            clk : OUT STD_LOGIC_VECTOR (4 DOWNTO 0);
            locked  : OUT STD_LOGIC ;
            phasedone   : OUT STD_LOGIC 
    );
    END COMPONENT;
begin
    i_pll : altpll
    generic map (
        bandwidth_type => "AUTO",
        clk0_divide_by => 4,
        clk0_duty_cycle => 50,
        clk0_multiply_by => 5,
        clk0_phase_shift => "0",
        clk1_divide_by => 2,
        clk1_duty_cycle => 50,
        clk1_multiply_by => 5,
        clk1_phase_shift => "0",
        clk2_divide_by => 2,
        clk2_duty_cycle => 50,
        clk2_multiply_by => 5,
        clk2_phase_shift => "2000",
        clk3_divide_by => 2,
        clk3_duty_cycle => 50,
        clk3_multiply_by => 5,
        clk3_phase_shift => "4480", -- was 2000 for zero delay design
        clk4_divide_by => 2,
        clk4_duty_cycle => 50,
        clk4_multiply_by => 5,
        clk4_phase_shift => "0",
        compensate_clock => "CLK1",
        inclk0_input_frequency => 20000,
        intended_device_family => "Cyclone IV E",
        lpm_type => "altpll",
        operation_mode => "NORMAL",
        pll_type => "AUTO",
        port_activeclock => "PORT_UNUSED",
        port_areset => "PORT_USED",
        port_clkbad0 => "PORT_UNUSED",
        port_clkbad1 => "PORT_UNUSED",
        port_clkloss => "PORT_UNUSED",
        port_clkswitch => "PORT_UNUSED",
        port_configupdate => "PORT_UNUSED",
        port_fbin => "PORT_UNUSED",
        port_inclk0 => "PORT_USED",
        port_inclk1 => "PORT_UNUSED",
        port_locked => "PORT_USED",
        port_pfdena => "PORT_UNUSED",
        port_phasecounterselect => "PORT_USED",
        port_phasedone => "PORT_USED",
        port_phasestep => "PORT_USED",
        port_phaseupdown => "PORT_USED",
        port_pllena => "PORT_UNUSED",
        port_scanaclr => "PORT_UNUSED",
        port_scanclk => "PORT_USED",
        port_scanclkena => "PORT_UNUSED",
        port_scandata => "PORT_UNUSED",
        port_scandataout => "PORT_UNUSED",
        port_scandone => "PORT_UNUSED",
        port_scanread => "PORT_UNUSED",
        port_scanwrite => "PORT_UNUSED",
        port_clk0 => "PORT_USED",
        port_clk1 => "PORT_USED",
        port_clk2 => "PORT_USED",
        port_clk3 => "PORT_USED",
        port_clk4 => "PORT_USED",
        port_clk5 => "PORT_UNUSED",
        port_clkena0 => "PORT_UNUSED",
        port_clkena1 => "PORT_UNUSED",
        port_clkena2 => "PORT_UNUSED",
        port_clkena3 => "PORT_UNUSED",
        port_clkena4 => "PORT_UNUSED",
        port_clkena5 => "PORT_UNUSED",
        port_extclk0 => "PORT_UNUSED",
        port_extclk1 => "PORT_UNUSED",
        port_extclk2 => "PORT_UNUSED",
        port_extclk3 => "PORT_UNUSED",
        self_reset_on_loss_lock => "OFF",
        vco_frequency_control => "MANUAL_PHASE",
        vco_phase_shift_step => 80,
        width_clock => 5,
        width_phasecounterselect => 3
    )
    port map (
        areset    => ref_reset,
        inclk(1)  => '0',
        inclk(0)  => ref_clock,
        phasecounterselect => phasecounterselect,
        phasestep => phasestep,
        phaseupdown => phaseupdown,
        scanclk => sys_clock_i,
        clk(0) => sys_clock_pll,
        clk(1) => mem_addr_clock,
        clk(2) => mem_write_clock,
        clk(3) => mem_read_clock,
        clk(4) => mem_measure_clock,
        locked => pll_locked,
        phasedone => phasedone
    );

    sys_clock   <= sys_clock_pll;
    sys_clock_i <= sys_clock_pll;
    
    process(sys_clock_i, pll_locked)
    begin
        if pll_locked = '0' then
            sys_reset_pipe <= (others => '1');
        elsif rising_edge(sys_clock_i) then
            sys_reset_pipe <= '0' & sys_reset_pipe(sys_reset_pipe'high downto 1);
        end if;
    end process;
    sys_reset <= sys_reset_pipe(0);

    i_clk_p: altddio_bidir
    generic map (
        extend_oe_disable => "UNUSED",
        implement_input_in_lcell => "UNUSED",
        intended_device_family => "Cyclone IV E",
        invert_output => "OFF",
        lpm_type => "altddio_bidir",
        oe_reg => "UNUSED",
        power_up_high => "OFF",
        width => 1
    ) port map (
        padio(0)  => mem_clk_p,
        outclock  => mem_addr_clock,
        inclock   => mem_measure_clock,
        oe        => '1',
        datain_h  => "0",
        datain_l  => "1",
        dataout_h(0) => measure_h,
        dataout_l(0) => measure_l,
        combout   => open,
        dqsundelayedout => open,
        outclocken  => '1',
        sclr        => '0',
        sset        => '0'
    );

    i_clk_n: altddio_bidir
    generic map (
        extend_oe_disable => "UNUSED",
        implement_input_in_lcell => "UNUSED",
        intended_device_family => "Cyclone IV E",
        invert_output => "OFF",
        lpm_type => "altddio_bidir",
        oe_reg => "UNUSED",
        power_up_high => "OFF",
        width => 1
    ) port map (
        padio(0)  => mem_clk_n,
        outclock  => mem_addr_clock,
        inclock   => mem_measure_clock,
        oe        => '1',
        datain_h  => "1",
        datain_l  => "0",
        dataout_h => open,
        dataout_l => open,
        combout   => open,
        dqsundelayedout => open,
        outclocken  => '1',
        sclr        => '0',
        sset        => '0'
    );

    not_sys_clock <= not sys_clock_i;
    
    b_measure: block
        signal measure_l_r  : std_logic;
        signal measure_h_r  : std_logic;
        signal count        : unsigned(5 downto 0) := (others => '0');
        signal value_h      : unsigned(5 downto 0) := (others => '0');
        signal value_l      : unsigned(5 downto 0) := (others => '0');
        signal count_h      : unsigned(5 downto 0) := (others => '0');
        signal count_l      : unsigned(5 downto 0) := (others => '0');
        signal new_values   : std_logic := '0';
    begin
        process(mem_measure_clock)
        begin
            if rising_edge(mem_measure_clock) then
                measure_l_r <= measure_l;
                measure_h_r <= measure_h;
                count <= count + 1;
                new_values <= '0';
                if signed(count) = -1 then
                    value_h <= count_h; 
                    count_h <= (others => '0');
                    value_l <= count_l; 
                    count_l <= (others => '0');
                    new_values <= '1';
                else
                    if measure_l_r = '1' then
                        count_l <= count_l + 1;
                    end if;
                    if measure_h_r = '1' then
                        count_h <= count_h + 1;
                    end if;
                end if;
            end if;
        end process;

        i_sync: entity work.synchronizer_gzw
        generic map(
            g_width     => 12,
            g_fast      => false
        )
        port map (
            tx_clock    => mem_measure_clock,
            tx_push     => new_values,
            tx_data(11 downto 6) => std_logic_vector(value_h),
            tx_data( 5 downto 0) => std_logic_vector(value_l),
            tx_done     => open,
            rx_clock    => sys_clock_i,
            rx_new_data => open,
            rx_data     => measurement
        );
        

    end block;

    i_addr: altddio_out 
    generic map (
        extend_oe_disable      => "UNUSED",
        intended_device_family => "Cyclone IV E",
        lpm_hint               => "UNUSED",
        lpm_type               => "altddio_out",
        oe_reg                 => "UNUSED",
        power_up_high          => "ON",
        width                  => mem_addr'length 
    ) port map (
        aset                   => sys_reset_pipe(0),
        datain_h               => addr_first,
        datain_l               => addr_second,
        dataout                => mem_addr,
        oe                     => '1',
        outclock               => not_sys_clock,
        outclocken             => '1'
    );

    process(mem_write_clock)
    begin
        if rising_edge(mem_write_clock) then
            wdata_mux <= not wdata_mux and not mux_reset;
        end if;
        if falling_edge(mem_write_clock) then
            mux_reset <= sys_reset_pipe(0);            
            wdata_r <= wdata;
            wdata_oe_r <= wdata_oe;
        end if;
        if rising_edge(mem_write_clock) then
            wdata_oe_r2 <= wdata_oe_r;
        end if;
    end process;

    wdata_half <= wdata_r(2*g_data_width-1 downto 0) when wdata_mux='0' else
                  wdata_r(4*g_data_width-1 downto 2*g_data_width);
                  

    i_data: altddio_bidir
    generic map (
        extend_oe_disable => "UNUSED",
        implement_input_in_lcell => "UNUSED",
        intended_device_family => "Cyclone IV E",
        invert_output => "OFF",
        lpm_type => "altddio_bidir",
        oe_reg => "REGISTERED",
        power_up_high => "OFF",
        width => g_data_width
    ) port map (
        padio     => mem_dq,
        outclock  => mem_write_clock,
        inclock   => mem_read_clock,
        oe        => dqs_oe, -- wdata_oe_r,
        datain_h  => wdata_half(g_data_width-1 downto 0),
        datain_l  => wdata_half(2*g_data_width-1 downto g_data_width),
        dataout_h => rdata_h,
        dataout_l => rdata_l,
        combout   => open,
        dqsundelayedout => open,
        outclocken  => '1',
        sclr        => '0',
        sset        => '0'
    );

    
--    i_dqs_oe: altddio_out 
--    generic map (
--        extend_oe_disable      => "UNUSED",
--        intended_device_family => "Cyclone IV E",
--        lpm_hint               => "UNUSED",
--        lpm_type               => "altddio_out",
--        oe_reg                 => "UNUSED",
--        power_up_high          => "OFF",
--        width                  => 1 
--    ) port map (
--        aset                   => sys_reset_pipe(0),
--        datain_h(0)            => dqs_oe_half(0),
--        datain_l(0)            => dqs_oe_half(1),
--        dataout(0)             => dqs_oe,
--        oe                     => '1',
--        outclock               => mem_write_clock,
--        outclocken             => '1'
--    );


    i_dqs: altddio_out 
    generic map (
        extend_oe_disable      => "UNUSED",
        intended_device_family => "Cyclone IV E",
        lpm_hint               => "UNUSED",
        lpm_type               => "altddio_out",
        oe_reg                 => "REGISTERED",
        power_up_high          => "OFF",
        width                  => 1 
    ) port map (
        aset                   => sys_reset_pipe(0),
        datain_h(0)            => wdata_oe_r,
        datain_l(0)            => '0',
        dataout(0)             => mem_dqs,
        oe                     => dqs_oe,
        outclock               => not_addr_clock,
        outclocken             => '1'
    );
    not_addr_clock <= not mem_addr_clock;
    
    process(mem_read_clock)
    begin
        if falling_edge(mem_read_clock) then
            rdata_f1 <= rdata_h & rdata_l;
            rdata_f2 <= rdata_f1;
            rdata_f3 <= rdata_f2;
        end if;
    end process;

    rdata_r1 <= rdata_h & rdata_l;
    
    process(mem_read_clock)
    begin
        if rising_edge(mem_read_clock) then
            rdata_r2 <= rdata_r1;
            rdata_r3 <= rdata_r2;
        end if;
    end process;

    process(sys_clock_i)
    begin
        if rising_edge(sys_clock_i) then
            mode_r <= mode;
            case mode_r is
            when "00" =>
                rdata <= rdata_r1 & rdata_r2;
            when "01" =>
                rdata <= rdata_f1 & rdata_f2;
            when "10" =>
                rdata <= rdata_r2 & rdata_r3;
            when "11" =>
                rdata <= rdata_f2 & rdata_f3;
            when others =>
                rdata <= (others => '0');
            end case;
        end if;
    end process;
    
    dqs_oe <= wdata_oe or wdata_oe_r; --dqs_oe_r or dqs_oe_r2;
end architecture;
