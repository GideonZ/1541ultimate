--------------------------------------------------------------------------------
-- Entity: spdif_tb
-- Date:2024-10-05  
-- Author: gideon     
--
-- Description: Testbench for spdif module
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spdif_tb is
end entity;

architecture tb of spdif_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal i2s         : std_logic;
    signal i2s_bclk    : std_logic;
    signal i2s_fs      : std_logic;
    signal spdif       : std_logic;
    signal sample_pulse     : std_logic;
    signal left_sample_out  : std_logic_vector(23 downto 0);
    signal right_sample_out : std_logic_vector(23 downto 0);
    signal left_sample_in   : std_logic_vector(23 downto 0) := X"F12345";
    signal right_sample_in  : std_logic_vector(23 downto 0) := X"6789AB";
    signal bit_count        : natural range 0 to 71;
    signal sampled          : std_logic_vector(71 downto 0);
    signal edge             : std_logic_vector(71 downto 0);
    signal even             : std_logic_vector(35 downto 0);
    signal odd              : std_logic_vector(35 downto 0);
    signal sync             : character;
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i2s_serializer_inst: entity work.i2s_serializer
    port map (
        clock            => clock,
        reset            => reset,
        i2s_out          => i2s,
        i2s_in           => i2s,
        i2s_bclk         => i2s_bclk,
        i2s_fs           => i2s_fs,
        sample_pulse     => sample_pulse,
        left_sample_out  => left_sample_out,
        right_sample_out => right_sample_out,
        left_sample_in   => left_sample_in,
        right_sample_in  => right_sample_in
    );

    spdif_encoder_inst: entity work.spdif_encoder
    port map (
        clock           => clock,
        reset           => reset,
        spdif_out       => spdif,
        sample_pulse    => sample_pulse,
        left_sample_in  => left_sample_in,
        right_sample_in => right_sample_in
    );

    process(i2s_bclk)
        variable last_8 : std_logic_vector(7 downto 0);
        procedure bmcm is
        begin
            for i in 0 to 35 loop
                even(i) <= edge(2*i);
            end loop;
        end procedure;
        procedure bmcw is
        begin
            for i in 0 to 35 loop
                odd(i) <= edge(2*i);
            end loop;
        end procedure;
    begin
        if i2s_bclk'event then -- both edges
            sampled(bit_count) <= spdif;
            last_8 := last_8(6 downto 0) & spdif;
            --sync <= ' ';
            if last_8 = "11101000" or last_8 = "00010111" then -- B
                bit_count <= 8;
                sync <= 'B';
                bmcm;
            elsif last_8 = "11100010" or last_8 = "00011101" then -- M
                bit_count <= 8;
                sync <= 'M';
                bmcm;
            elsif last_8 = "11100100" or last_8 = "00011011" then -- W
                sync <= 'W';
                bit_count <= 8;
                bmcw;
            elsif bit_count /= 71 then
                bit_count <= bit_count + 1;
            end if;
        end if;
    end process;
    edge <= sampled xor (sampled(71) & sampled(71 downto 1));

end architecture;

