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

    dbg1_request : out std_logic;
    dbg1_rwn     : out std_logic;
    dbg1_rack    : out std_logic;
    dbg1_rack_tag: out std_logic_vector(7 downto 0);
    dbg1_dack_tag: out std_logic_vector(7 downto 0);
    dbg1_rdata   : out std_logic_vector(31 downto 0);
    dbg2_request : out std_logic;
    dbg2_rwn     : out std_logic;
    dbg2_rack    : out std_logic;
    dbg2_rack_tag: out std_logic_vector(7 downto 0);
    dbg2_dack_tag: out std_logic_vector(7 downto 0);
    dbg2_rdata   : out std_logic_vector(31 downto 0);
    
    mem_req_1x  : in  t_mem_req_32;
    mem_resp_1x : out t_mem_resp_32;
    
    mem_req_2x  : out t_mem_req_32;
    mem_resp_2x : in  t_mem_resp_32 );
    
end entity;    

architecture Gideon of memreq_halfrate is
    signal toggle       : std_logic;
    signal toggle_r_1x  : std_logic;
    signal req_2x_i     : t_mem_req_32 := c_mem_req_32_init;
    signal resp_1x_i    : t_mem_resp_32 := c_mem_resp_32_init;

--    subtype t_state is std_logic_vector(1 downto 0);
--    constant c_idle     : t_state := "00";
--    constant c_offer    : t_state := "01";
--    constant c_reply    : t_state := "11";
    type t_state is (c_idle, c_offer, c_reply, c_illegal);
    signal state        : t_state := c_idle;
    
--    attribute syn_keep : boolean;
--    attribute syn_keep of dbg1_request  : signal is true;
--    attribute syn_keep of dbg1_rwn      : signal is true;
--    attribute syn_keep of dbg1_rack     : signal is true;
--    attribute syn_keep of dbg1_rack_tag : signal is true;
--    attribute syn_keep of dbg1_dack_tag : signal is true;
--    attribute syn_keep of dbg1_rdata    : signal is true;
--    attribute syn_keep of dbg2_request  : signal is true;
--    attribute syn_keep of dbg2_rwn      : signal is true;
--    attribute syn_keep of dbg2_rack     : signal is true;
--    attribute syn_keep of dbg2_rack_tag : signal is true;
--    attribute syn_keep of dbg2_dack_tag : signal is true;
--    attribute syn_keep of dbg2_rdata    : signal is true;
begin
--    process(mem_req_1x, resp_1x_i, req_2x_i, mem_resp_2x, reset_1x)
--    begin
        dbg1_request  <= mem_req_1x.request;
        dbg1_rwn      <= mem_req_1x.read_writen;
        dbg1_rack     <= resp_1x_i.rack;
        dbg1_rack_tag <= resp_1x_i.rack_tag;
        dbg1_dack_tag <= resp_1x_i.dack_tag;
        dbg1_rdata    <= resp_1x_i.data;
    
        dbg2_request  <= req_2x_i.request;
        dbg2_rwn      <= req_2x_i.read_writen;
        dbg2_rack     <= mem_resp_2x.rack;
        dbg2_rack_tag <= mem_resp_2x.rack_tag;
        dbg2_dack_tag <= mem_resp_2x.dack_tag;
        dbg2_rdata    <= mem_resp_2x.data;
        
--        if reset_1x='1' then
--            dbg1_request  <= '0';
--            dbg1_rack     <= '0';
--            dbg1_rack_tag <= (others => '0');
--            dbg1_dack_tag <= (others => '0');
--            dbg1_rdata    <= (others => '0');
--        
--            dbg2_request  <= '1';            
--            dbg2_rack     <= '1';            
--            dbg2_rack_tag <= (others => '1');
--            dbg2_dack_tag <= (others => '1');
--            dbg2_rdata    <= (others => '1');
--        end if;
--
--    end process;
    
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
    
    -- Forward path state machine, return path registers
    -- return path - register needed to keep last value
    -- acknowledge can only be cleared when toggle = 1.
    process(clock_2x)
    begin
        if rising_edge(clock_2x) then
            case state is
            when c_idle =>
                if mem_req_1x.request='1' then -- either toggle is ok if timing was checked correctly
                    req_2x_i           <= mem_req_1x;
                    resp_1x_i.rack     <= '1';
                    resp_1x_i.rack_tag <= mem_req_1x.tag;
                    state <= c_offer;
                end if;
            
            when c_offer =>
                if mem_resp_2x.rack='1' then -- memory controller accepts
                    req_2x_i.request <= '0'; -- stop requesting
                    if toggle = '1' then
                        state <= c_idle;
                    elsif resp_1x_i.rack = '0' then
                        state <= c_idle;
                    else
                        state <= c_reply;
                    end if;
                end if;
                if toggle = '1' then
                    resp_1x_i.rack <= '0';
                    resp_1x_i.rack_tag <= (others => '0');
                end if;
                            
            when c_reply =>
                if toggle = '1' then
                    state <= c_idle;
                    resp_1x_i.rack <= '0';
                    resp_1x_i.rack_tag <= (others => '0');
                end if;
            
            when others =>
                state <= c_idle;
            end case;

            if reset_1x = '1' then
                req_2x_i.request <= '0';
                resp_1x_i.rack <= '0';
                resp_1x_i.rack_tag <= (others => '0');
                state <= c_idle;
            end if;

            -- Return path for data
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
