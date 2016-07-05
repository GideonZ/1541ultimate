-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : free_queue
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: A simple source of free memory blocks. Software provides, hardware uses.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

package block_bus_pkg is

    type t_alloc_resp is record
        done    : std_logic;
        error   : std_logic;
        id      : unsigned(7 downto 0);
        address : unsigned(25 downto 0);
    end record;

    type t_used_req is record
        request : std_logic;
        id      : unsigned(7 downto 0);
        bytes   : unsigned(11 downto 0);
    end record;

    constant c_alloc_resp_init : t_alloc_resp := (
        done    => '0',
        error   => '0',
        id      => X"00",
        address => (others => '0') );

    constant c_used_req_init : t_used_req := (
        request => '0',
        id      => X"00",
        bytes   => X"000" );
        
end package;
