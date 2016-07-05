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

    type t_block_command is ( allocate, write );
    type t_block_req is record
        request : std_logic;
        command : t_block_command;
        id      : unsigned(7 downto 0);   -- for write only
        bytes   : unsigned(11 downto 0);  -- for write only
        -- lun: unsigned(1 downto 0); -- to indicate which logical unit has written the data block
    end record;

    type t_block_resp is record
        done    : std_logic;
        error   : std_logic;
        id      : unsigned(7 downto 0);
        address : unsigned(25 downto 0);
    end record;

    constant c_block_req_init : t_block_req := (
        request => '0',
        command => allocate,
        id      => X"00",
        bytes   => X"000" );

    constant c_block_resp_init : t_block_resp := (
        done    => '0',
        error   => '0',
        id      => X"00",
        address => (others => '0') );

    -- procedure:
    -- set request='1' and provide the command allocate
    -- wait until resp.done is '1'. If resp.error = '0' then allocated buffer ID and address become available
    -- store address and use for DMA action. store ID, to tell the block manager how much data you'll put in the block
    -- if error = '1' then no valid address is available for writing data. drop input data.
    -- 
    -- If address is valid, write data to RAM
    -- set request='1' and provide the command 'write', and set the ID and bytes fields. (The ID being the same as you got!)
    -- wait until resp.done is '1'. if resp.error = '1', you should wait and retry indefinitely otherwise there is a memory leak
    -- but it should actually never occur.

end package;
