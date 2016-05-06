-------------------------------------------------------------------------------
-- Title      : i2s_serializer
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: I2S Serializer / Deserializer
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;


entity i2s_serializer is
port (
    clock               : in  std_logic; -- equals i2s_mclk
    reset               : in  std_logic;
    
    i2s_out             : out std_logic;
    i2s_in              : in  std_logic;
    i2s_bclk            : out std_logic;
    i2s_fs              : out std_logic;
    
    stream_out_data     : out std_logic_vector(23 downto 0);
    stream_out_tag      : out std_logic_vector(0 downto 0);
    stream_out_valid    : out std_logic;
    
    stream_in_data      : in  std_logic_vector(23 downto 0);
    stream_in_tag       : in  std_logic_vector(0 downto 0);
    stream_in_valid     : in  std_logic;
    stream_in_ready     : out std_logic );

end entity;

architecture rtl of i2s_serializer is
    signal bit_count    : unsigned(7 downto 0);
    signal shift_reg    : std_logic_vector(31 downto 0) := (others => '0');
begin
    -- mclk = 256*fs. bclk = 64*fs (32 bits per channel)
    process(clock)
    begin
        if rising_edge(clock) then
            bit_count <= bit_count + 1;
            i2s_bclk  <= bit_count(1);
            i2s_fs    <= bit_count(7); 
            stream_in_ready  <= '0';
            stream_out_valid <= '0';
            
            if bit_count(1 downto 0) = "00" then
                i2s_out <= shift_reg(shift_reg'left);
            elsif bit_count(1 downto 0) = "10" then
                shift_reg <= shift_reg(shift_reg'left-1 downto 0) & i2s_in;
            end if;            
            if bit_count(6 downto 0) = "1111110" then
                stream_out_data   <= shift_reg(shift_reg'left-2 downto shift_reg'left - 25);
                stream_out_tag(0) <= bit_count(7);
                stream_out_valid  <= '1';
                if stream_in_valid = '1' and ((bit_count(7) xor stream_in_tag(0)) = '1') then
                    shift_reg <= '0' & stream_in_data & "0000000";
                    stream_in_ready <= '1';
                else
                    shift_reg <= (others => '0');
                end if;
            end if;
            
            if reset='1' then
                bit_count <= (others => '1');
                i2s_out   <= '0';
                stream_in_ready <= '0';
                stream_out_valid <= '0';
            end if;
        end if;
    end process;
end architecture;
