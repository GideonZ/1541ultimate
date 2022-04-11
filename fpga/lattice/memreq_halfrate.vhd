-------------------------------------------------------------------------------
-- Title      : MemReq rate adapter
-------------------------------------------------------------------------------
-- Description: This module implements a simple bridge between a half-rate 
--              memory interface to a full-rate memory interface. This is
--              useful for memory controllers that cannot run slower than
--              a given frequency, but when the main logic cannot keep up.
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.mem_bus_pkg.all;
    
entity memreq_halfrate is
generic (
    g_reg_in    : boolean := true );
port (
    clock_1x    : in  std_logic;
    clock_2x    : in  std_logic;
    reset_1x    : in  std_logic; -- synchronous with clock_1x
    
    mem_req_1x  : in  t_mem_req_32;
    mem_resp_1x : out t_mem_resp_32;
    
    mem_req_2x  : out t_mem_req_32;
    mem_resp_2x : in  t_mem_resp_32 );
    
end entity;    

architecture Gideon of memreq_halfrate is
    signal phase        : std_logic;
    signal mask_req     : std_logic;
    signal mem_req_reg  : t_mem_req_32;
    signal req_2x_i     : t_mem_req_32;
begin
    process(clock_2x)
    begin
        if rising_edge(clock_2x) then
            if reset_1x = '1' then
                phase <= '0'; -- within one low speed clock, phase is first 0, then 1
                mask_req <= '0';
            else
                phase <= not phase;
            
                if phase = '0' then
                    mem_req_reg <= mem_req_1x;
                end if;
                -- if the ack happens in the first half of the cycle, extend the
                -- ack to the requester, and mask the request to the controller
                mask_req <= mem_resp_2x.rack and not phase;
            end if;
        end if;
    end process;

    -- forward path
    process(mem_req_1x, mem_req_reg, phase, mask_req)    
    begin
        if g_reg_in then
            req_2x_i <= mem_req_reg;
        else
            req_2x_i <= mem_req_1x;
        end if;
        if mask_req = '1' then
            req_2x_i.request <= '0';
        end if;
    end process;
    mem_req_2x <= req_2x_i;
    
    -- return path - register needed to keep last value
    -- acknowledge can only be cleared when phase = 1.
    process(clock_2x)
    begin
        if rising_edge(clock_2x) then
            if phase = '1' then
                mem_resp_1x.dack_tag <= (others => '0');
            end if;
            if unsigned(mem_resp_2x.dack_tag) /= 0 then
                mem_resp_1x <= mem_resp_2x;
            end if;
        end if;
    end process;
       
end architecture;
