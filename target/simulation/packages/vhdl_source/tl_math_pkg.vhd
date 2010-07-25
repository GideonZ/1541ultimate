-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Miscelaneous mathematic operations
-------------------------------------------------------------------------------
-- Description: This file contains math functions
-------------------------------------------------------------------------------

library ieee;
	use ieee.std_logic_1164.all;
	use ieee.numeric_std.all;

package tl_math_pkg is
    ---------------------------------------------------------------------------
    -- increment/decrement functions
    ---------------------------------------------------------------------------

    ---------------------------------------------------------------------------
    -- wrapping decrement/increment
    ---------------------------------------------------------------------------
    -- Description: These functions give an increment/decrement that wraps when
    --              the maximal vector range is reached. 
    ---------------------------------------------------------------------------
    function incr(old_value: std_logic_vector; increment: natural := 1) return std_logic_vector;
    function incr(old_value: unsigned; increment: natural := 1) return unsigned;
    function incr(old_value: signed; increment: natural := 1) return signed;
    
    function decr(old_value: std_logic_vector; decrement: natural := 1) return std_logic_vector;
    function decr(old_value: unsigned; decrement: natural := 1) return unsigned;
    function decr(old_value: signed; decrement: natural := 1) return signed;

    ---------------------------------------------------------------------------
    -- clipping decrement/increment
    ---------------------------------------------------------------------------
    -- Description: These functions give an increment/decrement that clips when
    --              the maximal vector range is reached.
    ---------------------------------------------------------------------------
    function incr_clip(old_value: std_logic_vector; increment: natural := 1) return std_logic_vector;
    function incr_clip(old_value: unsigned; increment: natural := 1) return unsigned;
    function incr_clip(old_value: signed; increment: natural := 1) return signed;

    function decr_clip(old_value: std_logic_vector; decrement: natural := 1) return std_logic_vector;
    function decr_clip(old_value: unsigned; decrement: natural := 1) return unsigned;
    function decr_clip(old_value: signed; decrement: natural := 1) return signed;


    ---------------------------------------------------------------------------
    -- log functions
    ---------------------------------------------------------------------------

    ---------------------------------------------------------------------------
	-- log2
    ---------------------------------------------------------------------------
    -- Description: This functions returns the log2 value of a number. The
    --              result is in the natural range and rounded depended on the
    --              selected mode.
    -- NOTE:        use an argument of type unsigned when using this function for
    --              synthesis
    ---------------------------------------------------------------------------
    type log2mode is (ceil, floor);

    function log2(arg: integer) return natural;
    function log2(arg: integer; mode: log2mode) return natural;
    function log2_floor(arg: integer) return natural;
    function log2_ceil(arg: integer) return natural;

    function log2(arg: unsigned) return natural;
    function log2(arg: unsigned; mode: log2mode) return natural;
    function log2_floor(arg: unsigned) return natural;
    function log2_ceil(arg: unsigned) return natural;

    ---------------------------------------------------------------------------
	-- min/max
    ---------------------------------------------------------------------------
    -- Description: These functions return the minimum/maximum of two values
    ---------------------------------------------------------------------------
    function max(a, b: integer) return integer;
    function max(a, b: unsigned) return unsigned;

    function min(a, b: integer) return integer;
    function min(a, b: unsigned) return unsigned;

end tl_math_pkg;

library work;
    use work.tl_vector_pkg.all;

