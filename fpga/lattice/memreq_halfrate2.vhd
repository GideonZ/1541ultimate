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
    
entity memreq_halfrate2 is
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

architecture Gideon of memreq_halfrate2 is
    signal toggle       : std_logic;
    signal toggle_r_1x  : std_logic;

    signal req_2x_i     : t_mem_req_32 := c_mem_req_32_init;
    signal resp_1x_i    : t_mem_resp_32 := c_mem_resp_32_init;
    signal busy         : std_logic;        
    signal dack_tag_2x  : std_logic_vector(7 downto 0);
    signal rdata_2x     : std_logic_vector(31 downto 0);

    signal r1_req       : std_logic;
    signal r1_req_tag   : std_logic_vector(7 downto 0);
    signal r1_rack      : std_logic;
    signal r1_rack_tag  : std_logic_vector(7 downto 0);
    signal r1_dack_tag  : std_logic_vector(7 downto 0);
    signal r2_req       : std_logic;
    signal r2_req_tag   : std_logic_vector(7 downto 0);
    signal r2_rack      : std_logic;
    signal r2_rack_tag  : std_logic_vector(7 downto 0);
    signal r2_dack_tag  : std_logic_vector(7 downto 0);
begin
    r1_req       <= mem_req_1x.request;
    r1_req_tag   <= mem_req_1x.tag;
    r1_rack      <= resp_1x_i.rack;
    r1_rack_tag  <= resp_1x_i.rack_tag;
    r1_dack_tag  <= resp_1x_i.dack_tag;
    r2_req       <= req_2x_i.request;
    r2_req_tag   <= req_2x_i.tag;
    r2_rack      <= mem_resp_2x.rack;
    r2_rack_tag  <= mem_resp_2x.rack_tag;
    r2_dack_tag  <= mem_resp_2x.dack_tag;

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
    
    -- The idea is to forward the request to the double speed memory controller in the second half of the
    -- 50 MHz cycle. Thus, when phase = 1. This can be done in two ways; namely, with a register
    -- that has a 10 ns requirement on the request side, when phase = 0. OR.. directly, passing the
    -- dirty delays into the memory controller. Decided: a register on double clock is cleaner
    process(clock_2x)
    begin
        if rising_edge(clock_2x) then
            if busy = '0' then
                req_2x_i <= mem_req_1x;
            end if;
            req_2x_i.request <= (mem_req_1x.request or busy) and not toggle;
            inhibit_2x <= inhibit_1x;
        end if;
    end process;            

    -- In order to have a reply at 50 MHz that is a full cycle long, a single bit busy signal is created
    -- that tells us whether the request has been accepted by the high clock memory controller.
    -- Under the assumption that the memory controller does not queue requests, it therefore
    -- answers with 'rack' in the same cycle as the request, or it does so in the next cycle where
    -- request is active. And, this will always be when toggle = 1, thus before the rising edge
    -- of clock 1x!
    busy <= (req_2x_i.request and not mem_resp_2x.rack) when rising_edge(clock_1x);

    -- Now that we have a busy signal, we can easily reconstruct rack and rack_tag!
    process(mem_req_1x, busy)
    begin
        resp_1x_i.rack     <= '0';
        resp_1x_i.rack_tag <= (others => '0');
        if busy = '0' then
            resp_1x_i.rack <= mem_req_1x.request;
            if mem_req_1x.request = '1' then
                resp_1x_i.rack_tag <= mem_req_1x.tag;
            end if;
        end if;
    end process;

    -- Return Path. Currently, it is observed that the response from a request in the even cycle.
    -- But even if it doesn't we can easily delay it one clock with a clock enable, and still have
    -- a register output that can be reclocked in the 1x domain

    process(clock_2x)
    begin
        if rising_edge(clock_2x) then
            if toggle = '1' then
                dack_tag_2x <= (others => '0');
            end if;
            if unsigned(mem_resp_2x.dack_tag) /= 0 then
                dack_tag_2x <= mem_resp_2x.dack_tag;
                rdata_2x <= mem_resp_2x.data;
            end if;
        end if;
    end process;            

    -- Now reclock in the 1x domain
    resp_1x_i.dack_tag <= dack_tag_2x when rising_edge(clock_1x);
    resp_1x_i.data     <= rdata_2x when rising_edge(clock_1x);

end architecture;
