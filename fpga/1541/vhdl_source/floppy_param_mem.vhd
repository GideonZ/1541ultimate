-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Floppy Parameter memory
-------------------------------------------------------------------------------
-- File       : floppy.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements the emulator of the floppy drive.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library unisim;
    use unisim.vcomponents.all;

entity floppy_param_mem is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    cpu_write   : in  std_logic;
    cpu_ram_en  : in  std_logic;
    cpu_addr    : in  std_logic_vector(10 downto 0);
    cpu_wdata   : in  std_logic_vector(7 downto 0);
    cpu_rdata   : out std_logic_vector(7 downto 0);

    track       : in  std_logic_vector(6 downto 0);
    track_start : out std_logic_vector(25 downto 0);
    max_offset  : out std_logic_vector(13 downto 0) );

end floppy_param_mem;

architecture gideon of floppy_param_mem is
    signal toggle     : std_logic;
    signal param_addr : std_logic_vector(8 downto 0);
    signal param_data : std_logic_vector(31 downto 0);
begin
    ram: RAMB16_S9_S36
    port map (
		CLKA  => clock,
		SSRA  => reset,
		ENA   => cpu_ram_en,
		WEA   => cpu_write,
        ADDRA => cpu_addr,
		DIA   => cpu_wdata,
		DIPA  => "0",
		DOA   => cpu_rdata,
		DOPA  => open,
    
		CLKB  => clock,
		SSRB  => reset,
		ENB   => '1',
		WEB   => '0',
        ADDRB => param_addr,
		DIB   => X"00000000",
		DIPB  => X"0",
		DOB   => param_data,
		DOPB  => open );

    param_addr <= '0' & track & toggle;

    process(clock)
    begin
        if rising_edge(clock) then
            if toggle='1' then -- even addresses (one clock later)
                track_start <= param_data(track_start'range);
            else
                max_offset  <= param_data(max_offset'range);
            end if;
            if reset='1' then
                toggle <= '0';
            else
                toggle <= not toggle;
            end if;
        end if;
    end process;
end gideon;