package body tl_math_pkg is
    ---------------------------------------------------------------------------
    -- increment/decrement functions
    ---------------------------------------------------------------------------
    function incr(old_value: std_logic_vector; increment: natural := 1) return std_logic_vector is
    begin
        return std_logic_vector(incr(unsigned(old_value), increment));
    end function;
    
    function incr(old_value: unsigned; increment: natural := 1) return unsigned is
        variable v_result    : unsigned(old_value'range);
    begin
        v_result := (old_value + increment) mod 2**old_value'length;
        return v_result;
    end function;
    
    function incr(old_value: signed; increment: natural := 1) return signed is
    begin
        return signed(incr(unsigned(old_value), increment));
    end function;

    function decr(old_value: std_logic_vector; decrement: natural := 1) return std_logic_vector is
    begin
        return std_logic_vector(decr(unsigned(old_value), decrement));
    end function;
    
    function decr(old_value: unsigned; decrement: natural := 1) return unsigned is
        constant c_norm_decrement   : integer := decrement mod 2**old_value'length;
        variable v_result           : unsigned(old_value'range);
    begin
        v_result := (2**old_value'length + (old_value - c_norm_decrement)) mod 2**old_value'length;
        return v_result;
    end function;
    
    function decr(old_value: signed; decrement: natural := 1) return signed is
    begin
        return signed(decr(unsigned(old_value), decrement));
    end function;

    ---------------------------------------------------------------------------
    -- clipping decrement/increment
    ---------------------------------------------------------------------------
    -- Description: These functions give an increment/decrement that clips when
    --              the maximal vector range is reached.
    ---------------------------------------------------------------------------
    function incr_clip(old_value: std_logic_vector; increment: natural := 1) return std_logic_vector is
    begin
        return std_logic_vector(incr_clip(unsigned(old_value), increment));
    end function;
    
    function incr_clip(old_value: unsigned; increment: natural := 1) return unsigned is
        constant c_max_value    : unsigned(old_value'range) := (others => '1');
        variable v_result       : unsigned(old_value'range);
    begin
        assert increment < 2**old_value'length report "ERROR: Increment value is larger than vector range" severity error;

        if old_value <= (c_max_value - increment) then
            v_result := old_value + increment;
        else
            v_result := c_max_value;
        end if;

        return v_result;
    end function;
    
    function incr_clip(old_value: signed; increment: natural := 1) return signed is
        variable c_max_value    : signed(old_value'range) := to_signed(2**old_value'length / 2 - 1, old_value'length);
        variable v_result       : signed(old_value'range);
    begin
        if old_value <= (c_max_value - increment) then
            v_result := old_value + increment;
        else
            v_result := c_max_value;
        end if;

        return v_result;
    end function;
    
    function decr_clip(old_value: std_logic_vector; decrement: natural := 1) return std_logic_vector is
    begin
        return std_logic_vector(decr_clip(unsigned(old_value), decrement));
    end function;
    
    function decr_clip(old_value: unsigned; decrement: natural := 1) return unsigned is
        constant c_min_value    : unsigned(old_value'range) := (others => '0');
        variable v_result       : unsigned(old_value'range);
    begin
        if old_value >= (c_min_value + decrement) then
            v_result := old_value - decrement;
        else
            v_result := c_min_value;
        end if;

        return v_result;
    end function;
    
    function decr_clip(old_value: signed; decrement: natural := 1) return signed is
        constant c_min_value    : signed(old_value'range) := to_signed(2**old_value'length / 2, old_value'length);
        variable v_result       : signed(old_value'range);
    begin
        if old_value >= (c_min_value + decrement) then
            v_result := old_value - decrement;
        else
            v_result := c_min_value;
        end if;

        return v_result;
    end function;

    ---------------------------------------------------------------------------
    -- log functions
    ---------------------------------------------------------------------------
    function log2(arg: integer) return natural is
    begin
        return log2_ceil(arg);
    end function;

    function log2(arg: integer; mode: log2mode) return natural is
    begin
        if mode = floor then
            return log2_floor(arg);
        else
            return log2_ceil(arg);
        end if;
    end;

    function log2_ceil(arg: integer) return natural is
        variable v_temp   : integer;
        variable v_result : natural;
    begin
        v_result := log2_floor(arg);

        if 2**v_result < arg then
            return v_result + 1;
        else
            return v_result;
        end if;
    end function;

    function log2_floor(arg: integer) return natural is
        variable v_temp   : integer;
        variable v_result : natural;
    begin
        v_result    := 0;
        v_temp      := arg / 2;
        while v_temp /= 0 loop
            v_temp      := v_temp / 2;
            v_result    := v_result + 1;
        end loop;
        return v_result;
    end function;


    function log2(arg: unsigned) return natural is
    begin
        return log2_ceil(arg);
    end function;

    function log2(arg: unsigned; mode: log2mode) return natural is
    begin
        if mode = ceil then
            return log2_ceil(arg);
        else
            return log2_floor(arg);
        end if;
    end function;

    function log2_floor(arg: unsigned) return natural is
        alias w : unsigned(arg'length - 1 downto 0) is arg;
    begin
        return highest_bit(w);
    end function;

    function log2_ceil(arg: unsigned) return natural is
        alias w : unsigned(arg'length - 1 downto 0) is arg;
    begin
       
        if ones(arg) > 1 then
            return highest_bit(w) + 1;
        else
            return highest_bit(w);
        end if;
    end function;

    ---------------------------------------------------------------------------
	-- min/max
    ---------------------------------------------------------------------------
    -- Description: These functions return the minimum/maximum of two values
    ---------------------------------------------------------------------------
    function max(a, b: integer) return integer is
    begin
        if a > b then
            return a;
        else
            return b;
        end if;
    end function;

    function max(a, b: unsigned) return unsigned is
    begin
        if a > b then
            return a;
        else
            return b;
        end if;
    end function;

    function min(a, b: integer) return integer is
    begin
        if a < b then
            return a;
        else
            return b;
        end if;
    end function;

    function min(a, b: unsigned) return unsigned is
    begin
        if a < b then
            return a;
        else
            return b;
        end if;
    end function;


end tl_math_pkg;
