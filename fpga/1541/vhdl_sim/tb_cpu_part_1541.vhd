library ieee;
use ieee.std_logic_1164.all;

library work;
use work.iec_bus_bfm_pkg.all;

entity tb_cpu_part_1541 is
end tb_cpu_part_1541;

architecture tb of tb_cpu_part_1541 is

    signal clock           : std_logic := '0';
    signal clock_en        : std_logic;
    signal reset           : std_logic;
    signal atn_o           : std_logic; -- open drain
    signal atn_i           : std_logic := '1';
    signal clk_o           : std_logic; -- open drain
    signal clk_i           : std_logic := '1';    
    signal data_o          : std_logic; -- open drain
    signal data_i          : std_logic := '1';
    signal motor_on        : std_logic;
    signal mode            : std_logic;
    signal write_prot_n    : std_logic := '1';
    signal step            : std_logic_vector(1 downto 0);
    signal soe             : std_logic;
    signal rate_ctrl       : std_logic_vector(1 downto 0);
    signal byte_ready      : std_logic := '1';
    signal sync            : std_logic := '1';
    signal drv_rdata       : std_logic_vector(7 downto 0) := X"FF";
    signal drv_wdata       : std_logic_vector(7 downto 0);
    signal act_led         : std_logic;

    signal iec_clock       : std_logic;
    signal iec_data        : std_logic;
    signal iec_atn         : std_logic;

begin

    clock <= not clock after 125 ns;
    reset <= '1', '0' after 2 us;

    ce: process
    begin
        clock_en <= '0';
        wait until clock='1';
        wait until clock='1';
        wait until clock='1';
        clock_en <= '1';
        wait until clock='1';
    end process;

    mut: entity work.cpu_part_1541
    port map (
        clock       => clock,
        clock_en    => clock_en,
        reset       => reset,
        
        -- serial bus pins
        atn_o       => atn_o, -- open drain
        atn_i       => atn_i,
    
        clk_o       => clk_o, -- open drain
        clk_i       => clk_i,    
    
        data_o      => data_o, -- open drain
        data_i      => data_i,
        
        -- drive pins
        power           => '1',
        drive_select    => "00",
        motor_on        => motor_on,
        mode            => mode,
        write_prot_n    => write_prot_n,
        step            => step,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        byte_ready      => byte_ready,
        sync            => sync,
        
        drv_rdata       => drv_rdata,
        drv_wdata       => drv_wdata,
    
        -- other
        act_led         => act_led );

    
    -- open collector logic
    clk_i  <= iec_clock and '1';
    data_i <= iec_data  and '1';
    atn_i  <= iec_atn   and '1';

    iec_clock <= '0' when clk_o='0'  else 'H';
    iec_data  <= '0' when data_o='0' else 'H';
    iec_atn   <= '0' when atn_o='0'  else 'H';

    iec_bfm: entity work.iec_bus_bfm
    port map (
        iec_clock   => iec_clock,
        iec_data    => iec_data,
        iec_atn     => iec_atn );

    test: process
        variable bfm : p_iec_bus_bfm_object;
        variable msg : t_iec_message;
    begin
        bind_iec_bus_bfm(":tb_cpu_part_1541:iec_bfm:", bfm);

--        wait for 1250 ms;  -- unpatched ROM
        wait for 30 ms;      -- patched ROM

        iec_send_atn(bfm, X"48"); -- Drive 8, Talk, I will listen
        iec_send_atn(bfm, X"6F"); -- Open channel 15
        iec_turnaround(bfm);      -- start to listen
        iec_get_message(bfm, msg);
        iec_print_message(msg);
        wait;                
        
    end process;

end tb;
