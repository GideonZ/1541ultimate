
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu is
generic (
    support_bcd : boolean := true );
port (
    operation       : in  std_logic_vector(2 downto 0);

    n_in            : in  std_logic;
    v_in            : in  std_logic;
    z_in            : in  std_logic;
    c_in            : in  std_logic;
    d_in            : in  std_logic;
    
    data_a          : in  std_logic_vector(7 downto 0);
    data_b          : in  std_logic_vector(7 downto 0);
    
    n_out           : out std_logic;
    v_out           : out std_logic;
    z_out           : out std_logic;
    c_out           : out std_logic;
    
    half_carry      : out std_logic;
    full_carry      : out std_logic;
    data_out        : out std_logic_vector(7 downto 0));

end alu;

architecture gideon of alu is
    signal data_out_i   : std_logic_vector(7 downto 0) := X"FF";
    signal zero         : std_logic;
    signal sum_c        : std_logic;
    signal sum_n        : std_logic;
    signal sum_z        : std_logic;
    signal sum_v        : std_logic;
    signal sum_result   : unsigned(7 downto 0) := X"FF";    

    constant c_ora  : std_logic_vector(2 downto 0) := "000";
    constant c_and  : std_logic_vector(2 downto 0) := "001";
    constant c_eor  : std_logic_vector(2 downto 0) := "010";
    constant c_adc  : std_logic_vector(2 downto 0) := "011";
    constant c_lda  : std_logic_vector(2 downto 0) := "101";
    constant c_cmp  : std_logic_vector(2 downto 0) := "110";
    constant c_sbc  : std_logic_vector(2 downto 0) := "111";
begin

-- ORA $nn    AND $nn    EOR $nn    ADC $nn    STA $nn    LDA $nn    CMP $nn    SBC $nn

    sum: process(data_a, data_b, c_in, operation, d_in)
        variable a     : unsigned(7 downto 0);
        variable b     : unsigned(7 downto 0);
        variable c     : unsigned(0 downto 0);
        variable hc    : unsigned(0 downto 0);
        variable fc    : unsigned(0 downto 0);
        variable sum_l : unsigned(4 downto 0);
        variable sum_h : unsigned(4 downto 0);
    begin
        a := unsigned(data_a);
        -- for subtraction invert second operand
        if operation(2)='1' then -- invert b
            b := unsigned(not data_b);
        else
            b := unsigned(data_b);
        end if;    

        -- carry in is masked to '1' for CMP
        c(0) := c_in or not operation(0);
        
        -- First add the lower nibble
        sum_l := ('0' & a(3 downto 0)) + ('0' & b(3 downto 0)) + c;

        -- Determine HalfCarry for ADC only
        if support_bcd and d_in='1' and operation(0)='1' and operation(2) = '0' then
            if sum_l(4) = '1' or sum_l(3 downto 2)="11" or sum_l(3 downto 1)="101" then -- >9 (10-11, 12-15)
                hc := "1";
            else
                hc := "0";
            end if;
        else
            hc(0) := sum_l(4); -- Standard carry
        end if;
        
        half_carry <= hc(0);
        
        -- Then, determine the upper nipple
        sum_h := ('0' & a(7 downto 4)) + ('0' & b(7 downto 4)) + hc;

        -- Again, determine the carry of the upper nibble
        if support_bcd and d_in='1' and operation(0)='1' and operation(2) = '0' then
            if sum_h(4) = '1' or sum_h(3 downto 2)="11" or sum_h(3 downto 1)="101" then -- >9 (10-11, 12-15)
                fc := "1";
            else
                fc := "0";
            end if;
        else
            fc(0) := sum_h(4);
        end if;
        
        full_carry <= fc(0);
        
        -- Determine Z flag
        if sum_l(3 downto 0)="0000" and sum_h(3 downto 0)="0000" then
            sum_z <= '1';
        else
            sum_z <= '0';
        end if;

        sum_n  <= sum_h(3);
        sum_c  <= fc(0);
        sum_v  <= (sum_h(3) xor data_a(7)) and (sum_h(3) xor data_b(7) xor operation(2)); 

        sum_result <= sum_h(3 downto 0) & sum_l(3 downto 0);
    end process;

    with operation select data_out_i <= 
        data_a or  data_b            when c_ora,
        data_a and data_b            when c_and,
        data_a xor data_b            when c_eor,
        std_logic_vector(sum_result) when c_adc | c_cmp | c_sbc,
        data_b                       when others;

    zero <= '1' when data_out_i = X"00" else '0';
    
    with operation select c_out <=
        sum_c       when c_adc | c_sbc | c_cmp,
        c_in        when others;

    with operation select z_out <=
        sum_z       when c_adc | c_sbc | c_cmp,
        zero        when c_ora | c_and | c_eor | c_lda,
        z_in        when others;

    with operation select n_out <=
        sum_n         when c_adc | c_sbc | c_cmp,
        data_out_i(7) when c_ora | c_and | c_eor | c_lda,
        n_in          when others;
    
    with operation select v_out <=
        sum_v         when c_adc | c_sbc,
        v_in          when others;

    data_out <= data_out_i;
end gideon;
