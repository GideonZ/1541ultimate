-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2007 TECHNOLUTION BV, GOUDA NL
-- | =======          I                   ==          I    =
-- |    I             I                    I          I
-- |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
-- |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
-- |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
-- |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
-- |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
-- |                 +---------------------------------------------------+
-- +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
--      |            |             ++++++++++++++++++++++++++++++++++++++|
--      +------------+                          +++++++++++++++++++++++++|
--                                                         ++++++++++++++|
--              A U T O M A T I O N     T E C H N O L O G Y         +++++|
--
-------------------------------------------------------------------------------
-- Title      : tl_vector_pkg
-- Author     : Edwin Hakkennes  <Edwin.Hakkennes@Technolution.NL>
-------------------------------------------------------------------------------
-- Description: tl vector package, types and functions on vectors (of vectors)
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

package tl_vector_pkg is

    -- type to define vector ranges. Use left and right instead of high and low, to
    -- make it possible to encode both 'to' and 'downto' vectors.
    type t_vector_range is
        record
            left    : integer;
            right   : integer;
        end record;

-------------------------------------------------------------------------------
-- arrays of vectors
-------------------------------------------------------------------------------

    type t_std_logic_1_vector is array (integer range <>) of std_logic_vector(0 downto 0);
    type t_std_logic_2_vector is array (integer range <>) of std_logic_vector(1 downto 0);
    type t_std_logic_3_vector is array (integer range <>) of std_logic_vector(2 downto 0);
    type t_std_logic_4_vector is array (integer range <>) of std_logic_vector(3 downto 0);
    type t_std_logic_5_vector is array (integer range <>) of std_logic_vector(4 downto 0);
    type t_std_logic_6_vector is array (integer range <>) of std_logic_vector(5 downto 0);
    type t_std_logic_7_vector is array (integer range <>) of std_logic_vector(6 downto 0);
    type t_std_logic_8_vector is array (integer range <>) of std_logic_vector(7 downto 0);
    type t_std_logic_9_vector is array (integer range <>) of std_logic_vector(8 downto 0);
    type t_std_logic_10_vector is array (integer range <>) of std_logic_vector(9 downto 0);
    type t_std_logic_11_vector is array (integer range <>) of std_logic_vector(10 downto 0);
    type t_std_logic_12_vector is array (integer range <>) of std_logic_vector(11 downto 0);
    type t_std_logic_13_vector is array (integer range <>) of std_logic_vector(12 downto 0);
    type t_std_logic_14_vector is array (integer range <>) of std_logic_vector(13 downto 0);
    type t_std_logic_15_vector is array (integer range <>) of std_logic_vector(14 downto 0);
    type t_std_logic_16_vector is array (integer range <>) of std_logic_vector(15 downto 0);
    type t_std_logic_17_vector is array (integer range <>) of std_logic_vector(16 downto 0);
    type t_std_logic_18_vector is array (integer range <>) of std_logic_vector(17 downto 0);
    type t_std_logic_19_vector is array (integer range <>) of std_logic_vector(18 downto 0);
    type t_std_logic_20_vector is array (integer range <>) of std_logic_vector(19 downto 0);
    type t_std_logic_21_vector is array (integer range <>) of std_logic_vector(20 downto 0);
    type t_std_logic_22_vector is array (integer range <>) of std_logic_vector(21 downto 0);
    type t_std_logic_23_vector is array (integer range <>) of std_logic_vector(22 downto 0);
    type t_std_logic_24_vector is array (integer range <>) of std_logic_vector(23 downto 0);
    type t_std_logic_25_vector is array (integer range <>) of std_logic_vector(24 downto 0);
    type t_std_logic_26_vector is array (integer range <>) of std_logic_vector(25 downto 0);
    type t_std_logic_27_vector is array (integer range <>) of std_logic_vector(26 downto 0);
    type t_std_logic_28_vector is array (integer range <>) of std_logic_vector(27 downto 0);
    type t_std_logic_29_vector is array (integer range <>) of std_logic_vector(28 downto 0);
    type t_std_logic_30_vector is array (integer range <>) of std_logic_vector(29 downto 0);
    type t_std_logic_31_vector is array (integer range <>) of std_logic_vector(30 downto 0);
    type t_std_logic_32_vector is array (integer range <>) of std_logic_vector(31 downto 0);
    type t_std_logic_36_vector is array (integer range <>) of std_logic_vector(35 downto 0);
    type t_std_logic_48_vector is array (integer range <>) of std_logic_vector(47 downto 0);
    type t_std_logic_56_vector is array (integer range <>) of std_logic_vector(55 downto 0);
    type t_std_logic_64_vector is array (integer range <>) of std_logic_vector(63 downto 0);
    type t_std_logic_128_vector is array (integer range <>) of std_logic_vector(127 downto 0);
    type t_std_logic_256_vector is array (integer range <>) of std_logic_vector(255 downto 0);
    type t_std_logic_512_vector is array (integer range <>) of std_logic_vector(511 downto 0);

    type t_signed_1_vector is array (integer range <>) of signed(0 downto 0);
    type t_signed_2_vector is array (integer range <>) of signed(1 downto 0);
    type t_signed_3_vector is array (integer range <>) of signed(2 downto 0);
    type t_signed_4_vector is array (integer range <>) of signed(3 downto 0);
    type t_signed_5_vector is array (integer range <>) of signed(4 downto 0);
    type t_signed_6_vector is array (integer range <>) of signed(5 downto 0);
    type t_signed_7_vector is array (integer range <>) of signed(6 downto 0);
    type t_signed_8_vector is array (integer range <>) of signed(7 downto 0);
    type t_signed_9_vector is array (integer range <>) of signed(8 downto 0);
    type t_signed_10_vector is array (integer range <>) of signed(9 downto 0);
    type t_signed_11_vector is array (integer range <>) of signed(10 downto 0);
    type t_signed_12_vector is array (integer range <>) of signed(11 downto 0);
    type t_signed_13_vector is array (integer range <>) of signed(12 downto 0);
    type t_signed_14_vector is array (integer range <>) of signed(13 downto 0);
    type t_signed_15_vector is array (integer range <>) of signed(14 downto 0);
    type t_signed_16_vector is array (integer range <>) of signed(15 downto 0);
    type t_signed_17_vector is array (integer range <>) of signed(16 downto 0);
    type t_signed_18_vector is array (integer range <>) of signed(17 downto 0);
    type t_signed_19_vector is array (integer range <>) of signed(18 downto 0);
    type t_signed_20_vector is array (integer range <>) of signed(19 downto 0);
    type t_signed_21_vector is array (integer range <>) of signed(20 downto 0);
    type t_signed_22_vector is array (integer range <>) of signed(21 downto 0);
    type t_signed_23_vector is array (integer range <>) of signed(22 downto 0);
    type t_signed_24_vector is array (integer range <>) of signed(23 downto 0);
    type t_signed_25_vector is array (integer range <>) of signed(24 downto 0);
    type t_signed_26_vector is array (integer range <>) of signed(25 downto 0);
    type t_signed_27_vector is array (integer range <>) of signed(26 downto 0);
    type t_signed_28_vector is array (integer range <>) of signed(27 downto 0);
    type t_signed_29_vector is array (integer range <>) of signed(28 downto 0);
    type t_signed_30_vector is array (integer range <>) of signed(29 downto 0);
    type t_signed_31_vector is array (integer range <>) of signed(30 downto 0);
    type t_signed_32_vector is array (integer range <>) of signed(31 downto 0);
    type t_signed_36_vector is array (integer range <>) of signed(35 downto 0);
    type t_signed_64_vector is array (integer range <>) of signed(63 downto 0);
    type t_signed_128_vector is array (integer range <>) of signed(127 downto 0);
    type t_signed_256_vector is array (integer range <>) of signed(255 downto 0);
    type t_signed_512_vector is array (integer range <>) of signed(511 downto 0);

    type t_unsigned_1_vector is array (integer range <>) of unsigned(0 downto 0);
    type t_unsigned_2_vector is array (integer range <>) of unsigned(1 downto 0);
    type t_unsigned_3_vector is array (integer range <>) of unsigned(2 downto 0);
    type t_unsigned_4_vector is array (integer range <>) of unsigned(3 downto 0);
    type t_unsigned_5_vector is array (integer range <>) of unsigned(4 downto 0);
    type t_unsigned_6_vector is array (integer range <>) of unsigned(5 downto 0);
    type t_unsigned_7_vector is array (integer range <>) of unsigned(6 downto 0);
    type t_unsigned_8_vector is array (integer range <>) of unsigned(7 downto 0);
    type t_unsigned_9_vector is array (integer range <>) of unsigned(8 downto 0);
    type t_unsigned_10_vector is array (integer range <>) of unsigned(9 downto 0);
    type t_unsigned_11_vector is array (integer range <>) of unsigned(10 downto 0);
    type t_unsigned_12_vector is array (integer range <>) of unsigned(11 downto 0);
    type t_unsigned_13_vector is array (integer range <>) of unsigned(12 downto 0);
    type t_unsigned_14_vector is array (integer range <>) of unsigned(13 downto 0);
    type t_unsigned_15_vector is array (integer range <>) of unsigned(14 downto 0);
    type t_unsigned_16_vector is array (integer range <>) of unsigned(15 downto 0);
    type t_unsigned_17_vector is array (integer range <>) of unsigned(16 downto 0);
    type t_unsigned_18_vector is array (integer range <>) of unsigned(17 downto 0);
    type t_unsigned_19_vector is array (integer range <>) of unsigned(18 downto 0);
    type t_unsigned_20_vector is array (integer range <>) of unsigned(19 downto 0);
    type t_unsigned_21_vector is array (integer range <>) of unsigned(20 downto 0);
    type t_unsigned_22_vector is array (integer range <>) of unsigned(21 downto 0);
    type t_unsigned_23_vector is array (integer range <>) of unsigned(22 downto 0);
    type t_unsigned_24_vector is array (integer range <>) of unsigned(23 downto 0);
    type t_unsigned_25_vector is array (integer range <>) of unsigned(24 downto 0);
    type t_unsigned_26_vector is array (integer range <>) of unsigned(25 downto 0);
    type t_unsigned_27_vector is array (integer range <>) of unsigned(26 downto 0);
    type t_unsigned_28_vector is array (integer range <>) of unsigned(27 downto 0);
    type t_unsigned_29_vector is array (integer range <>) of unsigned(28 downto 0);
    type t_unsigned_30_vector is array (integer range <>) of unsigned(29 downto 0);
    type t_unsigned_31_vector is array (integer range <>) of unsigned(30 downto 0);
    type t_unsigned_32_vector is array (integer range <>) of unsigned(31 downto 0);
    type t_unsigned_36_vector is array (integer range <>) of unsigned(35 downto 0);
    type t_unsigned_64_vector is array (integer range <>) of unsigned(63 downto 0);
    type t_unsigned_128_vector is array (integer range <>) of unsigned(127 downto 0);
    type t_unsigned_256_vector is array (integer range <>) of unsigned(255 downto 0);
    type t_unsigned_512_vector is array (integer range <>) of unsigned(511 downto 0);

    type t_integer_vector is array (integer range <>) of integer;

    type t_character_vector is array (integer range <>) of character;

    type t_boolean_vector is array (integer range <>) of boolean;

    type t_real_vector is array (integer range <>) of real;

