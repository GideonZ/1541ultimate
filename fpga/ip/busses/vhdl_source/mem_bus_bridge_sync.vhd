-------------------------------------------------------------------------------
-- Title      : Mem Bus Clock Domain Bridge
-------------------------------------------------------------------------------
-- Description: This module implements a FIFO based clock domain bridge.
--              REQUIREMENT: Clock B is exactly twice the frequency of Clock A,
--              and these clocks are locked and related. 
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.mem_bus_pkg.all;
    
entity mem_bus_bridge_sync is
port (
    phase_out   : out std_logic;
    a_clock     : in  std_logic;
    a_reset     : in  std_logic;
    a_mem_req   : in  t_mem_req_32;
    a_mem_resp  : out t_mem_resp_32;

    b_clock     : in  std_logic;
    b_reset     : in  std_logic;
    b_toggle_r  : in  std_logic;
    b_mem_req   : out t_mem_req_32;
    b_mem_resp  : in  t_mem_resp_32 );
    
end entity;    

architecture Gideon of mem_bus_bridge_sync is
    signal a_mem_req_valid  : std_logic;
    signal a_mem_req_vec    : std_logic_vector(c_mem_bus_req_width-1 downto 0);
    signal a_mem_req_full   : std_logic;
    signal b_mem_req_vec    : std_logic_vector(c_mem_bus_req_width-1 downto 0);
    signal b_mem_req_valid  : std_logic;

    signal b_mem_resp_valid : std_logic;
    signal b_mem_resp_vec   : std_logic_vector(39 downto 0);
    signal a_mem_resp_vec   : std_logic_vector(39 downto 0);
    signal a_mem_resp_valid : std_logic;
    signal a_toggle_r       : std_logic;
    signal toggle           : std_logic;
begin
    i_toggle_reset_sync: entity work.pulse_synchronizer
    port map (
        clock_in  => b_clock,
        pulse_in  => b_toggle_r,
        clock_out => a_clock,
        pulse_out => a_toggle_r
    );

    -- if a_reset is in phase with a_clock, and b_clock is in phase with a_clock, then,
    -- on a rising edge of b_clock, there will also be a rising_edge of a_clock. So,
    -- a_reset will be 'true' right before the edge of b_clock, which makes the equation zero.
    -- Thus, the toggle signal will be 0 and then 1 in one cycle of a_clock. 
    toggle <= not toggle and not a_toggle_r when rising_edge(b_clock); 

    phase_out <= toggle when rising_edge(a_clock); -- should output '1' always, because toggle should be '1' upon rising edge of a_clock

    -- Forward path
    a_mem_resp.rack_tag <= a_mem_req.tag when (a_mem_req_full = '0' and a_mem_req.request = '1') else X"00";
    a_mem_resp.rack     <= a_mem_req_valid;
    a_mem_req_vec       <= to_std_logic_vector(a_mem_req);
    a_mem_req_valid     <= a_mem_req.request and toggle and not a_mem_req_full; 

    i_forward_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 10,
        g_data_width   => c_mem_bus_req_width,
        g_threshold    => 5,
        g_storage      => "distributed",
        g_storage_lat  => "distributed",
        g_fall_through => true
    )
    port map(
        clock          => b_clock,
        reset          => b_reset,
        rd_en          => b_mem_resp.rack,
        wr_en          => a_mem_req_valid,
        din            => a_mem_req_vec,
        dout           => b_mem_req_vec,
        full           => a_mem_req_full,
        valid          => b_mem_req_valid
    );

    b_mem_req <= to_mem_req(b_mem_req_vec, b_mem_req_valid);

    -- Return Path
    b_mem_resp_valid <= '1' when b_mem_resp.dack_tag /= X"00" else '0';
    b_mem_resp_vec   <= b_mem_resp.dack_tag & b_mem_resp.data;

    i_return_fifo: entity work.sync_fifo
    generic map( 
        g_depth        => 10,
        g_data_width   => a_mem_resp_vec'length,
        g_threshold    => 5,
        g_storage      => "distributed",
        g_storage_lat  => "distributed",
        g_fall_through => true
    )
    port map(
        clock          => b_clock,
        reset          => b_reset,
        rd_en          => toggle,
        wr_en          => b_mem_resp_valid,
        din            => b_mem_resp_vec,
        dout           => a_mem_resp_vec,
        valid          => a_mem_resp_valid
    );

    a_mem_resp.data <= a_mem_resp_vec(31 downto 0);
    a_mem_resp.dack_tag <= a_mem_resp_vec(39 downto 32) when a_mem_resp_valid = '1' else X"00";

end architecture;
