library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity mem_bus_register is
generic (
    g_reg_req   : boolean := true;
    g_reg_resp  : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    a_req       : in  t_mem_req_32;
    a_resp      : out t_mem_resp_32;
    a_refr_inh  : in  std_logic;

    b_req       : out t_mem_req_32;
    b_resp      : in  t_mem_resp_32;
    b_refr_inh  : out std_logic );
end entity;

architecture rtl of mem_bus_register is
    signal req_c    : t_mem_req_32 := c_mem_req_32_init;
    signal got_accepted : std_logic;
    signal extended_inhibit : std_logic;
    -- In between register blocks
--    signal temp_inh  : std_logic := '0';
--    signal temp_req  : t_mem_req_32;
--    signal temp_resp : t_mem_resp_32;
begin
    -- b_request_register: block
    --     signal req_c    : t_mem_req_32;
    --     signal a_refr_inh_d : std_logic := '0';
    -- begin
    --     -- request register
    --     process(clock)
    --     begin
    --         if rising_edge(clock) then
    --             if req_c.request = '0' or temp_resp.rack = '1' then -- if output_valid = '0' or output_ready = '1' ...
    --                 req_c <= a_req; -- could be new request or none.
    --             end if;
    --             if reset = '1' then
    --                 req_c.request <= '0';
    --             end if;
    --             a_refr_inh_d <= a_refr_inh;
    --         end if;
    --     end process;
        
    --     process(temp_resp, a_req, req_c)
    --     begin
    --         -- Just wire the data
    --         a_resp.data     <= temp_resp.data;
    --         a_resp.dack_tag <= temp_resp.dack_tag;
    
    --         -- Control depends on when the request is loaded into the register
    --         if g_reg_req then
    --             if req_c.request = '0' or temp_resp.rack = '1' then
    --                 a_resp.rack <= a_req.request;
    --                 a_resp.rack_tag <= a_req.tag;
    --             else
    --                 a_resp.rack <= '0';
    --                 a_resp.rack_tag <= (others => '0');
    --             end if;
    --         else -- just wires when the register is not used
    --             a_resp.rack <= temp_resp.rack;
    --             a_resp.rack_tag <= temp_resp.rack_tag;            
    --         end if;        
    --     end process;
    
    --     temp_req <= req_c when g_reg_req else a_req;
    --     temp_inh <= a_refr_inh_d when g_reg_req else a_refr_inh;
    -- end block;

    -- b_response_register: block
    --     signal resp_c   : t_mem_resp_32;    
    -- begin
    --     -- response register
    --     process(clock)
    --     begin
    --         if rising_edge(clock) then
    --             resp_c <= b_resp;
    --         end if;
    --     end process;

    --     process(b_resp, resp_c)
    --     begin
    --         -- default is pass through
    --         temp_resp <= b_resp;
            
    --         if g_reg_resp then
    --             -- use the registered data and data valid
    --             -- Request acknowledge doesn't support registering yet
    --             temp_resp.data     <= resp_c.data;
    --             temp_resp.dack_tag <= resp_c.dack_tag;
    --         end if;
    --     end process;
        
    --     -- Request is not modified
    --     b_req <= temp_req;
    --     b_refr_inh <= temp_inh;
    -- end block;

    got_accepted <= '1' when (b_resp.rack = '1' and b_resp.rack_tag = req_c.tag) else '0';

    -- request register
    process(clock)
    begin
        if rising_edge(clock) then
            if got_accepted = '1' then
                req_c.request <= '0';
            end if;

            if req_c.request = '0' or got_accepted = '1' then -- if output_valid = '0' or output_ready = '1' ...
                req_c.request     <= a_req.request;
                req_c.tag         <= a_req.tag;
                req_c.read_writen <= a_req.read_writen;
                req_c.address     <= a_req.address;
                req_c.byte_en     <= a_req.byte_en;
                req_c.data        <= a_req.data;

                -- If inhibit was active in the moment we send the request out,
                -- we need to extend the refresh inhibit until the command actually
                -- got accepted.
                if a_req.request = '1' and a_refr_inh = '1' then
                    extended_inhibit <= '1';
                elsif got_accepted = '1' then
                    extended_inhibit <= '0';
                end if;

            else
                req_c.tag(7) <= req_c.tag(7); -- test if we ever come here
            end if;
            if reset = '1' then
                extended_inhibit <= '0';
            --     req_c.request <= '0';
            end if;
        end if;
    end process;

    b_refr_inh <= a_refr_inh or extended_inhibit;

    process(b_resp, a_req, req_c, got_accepted)
    begin
        -- Just wire the data
        a_resp.data     <= b_resp.data;
        a_resp.dack_tag <= b_resp.dack_tag;

        -- Control depends on when the request is loaded into the register
        if req_c.request = '0' or got_accepted = '1' then -- if output_valid = '0' or output_ready = '1' ...
            a_resp.rack <= a_req.request;
            a_resp.rack_tag <= a_req.tag;
        else
            a_resp.rack <= '0';
            a_resp.rack_tag <= (others => '0');
        end if;        
    end process;

    b_req <= req_c;
end architecture;
