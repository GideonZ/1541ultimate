library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity serial_debug is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    inputs      : in  std_logic_vector(7 downto 0);

    out_data    : out std_logic_vector(31 downto 0);
    out_last    : out std_logic;
    out_valid   : out std_logic;
    out_ready   : in  std_logic := '1'
);
    
end entity;

architecture gideon of serial_debug is
    signal inputs_c     : std_logic_vector(7 downto 0);
    signal inputs_d     : std_logic_vector(7 downto 0);
    signal counter      : unsigned(22 downto 0);
    signal out_valid_i  : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            inputs_c <= inputs;
            inputs_d <= inputs_c;
            if out_ready = '1' then
                out_valid_i <= '0';
                out_data(23) <= '0';
            end if;
            if (inputs_c /= inputs_d) or (signed(counter) = -1) then
                out_data(31 downto 24) <= inputs_c;
                out_data(22 downto 0)  <= std_logic_vector(counter);
                if out_valid_i = '1' and out_ready = '0' then -- overflow!
                    out_data(23) <= '1';
                end if;
                out_valid_i <= '1';
                counter <= (0 => '1', others => '0');
            else
                counter <= counter + 1;
            end if;
            if reset = '1' then
                counter <= (others => '0');
                out_valid_i <= '0';
                out_data(23) <= '0';
            end if;
        end if;
    end process;

    out_last <= '0';
    out_valid <= out_valid_i;

end gideon;