-------------------------------------------------------------------------------
-- or-reduce functions on arrays of vectors.
-------------------------------------------------------------------------------
-- Desrcription: or_reduce is intended for busses where each unit drives the
--               with zero's unless it is addressed.  
-------------------------------------------------------------------------------
    function or_reduce (data_in : std_logic_vector) return std_logic;
    function or_reduce (data_in : t_std_logic_2_vector) return std_logic_vector;
    function or_reduce (data_in : t_std_logic_3_vector) return std_logic_vector;
    function or_reduce (data_in : t_std_logic_4_vector) return std_logic_vector;
    function or_reduce (data_in : t_std_logic_8_vector) return std_logic_vector;
    function or_reduce (data_in : t_std_logic_16_vector) return std_logic_vector;
    function or_reduce (data_in : t_std_logic_32_vector) return std_logic_vector;
    function or_reduce (data_in : t_std_logic_64_vector) return std_logic_vector;
    function or_reduce (data_in : t_std_logic_128_vector) return std_logic_vector;

    function or_reduce (data_in : t_unsigned_6_vector) return unsigned;

-------------------------------------------------------------------------------
-- xor-reduce functions
-------------------------------------------------------------------------------
    function xor_reduce(data_in: std_logic_vector) return std_logic;

-------------------------------------------------------------------------------
-- and-reduce functions
-------------------------------------------------------------------------------
    function and_reduce(data_in: std_logic_vector) return std_logic;

