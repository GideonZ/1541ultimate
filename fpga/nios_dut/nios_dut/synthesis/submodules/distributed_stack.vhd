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
-- 
--              For building on altera, this block has been sub-optimized,
--              and the required cells are now inferred.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;


entity distributed_stack is
generic (
    width : integer := 32;
    simultaneous_pushpop : boolean := false );     
port (
    clock       : in std_logic;
    reset       : in std_logic;
    pop         : in std_logic;
    push        : in std_logic;
    flush       : in std_logic;
    data_in     : in std_logic_vector(width-1 downto 0);
    data_out    : out std_logic_vector(width-1 downto 0);
    full        : out std_logic;
    data_valid  : out std_logic );
end distributed_stack;

architecture  Gideon  of  distributed_stack  is

    signal pointer         : unsigned(3 downto 0);
    signal address         : unsigned(3 downto 0);
    signal we              : std_logic;
    signal en              : std_logic;
    signal data_valid_i    : std_logic;
    signal full_i          : std_logic;
    signal filtered_pop    : std_logic;
    signal filtered_push   : std_logic;
    
    signal sel             : std_logic;
    signal ram_data        : std_logic_vector(width-1 downto 0) := (others => '0');
    signal last_written    : std_logic_vector(width-1 downto 0) := (others => '0');
begin
    filtered_pop  <= data_valid_i and pop;
    filtered_push <= not full_i and push;

    full <= full_i;
    data_valid <= data_valid_i;

    en <= filtered_pop or filtered_push;
    we <= filtered_push;
        
    address <= (pointer - 2) when filtered_pop='1' else pointer;

    process(clock)
        variable new_pointer : unsigned(3 downto 0);
    begin
        if rising_edge(clock) then
            new_pointer := pointer;
            if flush='1' then
                new_pointer := X"0";
            elsif (filtered_pop='1') and (filtered_push='0') then
                new_pointer := new_pointer - 1;
            elsif (filtered_pop='0') and (filtered_push='1') then
                new_pointer := new_pointer + 1;
            end if;

            pointer <= new_pointer;
            if (new_pointer = X"0") then
                data_valid_i <= '0';
            else
                data_valid_i <= '1';
            end if;

            if (new_pointer /= X"F") then
                full_i <= '0';
            else
                full_i <= '1';
            end if;                                    

            if reset='1' then
                pointer <= X"0";
                full_i <= '0';
                data_valid_i <= '0';
            end if;
        end if;
    end process;

    data_out <= ram_data when sel = '0' else last_written;

    b_ram: block
        type t_ram  is array(0 to 15) of std_logic_vector(width-1 downto 0);
        signal ram  : t_ram := (others => (others => '0'));
    begin
        process(clock)
        begin
            if rising_edge(clock) then
                if en = '1' then
                    ram_data <= ram(to_integer(address));
                    sel <= '0';
                    if we = '1' then 
                        ram(to_integer(address)) <= data_in;
                        last_written <= data_in;
                        sel <= '1';
                    end if;
                end if;
                if reset='1' then
                    sel <= '0';
                end if;
            end if;
        end process;
    end block;

end Gideon;
