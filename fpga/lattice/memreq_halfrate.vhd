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
port (
    clock_1x    : in  std_logic;
    clock_2x    : in  std_logic;
    reset_1x    : in  std_logic; -- synchronous with clock_1x

    phase_out   : out std_logic;
    toggle_r_2x : in  std_logic := '0';
    
    inhibit_1x  : in  std_logic := '0';
    mem_req_1x  : in  t_mem_req_32;
    mem_resp_1x : out t_mem_resp_32;
    
    inhibit_2x  : out std_logic;
    mem_req_2x  : out t_mem_req_32;
    mem_resp_2x : in  t_mem_resp_32 );
    
end entity;    

architecture Gideon of memreq_halfrate is
    signal toggle       : std_logic;
    signal toggle_r_1x  : std_logic;
    signal req_2x_i     : t_mem_req_32 := c_mem_req_32_init;
    signal resp_1x_i    : t_mem_resp_32 := c_mem_resp_32_init;
    signal local_req    : t_mem_req_32 := c_mem_req_32_init;
        
    type t_state is (c_idle, c_offer);--, c_reply, c_illegal);
    signal state        : t_state := c_idle;
    
begin
    
    i_toggle_reset_sync: entity work.pulse_synchronizer
    port map (
        clock_in  => clock_2x,
        pulse_in  => toggle_r_2x,
        clock_out => clock_1x,
        pulse_out => toggle_r_1x
    );

    -- if a_reset is in phase with clock_1x, and clock_2x is in phase with clock_1x, then,
    -- on a rising edge of clock_2x, there will also be a rising_edge of clock_1x. So,
    -- a_reset will be 'true' right before the edge of clock_2x, which makes the equation zero.
    -- Thus, the toggle signal will be 0 and then 1 in one cycle of clock_1x. 
    toggle <= not toggle and not toggle_r_1x when rising_edge(clock_2x); 
    phase_out <= toggle when rising_edge(clock_1x); -- should output '1' always, because toggle should be '1' upon rising edge of clock_1x

    mem_req_2x  <= req_2x_i;
    mem_resp_1x <= resp_1x_i;
    
--    process(resp_1x_i, state, mem_req_1x, a_mem_resp)
--    begin
--        -- default what we make in the 2x clock process, with the exception of .. the rack!
--        -- mem_resp_1x <= a_mem_resp; -- FIFO alternative
--        mem_resp_1x <= resp_1x_i; -- direct
--
--        if state = c_idle and mem_req_1x.request='1' then
--            mem_resp_1x.rack <= '1';
--            mem_resp_1x.rack_tag <= mem_req_1x.tag;
--        else
--            mem_resp_1x.rack <= '0';
--            mem_resp_1x.rack_tag <= (others => '0');
--        end if;
--    end process;
    
    -- Forward path state machine, return path registers
    process(clock_2x)
    begin
        if rising_edge(clock_2x) then
            if toggle = '1' then
                resp_1x_i.rack <= '0';
                resp_1x_i.rack_tag <= (others => '0');
            end if;
            
            inhibit_2x <= inhibit_1x;

            case state is
            when c_idle =>
                if mem_req_1x.request='1' and inhibit_1x = '0' then
                    req_2x_i <= mem_req_1x;
                    resp_1x_i.rack <= '1';
                    resp_1x_i.rack_tag <= mem_req_1x.tag;
                    state <= c_offer;
                end if;
            
            when c_offer =>
                if mem_resp_2x.rack='1' then -- memory controller accepts
                    req_2x_i.request <= '0'; -- stop requesting
                    if toggle = '1' then
                        state <= c_idle;
                    end if;
                elsif req_2x_i.request = '0' and toggle = '1' then
                    state <= c_idle;
                end if;
                            
            when others =>
                state <= c_idle;
            end case;

            if reset_1x = '1' then
                req_2x_i.request <= '0';
                state <= c_idle;
            end if;

            -- Return path for data
            -- Yields: "resp_1x_i"
            if toggle = '1' then
                resp_1x_i.dack_tag <= (others => '0');
            end if;
            if unsigned(mem_resp_2x.dack_tag) /= 0 then
                resp_1x_i.dack_tag <= mem_resp_2x.dack_tag;
                resp_1x_i.data     <= mem_resp_2x.data;
            end if;

        end if;
    end process;

end architecture;
