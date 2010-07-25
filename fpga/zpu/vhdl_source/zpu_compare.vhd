library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- oper input can have the following values, producing the following results

-- 000 => false
-- 001 => a = b
-- 010 => false
-- 011 => a = b

-- 100 => a < b
-- 101 => a <= b
-- 110 => a < b (unsigned)
-- 111 => a <= b (unsigned)

entity zpu_compare is
port (
    a       : in  unsigned(31 downto 0);
    b       : in  unsigned(31 downto 0);
    oper    : in  std_logic_vector(2 downto 0);
    y       : out boolean );
end zpu_compare;

architecture gideon of zpu_compare is
    signal result   : boolean;
    signal equal    : boolean;
    signal ext_a    : signed(32 downto 0);
    signal ext_b    : signed(32 downto 0);
begin
    equal <= (a = b);
    
    ext_a(32) <= not oper(1) and a(31); -- if oper(1) is 1, then we'll do an unsigned compare = signed compare with '0' in front.
    ext_b(32) <= not oper(1) and b(31); -- if oper(1) is 0, when we'll do a signed compare = extended signed with sign bit.
    ext_a(31 downto 0) <= signed(a);
    ext_b(31 downto 0) <= signed(b);
    
    result <= (ext_a < ext_b);
    process(oper, result, equal)
        variable r : boolean;
    begin
        r := false;
        if oper(0)='1' then
            r := r or equal;
        end if;
        if oper(2)='1' then
            r := r or result;
        end if;
        y <= r;
    end process;

end gideon;

--   constant OPCODE_LESSTHAN         : unsigned(5 downto 0):=to_unsigned(36,6); -- 100100
--   constant OPCODE_LESSTHANOREQUAL  : unsigned(5 downto 0):=to_unsigned(37,6); -- 100101
--   constant OPCODE_ULESSTHAN        : unsigned(5 downto 0):=to_unsigned(38,6); -- 100110
--   constant OPCODE_ULESSTHANOREQUAL : unsigned(5 downto 0):=to_unsigned(39,6); -- 100111

-- Note, the mapping is such, that for the opcodes above, the lower three bits of the opcode can be fed directly
-- into the compare unit.

--   constant OPCODE_EQ               : unsigned(5 downto 0):=to_unsigned(46,6); -- 101110
--   constant OPCODE_NEQ              : unsigned(5 downto 0):=to_unsigned(47,6); -- 101111

-- For these operations, the decoder must do extra work.
-- TODO: make a smarter mapping to support EQ and NEQ without external decoding.
