-------------------------------------------------------------------------------
-- Title      : packet_generator
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This is a module just to test the hardware
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity packet_generator is
port (
    clock               : in  std_logic; -- 50 MHz RMII clock
    reset               : in  std_logic;

    eth_tx_data         : out std_logic_vector(7 downto 0);
    eth_tx_sof          : out std_logic;
    eth_tx_eof          : out std_logic;
    eth_tx_valid        : out std_logic;
    eth_tx_ready        : in  std_logic );

end entity;

architecture rtl of packet_generator is
    signal delay        : integer range 0 to 50000000;
    signal byte_count   : integer range 0 to 63;
    type t_state is (idle, transmit);
    signal state        : t_state; 
    signal valid_i      : std_logic;
    signal eof          : std_logic;

    type t_byte_array is array (natural range <>) of std_logic_vector(7 downto 0);
    constant c_packet : t_byte_array := (
        X"00", X"22", X"15", X"32", X"BA", X"48", -- dest mac to desktop PC
        X"00", X"01", X"23", X"45", X"67", X"89", -- source mac from local PHY
        X"08", X"00", -- ethernet frame
        -- IP
        X"45", X"00", -- standard IP frame
        X"00", X"30", -- total length = 20 + 8 + 20 = 48
        X"12", X"34", -- ID
        X"00", X"00", -- flags & fragment offset
        X"20", X"11", -- TTL = 32, protocol = UDP
        X"05", X"DA", -- checksum
        X"C0", X"A8", X"00", X"F5", -- 192.168.0.245 (source)
        X"C0", X"A8", X"00", X"6A", -- 192.168.0.106 (dest)
        -- UDP
        X"27", X"10", X"4E", X"20", --- port 10000 to 20000
        X"00", X"1C", -- length
        X"00", X"00", -- checksum
        -- Payload
        X"48", X"65", X"78", X"3A",
        X"30", X"31", X"32", X"33", X"34", X"35", X"36", X"37",
        X"38", X"39", X"41", X"42", X"43", X"44", X"45", X"46"        
    );
begin
    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                byte_count <= 0;
                eof <= '0';
                if delay = 50000000 then
                    state <= transmit;
                    delay <= 0;
                else
                    delay <= delay + 1;
                end if;
            
            when transmit =>
                if valid_i = '0' or eth_tx_ready = '1' then
                    if eof = '1' then
                        valid_i <= '0';
                        state <= idle;
                    else
                        eth_tx_data <= c_packet(byte_count);
                        if byte_count = 0 then
                            eth_tx_sof <= '1';
                        else
                            eth_tx_sof <= '0';
                        end if;
                        if byte_count = c_packet'right then
                            eof <= '1';
                        else
                            eof <= '0';
                        end if;
                        valid_i <= '1';
                        byte_count <= byte_count + 1;
                    end if;
                end if;
            end case;                              

            if reset='1' then
                eth_tx_sof <= '0';
                valid_i <= '0';
                eof <= '0';
                delay <= 0;
                byte_count <= 0;
                state <= idle;
            end if;
        end if;
    end process;

    eth_tx_valid <= valid_i;
    eth_tx_eof   <= eof;
    
end architecture;
