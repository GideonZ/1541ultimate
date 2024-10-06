-------------------------------------------------------------------------------
-- Title      : i2s_serializer
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: I2S Serializer / Deserializer
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

entity spdif_encoder is
port (
    clock               : in  std_logic; -- equals i2s_mclk
    reset               : in  std_logic;
    
    spdif_out           : out std_logic;

    sample_pulse        : in  std_logic; -- i2s encoder is master
    left_sample_in      : in  std_logic_vector(23 downto 0);
    right_sample_in     : in  std_logic_vector(23 downto 0) );
end entity;

architecture rtl of spdif_encoder is
    signal bit_count    : unsigned(7 downto 0);
    signal shift_reg    : std_logic_vector(27 downto 0) := (others => '0');

    signal toggle       : std_logic_vector(0 to 7);
    signal parity       : std_logic;
    signal frame_count  : integer range 0 to 191;
    signal spdif_i      : std_logic := '0';
    constant m_preamble : std_logic_vector(0 to 7) := "10010011"; -- '1' is toggle, '0' is not
    constant w_preamble : std_logic_vector(0 to 7) := "10010110";
    constant b_preamble : std_logic_vector(0 to 7) := "10011100";
    constant c_bits_left  : std_logic_vector(191 downto 0) := X"00000000_00000000_00000000_00000000_0000000A_04100004";
    constant c_bits_right : std_logic_vector(191 downto 0) := X"00000000_00000000_00000000_00000000_0000000A_04200004";
begin
    -- mclk = 256*fs. bclk = 64*fs (32 bits per channel)
    process(clock)
    begin
        if rising_edge(clock) then
            if sample_pulse = '1' then
                bit_count <= X"00";
            else
                bit_count <= bit_count + 1;
            end if;
            
            case bit_count is
            when X"00" =>
                if frame_count = 0 then
                    toggle <= b_preamble;
                else
                    toggle <= m_preamble;
                end if;

            when X"0E" => --  PCUV
                shift_reg <= "0000" & left_sample_in(23 downto 4) & X"0";
                shift_reg(26) <= c_bits_left(frame_count);
                parity <= '0';

            when X"7C" | X"FC" => -- parity
                toggle(1) <= parity;
                toggle(0) <= '1';

            when X"80" =>
                toggle <= w_preamble;

            when X"8E" => --  PCUV
                shift_reg <= "0000" & right_sample_in(23 downto 4) & X"0";
                shift_reg(26) <= c_bits_right(frame_count);
                parity <= '0';

            when X"FF" =>
                if frame_count = 191 then
                    frame_count <= 0;
                else
                    frame_count <= frame_count + 1;
                end if;

            when others =>
                if bit_count(6 downto 4) /= "000" and bit_count(1 downto 0) = "00" then -- data
                    toggle(0) <= '1';
                    toggle(1) <= shift_reg(shift_reg'right);
                    parity <= parity xor shift_reg(shift_reg'right);
                    shift_reg <= '0' & shift_reg(shift_reg'left downto shift_reg'right+1);
                end if;
            end case;

            if bit_count(0) = '1' then
                spdif_i <= spdif_i xor toggle(0);
                toggle <= toggle(1 to 7) & '0';
            end if;

            if reset='1' then
                bit_count <= (others => '0');
                frame_count <= 0;
                toggle <= (others => '0');
                spdif_i <= '0';
            end if;
        end if;
    end process;

    spdif_out <= spdif_i;

end architecture;
