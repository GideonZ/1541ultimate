-------------------------------------------------------------------------------
-- Title      : ODDR Wrapper for Xilinx
-------------------------------------------------------------------------------
-- Description: Just a wrapper for an ODDR, trying to keep the clock inversion
--              local within the ODDR and not generated with a lut.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library unisim;
    use unisim.vcomponents.all;

entity my_ioddr is
port (
    clock       : in  std_logic;
    d0          : in  std_logic;
    d1          : in  std_logic;
    t0          : in  std_logic;
    t1          : in  std_logic;
    q0          : out std_logic;
    q1          : out std_logic;
    pin         : inout std_logic );

    attribute keep_hierarchy : string;
    --attribute keep_hierarchy of my_ioddr : entity is "yes";
    attribute ifd_delay_value : string;
    attribute ifd_delay_value of pin : signal is "6";
  
end entity;

architecture gideon of my_ioddr is
    signal q_i      : std_logic;
    signal t_i      : std_logic;
    signal not_clk  : std_logic;
begin
    not_clk <= not clock;
    
    i_out: ODDR2
    generic map (
		DDR_ALIGNMENT => "NONE",
		SRTYPE        => "SYNC" )
	port map (
		Q  => q_i,
		C0 => clock,
		C1 => not_clk,
		CE => '1',
		D0 => d0,
		D1 => d1,
		R  => '0',
		S  => '0' );

    i_tri: ODDR2
    generic map (
		DDR_ALIGNMENT => "NONE",
		SRTYPE        => "SYNC" )
	port map (
		Q  => t_i,
		C0 => clock,
		C1 => not_clk,
		CE => '1',
		D0 => t0,
		D1 => t1,
		R  => '0',
		S  => '0' );

    pin <= q_i when t_i='0' else 'Z';

    i_in: IDDR2
    generic map (
		DDR_ALIGNMENT => "NONE",
		SRTYPE        => "SYNC" )
	port map (
		D  => pin,
		C0 => clock,
		C1 => not_clk,
		CE => '1',
		Q0 => q0,
		Q1 => q1,
		R  => '0',
		S  => '0' );

end architecture;