-------------------------------------------------------------------------------
-- std_logic_vector manipulation functions
-------------------------------------------------------------------------------
    ---------------------------------------------------------------------------
    -- lowest_bit
    ---------------------------------------------------------------------------
    -- Description: Returns the lowest bit position in a vector that is '1'
    ---------------------------------------------------------------------------
    function lowest_bit(arg : std_logic_vector) return integer;
    function lowest_bit(arg : signed) return integer;
    function lowest_bit(arg : unsigned) return integer;

    ---------------------------------------------------------------------------
    -- highest_bit
    ---------------------------------------------------------------------------
    -- Description: Returns the highest bit position in a vector that is '1'
    ---------------------------------------------------------------------------
    function highest_bit(arg : std_logic_vector) return integer;
    function highest_bit(arg : signed) return integer;
    function highest_bit(arg : unsigned) return integer;

    ---------------------------------------------------------------------------
    -- ones
    ---------------------------------------------------------------------------
    -- Description: Returns the number of '1' in a vector
    ---------------------------------------------------------------------------
    function ones(arg : std_logic_vector) return natural;
    function ones(arg : signed) return natural;
    function ones(arg : unsigned) return natural;

-------------------------------------------------------------------------------
-- conversion functions
-------------------------------------------------------------------------------
    ---------------------------------------------------------------------------
    -- onehot2int
    ---------------------------------------------------------------------------
    -- Description: This function returns the bit position of the one in a
    --              one-hot encoded signal
    ---------------------------------------------------------------------------
    function onehot2int(arg : std_logic_vector) return natural;
    function onehot2bin(arg : std_logic_vector; size : natural) return std_logic_vector;

    ---------------------------------------------------------------------------
    -- int2onehot
    ---------------------------------------------------------------------------
    -- Description: This function returns a onehot encoded std_logic_vector
    --              with the one at the position given as argument
    ---------------------------------------------------------------------------
    function int2onehot(arg : natural; size : natural) return std_logic_vector;
    function bin2onehot(arg : std_logic_vector; size : natural) return std_logic_vector;

