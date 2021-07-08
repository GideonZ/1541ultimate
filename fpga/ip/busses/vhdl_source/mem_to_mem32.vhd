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
    generic (
        g_big_endian : boolean );
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
    mem_resp_8.data     <= mem_resp_32.data(31 downto 24) when g_big_endian else mem_resp_32.data(7 downto 0);
    mem_resp_8.rack     <= mem_resp_32.rack;
    mem_resp_8.rack_tag <= mem_resp_32.rack_tag;
    mem_resp_8.dack_tag <= mem_resp_32.dack_tag;
    mem_resp_8.count    <= "00";
    
    mem_req_32.tag          <= mem_req_8.tag;
    mem_req_32.request      <= mem_req_8.request;
    mem_req_32.read_writen  <= mem_req_8.read_writen;
    mem_req_32.address      <= mem_req_8.address;
    mem_req_32.data         <= (mem_req_8.data & X"000000") when g_big_endian else (X"000000" & mem_req_8.data);
    mem_req_32.byte_en      <= "1000" when g_big_endian else "0001";

end architecture;

-- The buffered variant of the 8-to-32 bit bus conversion performs reads in 32-bit mode
-- and compares the address of consequetive accesses to read from the buffer instead of
-- issuing a new access. The buffer is therefore just 32 bits and could potentially reduce
-- the number of accesses by a factor of 4. Writes fall through, in order to make sure
-- that a read never requires a pending write to be flushed first. Of course, writes also
-- update the buffered data.
--  
architecture buffered of mem_to_mem32 is
    type t_state is (idle, reading, read_req);
    type t_vars is record
        state           : t_state;
        last_address    : unsigned(mem_req_32.address'range);
        address_valid   : std_logic;
        buffered_data   : std_logic_vector(31 downto 0);
    end record;
    constant c_vars_init : t_vars := (state => idle, address_valid => '0', buffered_data => (others => '0'), last_address => (others => '0'));
    signal cur, nxt     : t_vars := c_vars_init;  

    function slice(a : std_logic_vector; len : natural; sel : unsigned) return std_logic_vector is
        alias aa : std_logic_vector(a'length-1 downto 0) is a;
        variable si : natural;
    begin
        si := to_integer(sel);
        return aa(len-1+si*len downto si*len);
    end function;
begin
    process(cur, mem_req_8, mem_resp_32)
        variable alow : unsigned(1 downto 0);
    begin
        nxt <= cur;

        mem_resp_8.data     <= X"00";
        mem_resp_8.rack     <= '0';
        mem_resp_8.rack_tag <= X"00";
        mem_resp_8.dack_tag <= X"00";
        mem_resp_8.count    <= "00";

        mem_req_32.tag          <= mem_req_8.tag;
        mem_req_32.request      <= '0';
        mem_req_32.read_writen  <= mem_req_8.read_writen;
        mem_req_32.address      <= mem_req_8.address;
        if g_big_endian then
            mem_req_32.data         <= mem_req_8.data & X"000000";
            mem_req_32.byte_en      <= "1000";
        else
            mem_req_32.data         <= X"000000" & mem_req_8.data;
            mem_req_32.byte_en      <= "0001";
        end if;

        case cur.state is
        when idle =>
            if mem_req_8.request = '1' then
                if mem_req_8.read_writen = '0' then
                    mem_resp_8.rack     <= mem_resp_32.rack;
                    mem_resp_8.rack_tag <= mem_resp_32.rack_tag;
                    mem_req_32.request  <= '1';

                    if cur.address_valid = '1' and mem_req_8.address(mem_req_8.address'high downto 2) = cur.last_address(mem_req_8.address'high downto 2) then
                        alow := mem_req_8.address(1 downto 0);
                        if g_big_endian then alow := not alow; end if;
                        case alow is
                        when "00" =>
                            nxt.buffered_data(7 downto 0) <= mem_req_8.data;
                        when "01" =>
                            nxt.buffered_data(15 downto 8) <= mem_req_8.data;
                        when "10" =>
                            nxt.buffered_data(23 downto 16) <= mem_req_8.data;
                        when "11" =>
                            nxt.buffered_data(31 downto 24) <= mem_req_8.data;
                        when others =>
                            null;
                        end case;
                    end if;

                else -- read
                    if cur.address_valid = '1' and mem_req_8.address(mem_req_8.address'high downto 2) = cur.last_address(mem_req_8.address'high downto 2) then
                        -- Ok.. easy, we're done.
                        mem_resp_8.rack     <= '1';
                        mem_resp_8.rack_tag <= mem_req_8.tag;
                        mem_resp_8.dack_tag <= mem_req_8.tag;
                        mem_resp_8.data     <= slice(cur.buffered_data, 8, mem_req_8.address(1 downto 0)); 
                    else
                        -- Not so easy, a request should be made to the memory, which may be acked immediately, or not
                        mem_req_32.request <= '1';
                        mem_req_32.address(1 downto 0) <= "00"; -- only aligned access
                        nxt.last_address <= mem_req_8.address;
                        nxt.address_valid <= '1';
                        if mem_resp_32.rack_tag /= mem_req_8.tag then
                            nxt.state <= read_req;
                        else
                            nxt.state <= reading;
                        end if;
                    end if;
                end if;
            end if;

        when read_req => -- we need to read, read has not been acknowledged yet.
            mem_req_32.request <= '1';
            mem_req_32.address(1 downto 0) <= "00"; -- only aligned access
            if mem_resp_32.rack_tag = mem_req_8.tag then
                -- does data come in the same cycle?
                if mem_resp_32.dack_tag = mem_req_8.tag then
                    nxt.buffered_data <= mem_resp_32.data;
                    nxt.state <= idle;
                else
                    nxt.state <= reading;
                end if;                     
            end if;

        when reading =>
            if mem_resp_32.dack_tag = mem_req_8.tag then
                nxt.buffered_data <= mem_resp_32.data;
                nxt.state <= idle;
            end if;                     
        when others =>
            null;
        end case;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            cur <= nxt;
            if reset = '1' then
                cur.address_valid <= '0';
            end if;
        end if;
    end process;
end architecture;

