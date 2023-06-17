--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: The alu_logical is a fully combinatorial implementation of the
--              logic functions AND, OR and XOR. Interestingly, the total ALU
--              became smaller when these functions were extracted from the main
--              multiplexer. - at least for architectures with 4-input LUTs.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity alu_logical is
port (
    data_1      : in  std_logic_vector(31 downto 0);
    data_2      : in  std_logic_vector(31 downto 0); 
    oper        : in  std_logic_vector(1 downto 0);
    data_out    : out std_logic_vector(31 downto 0)
);
end entity;

architecture gideon of alu_logical is
    signal result_xor   : std_logic_vector(31 downto 0);
    signal result_and   : std_logic_vector(31 downto 0);
    signal result_or    : std_logic_vector(31 downto 0); 
begin
    result_xor <= data_1 xor data_2;
    result_and <= data_1 and data_2;
    result_or  <= data_1 or  data_2;


    with oper select data_out <=
        result_xor   when "00",
        result_or    when "10",
        result_and   when "11",
        X"00000000"  when others;

end architecture;