-------------------------------------------------------------------------------
-- std_logic_vector range functions
-------------------------------------------------------------------------------

    ---------------------------------------------------------------------------
    -- reverse_order
    ---------------------------------------------------------------------------
    -- Description: Reverses the order of the vector. So 'downto' is converted
    --              to 'to' and vica versa.
    ---------------------------------------------------------------------------
    function reverse_order(v : std_logic_vector) return std_logic_vector;

    function reverse_bits(v: std_logic_vector) return std_logic_vector;
    function reverse_bits(v: unsigned) return unsigned;

    ---------------------------------------------------------------------------
    -- slice
    ---------------------------------------------------------------------------
    -- Description: Return a fraction of the given input vector, depending on
    --              requeste slice width and index. Note: in case the requested
    --              slice lies (partially) outside the input vector range, the 
    --              return data is padded with zeroes.
    ---------------------------------------------------------------------------
    function slice(data: std_logic_vector; size, index : integer) 
        return std_logic_vector;

end tl_vector_pkg;


package body tl_vector_pkg is

-------------------------------------------------------------------------------
-- std_logic_vector manipulation functions
-------------------------------------------------------------------------------
    function lowest_bit(arg : std_logic_vector) return integer is
        variable v_result : integer range arg'low to arg'high;
    begin
        v_result := arg'high;

        for i in arg'low to arg'high loop
            if arg(i) = '1' then
                v_result := i;
                exit;
            end if;
        end loop;

        return v_result;
    end;

    function lowest_bit(arg : signed) return integer is
    begin
        return lowest_bit(std_logic_vector(arg));
    end;

    function lowest_bit(arg : unsigned) return integer is
    begin
        return lowest_bit(std_logic_vector(arg));
    end;

    function highest_bit(arg : std_logic_vector) return integer is
        variable v_result : integer range arg'low to arg'high;
    begin
        v_result := arg'low;

        for i in arg'high downto arg'low loop
            if arg(i) = '1' then
                v_result := i;
                exit;
            end if;
        end loop;

        return v_result;
    end;

    function highest_bit(arg : signed) return integer is
    begin
        return highest_bit(std_logic_vector(arg));
    end;

    function highest_bit(arg : unsigned) return integer is
    begin
        return highest_bit(std_logic_vector(arg));
    end;

    function ones(arg : std_logic_vector) return natural is
        variable v_result : natural range 0 to arg'length;
    begin
        v_result := 0;

        for i in arg'range loop
            if arg(i) = '1' then
                v_result := v_result + 1;
            end if;
        end loop;

        return v_result;
    end function;

    function ones(arg : signed) return natural is
    begin
        return ones(std_logic_vector(arg));
    end function;

    function ones(arg : unsigned) return natural is
    begin
        return ones(std_logic_vector(arg));
    end function;


