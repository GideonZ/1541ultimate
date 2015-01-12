--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: mem_to_mem32
-- Date:2015-01-05  
-- Author: Gideon     
-- Description: Adapter to attach an 8 bit memory slave to a 32 bit memory controller port.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity mem_to_mem32 is
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;
        
        mem_req_8   : in  t_mem_req;
        mem_resp_8  : out t_mem_resp;
        
        mem_req_32  : out t_mem_req_32;
        mem_resp_32 : in  t_mem_resp_32 );

end entity;

architecture route_through of mem_to_mem32 is
begin
    -- this adapter is the most simple variant; it just routes through the data and address
    -- no support for count and burst.
    mem_resp_8.data     <= mem_resp_32.data(31 downto 24);
    mem_resp_8.rack     <= mem_resp_32.rack;
    mem_resp_8.rack_tag <= mem_resp_32.rack_tag;
    mem_resp_8.dack_tag <= mem_resp_32.dack_tag;
    mem_resp_8.count    <= "00";
    
    mem_req_32.tag          <= mem_req_8.tag;
    mem_req_32.request      <= mem_req_8.request;
    mem_req_32.read_writen  <= mem_req_8.read_writen;
    mem_req_32.address      <= mem_req_8.address;
    mem_req_32.data         <= mem_req_8.data & mem_req_8.data & mem_req_8.data & mem_req_8.data;
    
    with mem_req_8.address(1 downto 0) select
        mem_req_32.byte_en <= 
            "1000" when "00",
            "0100" when "01",
            "0010" when "10",
            "0001" when others;

end architecture;
