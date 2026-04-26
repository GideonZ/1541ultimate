--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: The SLIP encoder takes an AXI packet stream and outputs
--              a slip stream, no pun intended. It encodes the special slip 
--              bytes by inserting escape characters where necessary.
--              When slip mode is disabled, the bytes are passed through
--              unmodified and the 'last' input is don't care.
--              The pushback mechanism can be used to take the handshake from
--              the uart to pause transmission.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity slip_encoder is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    slip_enable : in  std_logic;

    in_data     : in  std_logic_vector(7 downto 0);
    in_last     : in  std_logic;
    in_valid    : in  std_logic;
    in_ready    : out std_logic;

    out_data    : out std_logic_vector(7 downto 0) := X"00";
    out_valid   : out std_logic;
    out_ready   : in  std_logic
);
end entity;

architecture rtl of slip_encoder is
    type t_state is (ascii, sync, start, packet, escape, close);
    signal state        : t_state;
    signal out_valid_i  : std_logic;
    signal transfer     : std_logic;
    signal esc_flag     : std_logic;
    signal last_reg     : std_logic;
begin
    out_valid <= out_valid_i;
    transfer  <= in_valid and (out_ready or not out_valid_i);

    with state select in_ready <=
        '0' when start,
        '0' when close,
        '0' when escape,
        out_ready or not out_valid_i when others;

    process(clock)
    begin
        if rising_edge(clock) then
            if out_ready = '1' then
                out_valid_i <= '0';
            end if;
            case state is
            when ascii =>
                if slip_enable = '1' then
                    state <= start;
                elsif transfer = '1' then
                    out_data <= in_data;
                    out_valid_i <= '1';
                end if;

            when start =>
                if slip_enable = '0' then
                    state <= ascii;
                elsif transfer = '1' then
                    out_data <= X"C0";
                    out_valid_i <= '1';
                    state <= packet;
                end if;
            
            when packet =>
                if slip_enable = '0' then
                    state <= close;
                elsif out_ready = '1' or out_valid_i = '0' then
                    if in_data = X"C0" then
                        out_data <= X"DB";
                        out_valid_i <= '1';
                        esc_flag <= '0';
                        last_reg <= in_last;
                        state <= escape;
                    elsif in_data = X"DB" then
                        out_data <= X"DB";
                        out_valid_i <= '1';
                        esc_flag <= '1';
                        last_reg <= in_last;
                        state <= escape;
                    else
                        out_data <= in_data;
                        out_valid_i <= '1';
                        if in_last = '1' then
                            state <= close;
                        end if;
                    end if;
                end if;

            when escape =>
                if out_ready = '1' or out_valid_i = '0' then
                    out_data <= X"DC";
                    out_data(0) <= esc_flag; -- switches between DC and DD
                    out_valid_i <= '1';
                    if last_reg = '1' then
                        state <= close;
                    else
                        state <= packet;
                    end if;
                end if;

            when close =>
                if out_ready = '1' or out_valid_i = '0' then
                    out_data <= X"C0";
                    out_valid_i <= '1';
                    state <= start;
                end if;

            when others =>
                null;
            end case;

            if reset = '1' then
                state <= ascii;
                out_valid_i <= '0';
                esc_flag <= '0';
                last_reg <= '0';
            end if;
        end if;
    end process;
end architecture;
