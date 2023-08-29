--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Testbench for the slip encoder and decoder.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_slip is
end entity;

architecture test of tb_slip is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal in_data     : std_logic_vector(7 downto 0);
    signal in_valid    : std_logic;
    signal in_last     : std_logic;
    signal in_ready    : std_logic;
    signal slip_data   : std_logic_vector(7 downto 0);
    signal slip_valid  : std_logic;
    signal slip_ready  : std_logic;
    signal out_data    : std_logic_vector(7 downto 0) := X"00";
    signal out_last    : std_logic;
    signal out_valid   : std_logic;
    signal out_ready   : std_logic;
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    slip_encoder_inst: entity work.slip_encoder
        port map (
            clock       => clock,
            reset       => reset,
            slip_enable => '1',
            in_data     => in_data,
            in_last     => in_last,
            in_valid    => in_valid,
            in_ready    => in_ready,
            out_data    => slip_data,
            out_valid   => slip_valid,
            out_ready   => slip_ready
        );

    slip_decoder_inst: entity work.slip_decoder
        port map (
            clock       => clock,
            reset       => reset,
            slip_enable => '1',
            in_data     => slip_data,
            in_valid    => slip_valid,
            in_ready    => slip_ready,
            out_data    => out_data,
            out_last    => out_last,
            out_valid   => out_valid,
            out_ready   => out_ready
        );

    process
        procedure send_data(a : std_logic_vector; last : boolean) is
        begin
            in_data <= a;
            in_valid <= '1';
            if last then
                in_last <= '1';
            else
                in_last <= '0';
            end if;
            wait until clock = '1';
            while in_ready = '0' loop
                wait until clock = '1';
            end loop;
            in_valid <= '0';
        end procedure;
    begin
        in_valid <= '0';
        out_ready <= '1';
        wait until reset = '0';
        wait until clock = '1';
        for i in 0 to 255 loop
            send_data(std_logic_vector(to_unsigned(i, 8)), i = 255);
        end loop;            
        for i in 192 to 255 loop
            send_data(std_logic_vector(to_unsigned(i, 8)), i = 255);
        end loop;            
        for i in 192 to 219 loop
            send_data(std_logic_vector(to_unsigned(i, 8)), i = 219);
        end loop;            
        send_data(std_logic_vector(to_unsigned(0, 8)), true);
        send_data(std_logic_vector(to_unsigned(1, 8)), true);
        send_data(std_logic_vector(to_unsigned(192, 8)), true);
        send_data(std_logic_vector(to_unsigned(219, 8)), true);
        send_data(std_logic_vector(to_unsigned(2, 8)), true);
        wait;
    end process;

end architecture;