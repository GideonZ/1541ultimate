-------------------------------------------------------------------------------
-- Title      : Mem Bus Clock Domain Bridge
-------------------------------------------------------------------------------
-- Description: This module implements a FIFO based clock domain bridge. 
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.mem_bus_pkg.all;
    
entity mem_bus_bridge is
port (
    a_clock     : in  std_logic;
    a_reset     : in  std_logic;
    a_mem_req   : in  t_mem_req_32;
    a_mem_resp  : out t_mem_resp_32;

    b_clock     : in  std_logic;
    b_reset     : in  std_logic;
    b_mem_req   : out t_mem_req_32;
    b_mem_resp  : in  t_mem_resp_32 );
    
end entity;    

architecture Gideon of mem_bus_bridge is
    signal a_mem_req_vec    : std_logic_vector(c_mem_bus_req_width-1 downto 0);
    signal a_mem_req_full   : std_logic;
    signal b_mem_req_vec    : std_logic_vector(c_mem_bus_req_width-1 downto 0);
    signal b_mem_req_valid  : std_logic;

    signal b_mem_resp_valid : std_logic;
    signal b_mem_resp_vec   : std_logic_vector(39 downto 0);
    signal a_mem_resp_vec   : std_logic_vector(39 downto 0);
    signal a_mem_resp_valid : std_logic;
begin
    -- Forward path
    a_mem_resp.rack_tag <= a_mem_req.tag when (a_mem_req_full = '0' and a_mem_req.request = '1') else X"00";
    a_mem_resp.rack     <= a_mem_req.request and not a_mem_req_full;
    a_mem_req_vec <= to_std_logic_vector(a_mem_req);
    
    i_forward_fifo: entity work.async_fifo_ft
    generic map (
        g_data_width => c_mem_bus_req_width,
        g_depth_bits => 3
    )
    port map (
        wr_clock => a_clock,
        wr_reset => a_reset,
        wr_en    => a_mem_req.request,
        wr_din   => a_mem_req_vec,
        wr_full  => a_mem_req_full,

        rd_clock => b_clock,
        rd_reset => b_reset,
        rd_next  => b_mem_resp.rack,
        rd_dout  => b_mem_req_vec,
        rd_valid => b_mem_req_valid
    );

    b_mem_req <= to_mem_req(b_mem_req_vec, b_mem_req_valid);

    -- Return Path
    b_mem_resp_valid <= '1' when b_mem_resp.dack_tag /= X"00" else '0';
    b_mem_resp_vec   <= b_mem_resp.dack_tag & b_mem_resp.data;

    i_return_fifo: entity work.async_fifo_ft
    generic map (
        g_data_width => a_mem_resp_vec'length,
        g_depth_bits => 3
    )
    port map (
        wr_clock => b_clock,
        wr_reset => b_reset,
        wr_en    => b_mem_resp_valid,
        wr_din   => b_mem_resp_vec,
        wr_full  => open, -- should't happen

        rd_clock => a_clock,
        rd_reset => a_reset,
        rd_next  => '1',
        rd_dout  => a_mem_resp_vec,
        rd_valid => a_mem_resp_valid
    );

    a_mem_resp.data <= a_mem_resp_vec(31 downto 0);
    a_mem_resp.dack_tag <= a_mem_resp_vec(39 downto 32) when a_mem_resp_valid = '1' else X"00";

end architecture;