-------------------------------------------------------------------------------
-- conversion functions
-------------------------------------------------------------------------------
    function onehot2int(arg : std_logic_vector) return natural is
    begin
        return lowest_bit(arg);
    end function;

    function int2onehot(arg : natural; size : natural) return std_logic_vector is
        variable v_result : std_logic_vector(size - 1 downto 0);
    begin
        v_result := (others => '0');

        if arg < size then
            v_result(arg) := '1';
        end if;

        return v_result;
    end function;

    function onehot2bin(arg : std_logic_vector; size : natural) return std_logic_vector is
    begin
        return std_logic_vector(to_unsigned(onehot2int(arg), size));
    end function;

    function bin2onehot(arg : std_logic_vector; size : natural) return std_logic_vector is
    begin
        return int2onehot(to_integer(unsigned(arg)), size);
    end function;


-------------------------------------------------------------------------------
-- std_logic_vector range functions
-------------------------------------------------------------------------------
    function reverse_order(v : std_logic_vector) return std_logic_vector is
        variable result : std_logic_vector(v'reverse_range);
    begin
        for i in v'low to v'high loop
            result(i) := v(v'high - i + v'low);
        end loop;

        return result;
    end function;


    function reverse_bits(v: std_logic_vector) return std_logic_vector is
        variable result : std_logic_vector(v'range);
    begin
        for i in v'low to v'high loop
            result(i) := v(v'high - i + v'low);
        end loop;

        return result;
    end function;

    function reverse_bits(v: unsigned) return unsigned is
        variable result : unsigned(v'range);
    begin
        for i in v'low to v'high loop
            result(i) := v(v'high - i + v'low);
        end loop;

        return result;
    end function;

    function slice(data: std_logic_vector; size, index : integer) return std_logic_vector is
        variable v_result   : std_logic_vector(size - 1 downto 0);
        variable v_data_pad : std_logic_vector(((index + 1) * size) - 1 downto 0);
    begin
        v_data_pad := std_logic_vector(resize(unsigned(data), v_data_pad'length));
        v_result   := v_data_pad(((index + 1) * size) - 1 downto (index * size));
        return v_result;
    end function;


-------------------------------------------------------------------------------
-- or_reduce functions
-------------------------------------------------------------------------------
    function or_reduce (data_in : std_logic_vector) return std_logic is
        variable temp : std_logic := '0';
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_2_vector) return std_logic_vector is
        variable temp : std_logic_vector(1 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_3_vector) return std_logic_vector is
        variable temp : std_logic_vector(2 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_4_vector) return std_logic_vector is
        variable temp : std_logic_vector(3 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_8_vector) return std_logic_vector is
        variable temp : std_logic_vector(7 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_16_vector) return std_logic_vector is
        variable temp : std_logic_vector(15 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_32_vector) return std_logic_vector is
        variable temp : std_logic_vector(31 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_64_vector) return std_logic_vector is
        variable temp : std_logic_vector(63 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_std_logic_128_vector) return std_logic_vector is
        variable temp : std_logic_vector(127 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;

    function or_reduce (data_in : t_unsigned_6_vector) return unsigned is
        variable temp : unsigned(5 downto 0) := (others => '0');
    begin  -- or_reduce
        for i in data_in'range loop
            temp := temp or data_in(i);
        end loop;  -- i
        return temp;
    end or_reduce;
   
-------------------------------------------------------------------------------
-- xor_reduce functions
-------------------------------------------------------------------------------
    function xor_reduce(data_in : std_logic_vector) return std_logic is
        variable temp : std_logic := '0';
    begin
        for i in data_in'range loop
            temp := temp xor data_in(i);
        end loop;
        
        return temp;
    end function;

-------------------------------------------------------------------------------
-- and_reduce functions
-------------------------------------------------------------------------------
    function and_reduce(data_in : std_logic_vector) return std_logic is
        variable temp : std_logic := '1';
    begin
        for i in data_in'range loop
            temp := temp and data_in(i);
        end loop;
        
        return temp;
    end function;


    
end tl_vector_pkg;
