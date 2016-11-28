library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mem_remain_counter is
port (
    clock       : in  std_logic;
    load_value  : in  unsigned(7 downto 0);
    do_load     : in  std_logic;
    do_dec      : in  std_logic;
    
    remain      : out unsigned(7 downto 0);
    remain_is_1 : out std_logic;
    remain_is_0 : out std_logic );
    
end mem_remain_counter;

architecture test of mem_remain_counter is
    signal rem_i    : unsigned(remain'range) := (others => '0');
    signal small    : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if do_load='1' then
                rem_i <= load_value;
            elsif do_dec='1' then
                rem_i <= rem_i - 1;
            end if;
        end if;
    end process;

    remain       <= rem_i;
    small        <= '1' when (rem_i(rem_i'high downto 2) = 0) else '0';
    remain_is_0  <= small when (rem_i(1 downto 0) = 0) else '0';
    remain_is_1  <= small when (rem_i(1 downto 0) = 1) else '0';
--    remain_less4 <= small;
    
end architecture;
