-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2012, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Small Synchronous Stack Using Single Port Distributed RAM
-------------------------------------------------------------------------------
-- File       : distributed_stack.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This implementation makes use of the RAMX1 properties,
--              implementing a 16-deep synchronous stack in only one LUT per
--              bit. The value to be popped is always visible.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library unisim;
use unisim.vcomponents.all;

entity distributed_stack is
generic (
    Width : integer := 32;
    simultaneous_pushpop : boolean := false );     
port (
    clock       : in std_logic;
    reset       : in std_logic;
    pop         : in std_logic;
    push        : in std_logic;
    flush       : in std_logic;
    data_in     : in std_logic_vector(Width-1 downto 0);
    data_out    : out std_logic_vector(Width-1 downto 0);
    full        : out std_logic;
    data_valid  : out std_logic );
end distributed_stack;

architecture  Gideon  of  distributed_stack  is

    signal pointer         : unsigned(3 downto 0);
    signal address         : unsigned(3 downto 0);
    signal we              : std_logic;
    signal data_valid_i    : std_logic;
    signal full_i          : std_logic;
    signal filtered_pop    : std_logic;
    signal filtered_push   : std_logic
begin
    filtered_pop  <= data_valid_i and pop;
    filtered_push <= not full_i and push;
    full <= full_i;
    
    process(filtered_push, pop, pointer, ram_data, data_in)
    begin
        we <= filtered_push;
        data_out <= ram_data;
        data_valid  <= data_valid_i;

        if filtered_push='1' then
            address <= pointer + 1;
        else
            address <= pointer;
        end if;
        
        if simultaneous_pushpop then
            if filtered_push='1' and pop='1' then
                data_out <= data_in;
                we <= '0';
                data_valid <= '1';
            end if;
        end if;
    end process;

    process(clock)
        variable new_pointer : unsigned(3 downto 0);--integer range 0 to Depth;
    begin
        if rising_edge(clock) then
            if flush='1' then
                new_pointer := X"F";
            elsif (filtered_pop='1') and (filtered_push='0') then
                new_pointer := pointer - 1;
            elsif (filtered_pop='0') and (filtered_push='1') then
                new_pointer := pointer + 1;
            else
                new_pointer := pointer;
            end if;
            pointer <= new_pointer;

            if (new_pointer = X"F") then
                data_valid_i <= '0';
            else
                data_valid_i <= '1';
            end if;

            if (new_pointer /= X"E") then
                full_i <= '0';
            else
                full_i <= '1';
            end if;                                    

            if reset='1' then
                pointer <= X"F";
                full_i <= '0';
                data_valid_i <= '0';
            end if;
        end if;
    end process;

    RAMs : for ram2 in 0 to Width-1 generate
      i_ram : RAM16X1S
        port map (
          WCLK => clock,
          WE   => we,
          D    => data_in(ram2),
          A3   => address(3),
          A2   => address(2),
          A1   => address(1),
          A0   => address(0),
          O    => ram_data(ram2) );
    end generate;
end Gideon;
