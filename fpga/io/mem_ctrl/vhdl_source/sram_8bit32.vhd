-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Asynchronous SRAM Controller
-------------------------------------------------------------------------------
-- File       : sram_8bit32.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements a simple, single access sram controller,
--              using an external 32-bit sram, and an internal 8 bit bus.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_arith.all;
    use ieee.std_logic_unsigned.all;

library unisim;
    use unisim.vcomponents.all;
    
entity sram_8bit32 is
generic (
	tag_width		   : integer := 2;
	
	SRAM_WR_ASU		   : integer := 0;
	SRAM_WR_Pulse      : integer := 1; -- 2 cycles in total
	SRAM_WR_Hold       : integer := 1;

	SRAM_RD_ASU		   : integer := 0;
	SRAM_RD_Pulse      : integer := 1;
	SRAM_RD_Hold	   : integer := 1 ); -- recovery time (bus turnaround)
port (
    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';

    req         : in  std_logic;
    req_tag     : in  std_logic_vector(1 to tag_width) := (others => '0');
    readwriten  : in  std_logic;
    address     : in  std_logic_vector(19 downto 0);
    rack        : out std_logic;
	dack		: out std_logic;
    rack_tag	: out std_logic_vector(1 to tag_width);
	dack_tag	: out std_logic_vector(1 to tag_width);
	
    wdata       : in  std_logic_vector(7 downto 0);
    rdata       : out std_logic_vector(7 downto 0);

	--
    SRAM_A      : out   std_logic_vector(17 downto 0);
    SRAM_OEn    : out   std_logic;
    SRAM_WEn    : out   std_logic;
    SRAM_CSn    : out   std_logic;
    SRAM_D      : inout std_logic_vector(31 downto 0) := (others => 'Z');
    SRAM_BEn    : out   std_logic_vector(3 downto 0) );
end sram_8bit32;    

architecture mux of sram_8bit32 is
	signal rdata_i		: std_logic_vector(31 downto 0);
	signal wdata_i		: std_logic_vector(31 downto 0);
	signal wdata_mask 	: std_logic_vector(3 downto 0);
	signal a_low		: std_logic_vector(1 downto 0);
	signal rack_i		: std_logic;
	signal dack_i		: std_logic;
begin
	ctrl: entity work.simple_sram
	generic map (
	    SRAM_Byte_Lanes    => 4,
		SRAM_Data_Width	   => 32,
	
		SRAM_WR_ASU		   => SRAM_WR_ASU,
		SRAM_WR_Pulse      => SRAM_WR_Pulse,
		SRAM_WR_Hold       => SRAM_WR_Hold,
	
		SRAM_RD_ASU		   => SRAM_RD_ASU,
		SRAM_RD_Pulse      => SRAM_RD_Pulse,
		SRAM_RD_Hold	   => SRAM_RD_Hold,
		
	    SRAM_A_Width       => 18 )
	port map (
	    clock       => clock,
	    reset       => reset,
	
	    req         => req,
	    req_tag	    => req_tag,
	    readwriten  => readwriten,
	    address     => address(19 downto 2),
	    rack        => rack_i,
		dack		=> dack_i,
	    rack_tag    => rack_tag,
		dack_tag    => dack_tag,
		
	    wdata       => wdata_i,
		wdata_mask	=> wdata_mask,
	    rdata       => rdata_i,
	
		--
	    SRAM_A      => SRAM_A,
	    SRAM_OEn    => SRAM_OEn,
	    SRAM_WEn    => SRAM_WEn,
	    SRAM_CSn    => SRAM_CSn,
	    SRAM_D      => SRAM_D,
	    SRAM_BEn    => SRAM_BEn );

	wdata_i <= wdata & wdata & wdata & wdata;
	
	-- muxing:
	process(clock)
		variable hold : std_logic;
	begin
		if rising_edge(clock) then
			if rack_i='1' then
				hold := '1';
			end if;
			if dack_i='1' then
				hold := '0';
			end if;
			if hold='0' then
				a_low <= address(1 downto 0);
			end if;
			if reset='1' then
				hold := '0';
				a_low <= "00";
			end if;
		end if;
	end process;

	process(address)
	begin
		case address(1 downto 0) is
		when "00" => wdata_mask <= "0001";
		when "01" => wdata_mask <= "0010";
		when "10" => wdata_mask <= "0100";
		when "11" => wdata_mask <= "1000";
		when others =>
			wdata_mask <= "0000";
		end case;
	end process;
	
	with a_low select rdata <=
		rdata_i(07 downto 00) when "00",
		rdata_i(15 downto 08) when "01",
		rdata_i(23 downto 16) when "10",
		rdata_i(31 downto 24) when others;

	dack <= dack_i;
	rack <= rack_i;
end mux;

