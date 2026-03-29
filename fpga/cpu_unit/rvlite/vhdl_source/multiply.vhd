--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2026
--
-- Description: This module implements the multiply part of the "M"-extension
--              of the RiscV specification.
--
-- RV32M Multiply Extension
-- Inst   Name            FMT Opcode  funct3 funct7 Description (C)
-- mul    MUL             R   0110011 0x0    0x01   rd = (rs1 * rs2)[31:0]
-- mulh   MUL High        R   0110011 0x1    0x01   rd = (rs1 * rs2)[63:32]
-- mulsu  MUL High (S)(U) R   0110011 0x2    0x01   rd = (rs1 * rs2)[63:32]
-- mulu   MUL High (U)    R   0110011 0x3    0x01   rd = (rs1 * rs2)[63:32]
-- div    DIV             R   0110011 0x4    0x01   rd = rs1 / rs2
-- divu   DIV (U)         R   0110011 0x5    0x01   rd = rs1 / rs2
-- rem    Remainder       R   0110011 0x6    0x01   rd = rs1 % rs2
-- remu   Remainder (U)   R   0110011 0x7    0x01   rd = rs1 % rs2
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity multiply is
port (
    data_1      : in  std_logic_vector(31 downto 0); -- register rs1
    data_2      : in  std_logic_vector(31 downto 0); -- register rs2
    oper        : in  std_logic_vector(2 downto 0);
    data_out    : out std_logic_vector(63 downto 0)
);
end entity;

architecture gideon of multiply is
    signal sext_a   : signed(32 downto 0);
    signal sext_b   : signed(32 downto 0);
    signal result   : signed(65 downto 0);
begin
    sext_a(31 downto 0) <= signed(data_1);
    sext_b(31 downto 0) <= signed(data_2);

    process(data_1, data_2, oper)
    begin
        sext_a(32) <= data_1(31); -- default sign extended
        sext_b(32) <= data_2(31) and not oper(1); -- signed for 00 and 01; unsigned for 10 and 11
        if oper(1 downto 0) = "11" then
            sext_a(32) <= '0'; -- unsigned
        end if;
    end process;

    result <= sext_a * sext_b;

    data_out <= std_logic_vector(result(63 downto 0));
    -- data_out <= std_logic_vector(result(31 downto 0)) when oper(1 downto 0) = "00"
    --        else std_logic_vector(result(63 downto 32));

end architecture;
