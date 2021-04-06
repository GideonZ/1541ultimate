-------------------------------------------------------------------------------
-- Title      : eth_debug_stream
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Merges bus debug data to ethernet Tx stream for U2P (Dummy!)
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use work.io_bus_pkg.all;

entity eth_debug_stream is
port (
    eth_clock           : in  std_logic;
    eth_reset           : in  std_logic;
    
    eth_u2p_data        : in  std_logic_vector(7 downto 0);
    eth_u2p_last        : in  std_logic;
    eth_u2p_valid       : in  std_logic;
    eth_u2p_ready       : out std_logic;

    eth_tx_data         : out std_logic_vector(7 downto 0);
    eth_tx_last         : out std_logic;
    eth_tx_valid        : out std_logic;
    eth_tx_ready        : in  std_logic := '1';

    --
    sys_clock           : in  std_logic;
    sys_reset           : in  std_logic;
    io_req              : in  t_io_req;
    io_resp             : out t_io_resp;

    c64_debug_select    : out std_logic_vector(2 downto 0);
    c64_debug_data      : in  std_logic_vector(31 downto 0);
    c64_debug_valid     : in  std_logic;
    drv_debug_data      : in  std_logic_vector(31 downto 0);
    drv_debug_valid     : in  std_logic;

    IEC_ATN             : in  std_logic;
    IEC_CLOCK           : in  std_logic;
    IEC_DATA            : in  std_logic );

end entity;

architecture structural of eth_debug_stream is
begin
    -- Because the Ethernet Debug streamer uses propriatary IP, this
    -- module is simply a dummy; indicating that it does not support
    -- this feature. The network TX connection is simply passed through.  

    c64_debug_select <= "000";

    -- Direct Connection
    eth_tx_data      <= eth_u2p_data;
    eth_tx_last      <= eth_u2p_last;
    eth_tx_valid     <= eth_u2p_valid;
    eth_u2p_ready    <= eth_tx_ready;

    process(sys_clock)
    begin
        if rising_edge(sys_clock) then
            io_resp <= c_io_resp_init;
            io_resp.ack <= io_req.read or io_req.write;
        end if;
    end process;
end architecture;
