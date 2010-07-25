-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2004, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Testbench for Serial receiver: 115200/8N1
-------------------------------------------------------------------------------
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-- Created    : Wed Apr 28, 2004
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

library work;
use work.conversion_pkg.all;

entity tb_rx is
end tb_rx;

architecture tb of tb_rx is
    component tx is
    generic (clks_per_bit : integer := 434);
    port (
        clk     : in  std_logic;
        reset   : in  std_logic;
        
        dotx    : in  std_logic;
        txchar  : in  std_logic_vector(7 downto 0);
    
        txd     : out std_logic;
        done    : out std_logic );
    end component;

    component rx is
    generic (clks_per_bit : integer := 434);
    port (
        clk     : in  std_logic;
        reset   : in  std_logic;
    
        rxd     : in  std_logic;
        
        rxchar  : out std_logic_vector(7 downto 0);
        rx_ack  : out std_logic );
    end component;

    signal clk     : std_logic;
    signal reset   : std_logic;
    
    signal dotx    : std_logic;
    signal txchar  : std_logic_vector(7 downto 0);
    signal rxchar  : std_logic_vector(7 downto 0);
    
    signal rx_ack  : std_logic;
    signal txd     : std_logic;
    signal done    : std_logic;
    constant teststring : string := "Gideon is gek";
begin
    ck: process
    begin
        clk <= '0'; wait for 10 ns;
        clk <= '1'; wait for 10 ns;
    end process;
    
    test: process
    begin
        reset  <= '1';
        dotx   <= '0';
        txchar <= (others => '0');
        wait for 80 ns;
        reset  <= '0';
        wait until clk='1';
        for i in teststring'range loop
            txchar <= CharToStd(teststring(i));
            dotx   <= '1';
            wait until clk='1';
            dotx   <= '0';
            wait until clk='1';
            while done='0' loop
                wait until clk='1';
            end loop;
        end loop;
        wait;
    end process;
    
    my_tx: tx 
    generic map (20)
    port map (
        clk     => clk,
        reset   => reset,
        
        dotx    => dotx,
        txchar  => txchar,
    
        txd     => txd,
        done    => done );

    my_rx: rx 
    generic map (20)
    port map (
        clk     => clk,
        reset   => reset,
    
        rxd     => txd,
        
        rxchar  => rxchar,
        rx_ack  => rx_ack );

end tb;
