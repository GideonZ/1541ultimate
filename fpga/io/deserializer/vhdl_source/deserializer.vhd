
library ieee;
use ieee.std_logic_1164.all;

entity deserializer is
port (
    clock       : in  std_logic;
    sync        : in  std_logic;
    rxd         : in  std_logic;
    txd         : out std_logic;
    
    io          : inout std_logic_vector(11 downto 0) );
end deserializer;

architecture gideon of deserializer is
    signal index    : integer range 0 to 31;
    signal io_t     : std_logic_vector(io'range) := (others => '0');
    signal io_o     : std_logic_vector(io'range) := (others => '0');
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if sync='1' then
                index <= 0;
            elsif index /= 31 then
                index <= index + 1;
            end if;

            if index <= io'high then
                txd   <= io(index);
            else
                txd   <= '0';
            end if;
            
            if index <= io'high then
                io_o(index) <= rxd;
            elsif index < (2*io'length) then
                io_t(index - io'length) <= rxd;
            end if;
        end if;
    end process;
    
    r_out: for i in io'range generate
        io(i) <= io_o(i) when io_t(i)='1' else 'Z';
    end generate;
end gideon;
