library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.gray_code_pkg.all;

entity slot_measure is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    phi2        : in  std_logic;
    addr        : in  unsigned(15 downto 0);
    
    data_out    : out std_logic_vector(7 downto 0)
);
end entity;

architecture rtl of slot_measure is
    signal phi2_d       : std_logic := '0';
    signal addr_d       : unsigned(15 downto 0);
    signal phi2_gray    : t_gray(6 downto 0);
    signal falling_edge : unsigned(6 downto 0);
    signal last_addr    : unsigned(6 downto 0);
begin
    process(clock)
        variable v_count    : unsigned(6 downto 0);
    begin
        if rising_edge(clock) then
            phi2_d <= phi2; -- phi2 is already synchornized
            addr_d <= addr;
            if phi2 = '1' and phi2_d = '0' then -- rising edge detection
                v_count := (others => '0');
            else
                v_count := v_count + 1;
            end if;
            phi2_gray <= to_gray(v_count);
            if phi2 = '0' and phi2_d = '1' then -- falling edge detection
                falling_edge <= v_count;
            end if;
            if addr_d /= addr then
                last_addr <= v_count;
            end if;
        end if;
    end process;
    with addr(4 downto 3) select data_out <=
        phi2 & std_logic_vector(phi2_gray) when "00",
        phi2 & std_logic_vector(last_addr) when "01",
        phi2 & std_logic_vector(falling_edge) when "10",
        phi2 & "0000000" when others;

end architecture;
