-------------------------------------------------------------------------------
-- Title      : avalon_to_mem32_bridge
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module bridges the avalon bus to I/O
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

--library work;
--    use work.mem_bus_pkg.all;

entity avalon_to_mem32_bridge is
generic (
    g_tag               : std_logic_vector(7 downto 0) := X"5B" );
port (
    clock               : in  std_logic;
    reset               : in  std_logic;
    
    -- 32 bits Avalon bus interface
    avs_read            : in  std_logic;
    avs_write           : in  std_logic;
    avs_address         : in  std_logic_vector(25 downto 0);
    avs_writedata       : in  std_logic_vector(31 downto 0);
    avs_byteenable      : in  std_logic_vector(3 downto 0);
    avs_waitrequest     : out std_logic;
    avs_readdata        : out std_logic_vector(31 downto 0);
    avs_readdatavalid   : out std_logic;

    mem_req_read_writen : out std_logic;
    mem_req_request     : out std_logic;
    mem_req_address     : out std_logic_vector(25 downto 0);
    mem_req_wdata       : out std_logic_vector(31 downto 0);
    mem_req_byte_en     : out std_logic_vector(3 downto 0);
    mem_req_tag         : out std_logic_vector(7 downto 0);
    
    mem_resp_rack_tag   : in  std_logic_vector(7 downto 0);
    mem_resp_dack_tag   : in  std_logic_vector(7 downto 0);
    mem_resp_data       : in  std_logic_vector(31 downto 0) );

end entity;

architecture rtl of avalon_to_mem32_bridge is
begin

    avs_waitrequest     <= '0' when (mem_resp_rack_tag = g_tag) else '1';
    avs_readdatavalid   <= '1' when (mem_resp_dack_tag = g_tag) else '0';
    avs_readdata        <= mem_resp_data;

    mem_req_read_writen <= avs_read;
    mem_req_request     <= avs_read or avs_write;
    mem_req_address     <= avs_address;
    mem_req_wdata       <= avs_writedata;
    mem_req_byte_en     <= avs_byteenable;
    mem_req_tag         <= g_tag;

end architecture;
