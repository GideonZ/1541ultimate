-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : synchroniser block
-------------------------------------------------------------------------------
-- Description: synchroniser block implementing the synchroniser described in
--              Rino Goslars paper:"Fourteen ways to fool your synchroniser"
--
--              Add a timing-ignore on all _tig signals in this module.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity synchroniser is
    
    generic (
        g_data_width : natural := 16);

    port (
        tx_clock    : in  std_logic;
        tx_reset    : in  std_logic;
        tx_push     : in  std_logic;
        tx_data     : in  std_logic_vector(g_data_width - 1 downto 0);
        tx_done     : out std_logic;
        rx_clock    : in  std_logic;
        rx_reset    : in  std_logic;
        rx_new_data : out std_logic;
        rx_data     : out std_logic_vector(g_data_width - 1 downto 0));

    ---------------------------------------------------------------------------
    -- synthesis attributes to prevent duplication and balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_duplication                 : string;
    attribute register_duplication of synchroniser : entity is "no";
    attribute register_balancing                   : string;
    attribute register_balancing of synchroniser   : entity is "no";
    -- Altera attributes
    attribute dont_replicate                       : boolean;
    attribute dont_replicate of synchroniser       : entity is true;
    attribute dont_retime                          : boolean;
    attribute dont_retime of synchroniser          : entity is true;
    
end synchroniser;

architecture rtl of synchroniser is
    type t_tx_fsm_state is (tx_idle, tx_req, tx_waiting);
    type t_rx_fsm_state is (rx_idle, rx_ack);

    signal tx_fsm_state : t_tx_fsm_state;
    signal rx_fsm_state : t_rx_fsm_state;

    signal ack        : std_logic;
    signal ack_d1_tig : std_logic;
    signal ack_d2     : std_logic;
    signal ack_d3     : std_logic;

    signal req        : std_logic;
    signal req_d1_tig : std_logic;
    signal req_d2     : std_logic;
    signal req_d3     : std_logic;

    signal tx_data_i   : std_logic_vector(g_data_width - 1 downto 0);
    signal rx_data_tig : std_logic_vector(g_data_width - 1 downto 0);

begin  -- rtl

    transmitter : process (tx_clock)
    begin  -- process transmitter
        if tx_clock'event and tx_clock = '1' then  -- rising clock edge
            ack_d1_tig <= ack;                     -- async boundary
            ack_d2     <= ack_d1_tig;
            ack_d3     <= ack_d2;
            tx_done    <= not ack_d3 and ack_d2;
            case tx_fsm_state is
                when tx_idle =>
                    if tx_push = '1' then
                        tx_fsm_state <= tx_req;
                        req          <= '1';
                        tx_data_i    <= tx_data;
                    else
                        req <= '0';
                    end if;
                when tx_req =>
                    if (ack_d2 = '1') then
                        tx_fsm_state <= tx_waiting;
                        req          <= '0';
                    end if;
                when tx_waiting =>
                    if ack_d2 = '0' then
                        tx_fsm_state <= tx_idle;
                    end if;
                when others =>
                    tx_fsm_state <= tx_idle;
                    req          <= '0';
            end case;
            if tx_reset = '1' then
                ack_d1_tig   <= '0';
                ack_d2       <= '0';
                ack_d3       <= '0';
                tx_done      <= '0';
                tx_fsm_state <= tx_idle;
                tx_data_i    <= (others => '0');
            end if;
        end if;
    end process transmitter;

    receiver : process (rx_clock)
    begin  -- process receiver
        if rx_clock'event and rx_clock = '1' then   -- rising clock edge
            req_d1_tig  <= req;                     -- async boundary
            req_d2      <= req_d1_tig;
            req_d3      <= req_d2;
            rx_new_data <= not req_d3 and req_d2;
            case rx_fsm_state is
                when rx_idle =>
                    if req_d2 = '1' then
                        rx_fsm_state <= rx_ack;
                        ack          <= '1';
                        rx_data_tig  <= tx_data_i;  -- async boundary
                    else
                        ack <= '0';
                    end if;
                when rx_ack =>
                    if req_d2 = '0' then
                        rx_fsm_state <= rx_idle;
                        ack          <= '0';
                    end if;
                when others =>
                    rx_fsm_state <= rx_idle;
                    ack          <= '0';
            end case;
            if rx_reset = '1' then
                req_d1_tig   <= '0';
                req_d2       <= '0';
                req_d3       <= '0';
                rx_new_data  <= '0';
                rx_data_tig  <= (others => '0');
                rx_fsm_state <= rx_idle;
            end if;
        end if;
    end process receiver;

    rx_data <= rx_data_tig;
    
end rtl;
