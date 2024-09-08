--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2024
-- Entity: mem32_to_mem64
-- Date:2024-08-04 
-- Author: Gideon     
-- Description: Adapter to attach an 32 bit memory master to a 64 bit memory controller port.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity mem32_to_mem64 is
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;
        
        mem_req_32  : in  t_mem_req_32;
        mem_resp_32 : out t_mem_resp_32;
        
        mem_req_64  : out t_mem_req_64;
        mem_resp_64 : in  t_mem_resp_64 );

end entity;

architecture route_through of mem32_to_mem64 is
begin
    -- this adapter is the most simple variant; it just routes through the data and address
    -- The memory is organized as 16 bit wide, which means that bytes cannot appear on the same 'byte lane':
    -- A=0: B7 B6 B5 B4 B3 B2 B1 B0 -- straight
    -- A=2: B1 B0 B7 B6 B5 B4 B3 B2 -- word shift 0
    -- A=4: B3 B2 B1 B0 B7 B6 B5 B4 -- word shift 1
    -- A=6: B5 B4 B3 B2 B1 B0 B7 B6 -- word shift 2

    -- Byte swap is needed:
    -- A=0: B7 B6 B5 B4 B3 B2 B1 B0 -- straight
    -- A=1: xx xx xx xx B0 B3 B2 B1 -- byte shift (with mux)
    -- A=2: B1 B0 B7 B6 B5 B4 B3 B2 -- word shift 0
    -- A=3: xx xx xx xx B2 B5 B4 B3 -- word shift 0, byte swap
    -- A=4: B3 B2 B1 B0 B7 B6 B5 B4 -- word shift 1
    -- A=5: xx xx xx xx B4 B7 B6 B5 -- word shift 1, byte swap
    -- A=6: B5 B4 B3 B2 B1 B0 B7 B6 -- word shift 2
    -- A=7: xx xx xx xx B2 B1 B0 B7 -- word shift 2, byte swap

    -- Compared to 32 bit:
    -- A=0: B3 B2 B1 B0 -- straight
    -- A=1: B0 B3 B2 B1 -- shift 0
    -- A=2: B1 B0 B3 B2 -- shift 1
    -- A=3: B2 B1 B0 B3 -- shift 2

    -- The only thing that needs to be supported are: byte access, word access and dword access.
    -- word and dword access come for free, by simply using the address bit. byte access does not.
    -- What if... we use the address for byte writes and place the shifted byte in the upper 32 bits *statically*:
    -- Then, when address bit 0 is set, we simply swap the upper and lower 32 bit by inverting address bit 2:
    -- A=0: B3 B2 B1 B0 => xx xx xx xx B3 B2 B1 B0 (a=0)
    -- A=1: xx xx xx B1 => xx xx B1 xx xx xx xx xx (a=4)!
    -- A=2: xx xx B3 B2 => xx xx xx xx xx xx B3 B2 (a=2)
    -- A=3: xx xx xx B3 => xx xx B3 xx xx xx xx xx (a=6)!
    -- A=4: B3 B2 B1 B0 => xx xx xx xx B3 B2 B1 B0 (a=4)
    -- A=5: xx xx xx B1 => xx xx B1 xx xx xx xx xx (a=0)!
    -- A=6: xx xx B3 B2 => xx xx xx xx xx xx B3 B2 (a=6)
    -- A=7: xx xx xx B3 => xx xx B3 xx xx xx xx xx (a=2)!

    process(mem_req_32)
    begin
        mem_req_64.request      <= mem_req_32.request;
        mem_req_64.read_writen  <= mem_req_32.read_writen;
        mem_req_64.address      <= "00" & mem_req_32.address(25 downto 1); -- left: 27 downto 1; right: 25 downto 0
        mem_req_64.address(2)   <= mem_req_32.address(2) xor mem_req_32.address(0); -- whoa!!! This is crazy!!
        mem_req_64.tag          <= mem_req_32.address(0) & mem_req_32.tag; -- pass bit 0 of the address as tag, needed for read
        mem_req_64.data         <= X"0000" & mem_req_32.data(7 downto 0) & X"00" & mem_req_32.data;
        if mem_req_32.address(0) = '0' then
            mem_req_64.byte_en  <= "0000" & mem_req_32.byte_en;
        else
            mem_req_64.byte_en  <= "00" & mem_req_32.byte_en(0) & "00000";
        end if;
    end process;

    mem_resp_32.data     <= mem_resp_64.data(31 downto 0) when mem_resp_64.dack_tag(8) = '0' else
                            mem_resp_64.data(31 downto 8) & mem_resp_64.data(47 downto 40); -- !!!
    mem_resp_32.rack     <= mem_resp_64.rack;
    mem_resp_32.rack_tag <= mem_resp_64.rack_tag(7 downto 0);
    mem_resp_32.dack_tag <= mem_resp_64.dack_tag(7 downto 0);
    

end architecture;

