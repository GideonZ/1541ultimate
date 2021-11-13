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
use ieee.numeric_std.all;

entity tb_rx is
end tb_rx;

architecture tb of tb_rx is
    signal divisor : std_logic_vector(9 downto 0) := "0000010100";
    signal clk     : std_logic;
    signal reset   : std_logic;
    
    signal dotx    : std_logic;
    signal txchar  : std_logic_vector(7 downto 0);
    signal rxchar  : std_logic_vector(7 downto 0);
    signal rxchar_d: std_logic_vector(7 downto 0);
    
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
            txchar <= std_logic_vector(to_unsigned(character'pos(teststring(i)), 8));
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
    
    my_tx: entity work.tx 
    port map (
        divisor => divisor,
        clk     => clk,
        reset   => reset,
        tick    => '1',
        dotx    => dotx,
        txchar  => txchar,
    
        txd     => txd,
        done    => done );

    my_rx: entity work.rx 
    port map (
        divisor => divisor,
        clk     => clk,
        reset   => reset,
        tick    => '1',
    
        rxd     => txd,
        
        rxchar  => rxchar,
        rx_ack  => rx_ack );

    process(clk)
	begin
		if rising_edge(clk) then
            if rx_ack='1' then
			    rxchar_d <= rxchar;
			end if;
		end if;
	end process;
end tb;
