
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity shifter is
port (
    operation       : in  std_logic_vector(2 downto 0);
    bypass          : in  std_logic := '0';    
    c_in            : in  std_logic;
    n_in            : in  std_logic;
    z_in            : in  std_logic;
    
    data_in         : in  std_logic_vector(7 downto 0);
    
    c_out           : out std_logic;
    n_out           : out std_logic;
    z_out           : out std_logic;
    
    data_out        : out std_logic_vector(7 downto 0) := X"00");

end shifter;

architecture gideon of shifter is
    signal data_out_i   : std_logic_vector(7 downto 0) := X"00";
    signal zero         : std_logic := '0';
    constant c_asl  : std_logic_vector(2 downto 0) := "000";
    constant c_rol  : std_logic_vector(2 downto 0) := "001";
    constant c_lsr  : std_logic_vector(2 downto 0) := "010";
    constant c_ror  : std_logic_vector(2 downto 0) := "011";
    constant c_sta  : std_logic_vector(2 downto 0) := "100";
    constant c_lda  : std_logic_vector(2 downto 0) := "101";
    constant c_dec  : std_logic_vector(2 downto 0) := "110";
    constant c_inc  : std_logic_vector(2 downto 0) := "111";
begin
-- ASL $nn	ROL $nn	LSR $nn	ROR $nn	STX $nn	LDX $nn	DEC $nn	INC $nn

    with operation select data_out_i <= 
        data_in(6 downto 0) & '0'   when c_asl,
        data_in(6 downto 0) & c_in  when c_rol,
        '0' & data_in(7 downto 1)   when c_lsr,
        c_in & data_in(7 downto 1)  when c_ror,
        std_logic_vector(unsigned(data_in) - 1) when c_dec,
        std_logic_vector(unsigned(data_in) + 1) when c_inc,
        data_in                     when others;
        
    zero <= '1' when data_out_i = X"00" else '0';
    
    with operation select c_out <=
        data_in(7)  when c_asl | c_rol,
        data_in(0)  when c_lsr | c_ror,
        c_in        when others;

    with operation select z_out <=
        zero        when c_asl | c_rol | c_lsr | c_ror | c_lda | c_dec | c_inc,
        z_in        when others;

    with operation select n_out <=
        data_out_i(7) when c_asl | c_rol | c_lsr | c_ror | c_lda | c_dec | c_inc,
        n_in          when others;
    
    data_out <= data_out_i when bypass = '0' else data_in;

end gideon;
