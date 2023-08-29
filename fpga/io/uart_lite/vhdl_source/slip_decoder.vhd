--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: The SLIP decoder takes an AXI stream of bytes and outputs
--              packets, by taking out the packet marker. It also decodes the
--              escape sequences. When slip mode is disabled, the packets are
--              formed by taking the '\r' as packet separator, so that each
--              line is seen as a packet. The pushback mechanism can be used
--              to control the hardware handshake on a uart.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity slip_decoder is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    slip_enable : in  std_logic;

    in_data     : in  std_logic_vector(7 downto 0);
    in_valid    : in  std_logic;
    in_ready    : out std_logic;

    out_data    : out std_logic_vector(7 downto 0) := X"00";
    out_last    : out std_logic;
    out_valid   : out std_logic;
    out_ready   : in  std_logic
);
end entity;

architecture rtl of slip_decoder is
    type t_state is (ascii, sync, start, packet, escape);
    signal state        : t_state;
    signal out_valid_i  : std_logic;
    signal transfer     : std_logic;
    signal data_reg     : std_logic_vector(7 downto 0) := X"00";
begin
    out_valid <= out_valid_i;
    transfer  <= in_valid and (out_ready or not out_valid_i);
    in_ready  <= out_ready or not out_valid_i;

    process(clock)
    begin
        if rising_edge(clock) then
            if out_ready = '1' then
                out_valid_i <= '0';
                out_last    <= '0';
            end if;
            case state is
            when ascii =>
                if slip_enable = '1' then
                    state <= sync;
                elsif transfer = '1' then
                    out_data <= in_data;
                    out_valid_i <= '1';
                    if in_data = X"0A" then
                        out_last <= '1';
                    end if;
                end if;

            when sync =>
                if slip_enable = '0' then
                    state <= ascii;
                elsif transfer = '1' and in_data = X"C0" then
                    state <= start;
                end if;

            when start =>
                if slip_enable = '0' then
                    state <= ascii;
                elsif transfer = '1' then
                    data_reg <= in_data;
                    if in_data = X"C0" then
                        state <= start;
                    elsif in_data = X"DB" then
                        state <= escape;
                    else
                        state <= packet;
                    end if;
                end if;

            when packet =>
                if slip_enable = '0' then
                    state <= ascii;
                elsif transfer = '1' then
                    out_data    <= data_reg;
                    out_valid_i <= '1';
                    data_reg    <= in_data;

                    if in_data = X"C0" then
                        out_last <= '1';
                        state <= start;
                    elsif in_data = X"DB" then
                        state <= escape;
                    end if;
                end if;

            when escape =>
                if slip_enable = '0' then
                    state <= ascii;
                elsif transfer = '1' then
                    state <= packet;
                    if in_data = X"DC" then
                        data_reg <= X"C0";
                    elsif in_data = X"DD" then
                        data_reg <= X"DB";
                    else
                        data_reg <= in_data;
                    end if;
                end if;

            when others =>
                null;
            end case;

            if reset = '1' then
                state <= ascii;
                out_valid_i <= '0';
                out_last <= '0';
            end if;
        end if;
    end process;
end architecture;
