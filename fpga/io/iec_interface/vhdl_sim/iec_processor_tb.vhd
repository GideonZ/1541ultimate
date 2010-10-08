library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;


library work;
use work.io_bus_pkg.all;

entity iec_processor_tb is

end iec_processor_tb;

architecture tb of iec_processor_tb is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal req             : t_io_req;
    signal resp            : t_io_resp;
    signal clk_o           : std_logic;
    signal data_o          : std_logic;
    signal atn_o           : std_logic;
    signal srq_o           : std_logic;
    signal iec_clock       : std_logic;
    signal iec_data        : std_logic;
    signal iec_atn         : std_logic;
    
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.iec_processor_io
    port map (
        clock           => clock,
        reset           => reset,
        req             => req,
        resp            => resp,
        clk_o           => clk_o,
        clk_i           => iec_clock,
        data_o          => data_o,
        data_i          => iec_data,
        atn_o           => atn_o,
        atn_i           => iec_atn,
        srq_o           => srq_o,
        srq_i           => srq_o );

    iec_clock <= '0' when clk_o='0' else 'H';
    iec_data  <= '0' when data_o='0' else 'H';
    iec_atn   <= '0' when atn_o='0' else 'H';    

    i_io_bfm: entity work.io_bus_bfm
    generic map (
        g_name      => "io_bfm" )
    port map (
        clock       => clock,
    
        req         => req,
        resp        => resp );

    i_iec_bfm: entity work.iec_bus_bfm
    port map (
        iec_clock   => iec_clock,
        iec_data    => iec_data,
        iec_atn     => iec_atn );

        