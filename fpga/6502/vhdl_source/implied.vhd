library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity implied is
port (
    inst            : in  std_logic_vector(7 downto 0);
    enable          : in  std_logic;
    
    c_in            : in  std_logic;
    i_in            : in  std_logic;
    n_in            : in  std_logic;
    z_in            : in  std_logic;
    d_in            : in  std_logic;
    v_in            : in  std_logic;
    
    reg_a           : in  std_logic_vector(7 downto 0);
    reg_x           : in  std_logic_vector(7 downto 0);
    reg_y           : in  std_logic_vector(7 downto 0);
    reg_s           : in  std_logic_vector(7 downto 0);
    shift_data      : in  std_logic_vector(7 downto 0);
    
    c_out           : out std_logic;
    i_out           : out std_logic;
    n_out           : out std_logic;
    z_out           : out std_logic;
    d_out           : out std_logic;
    v_out           : out std_logic;
    
    set_a           : out std_logic;
    set_x           : out std_logic;
    set_y           : out std_logic;
    set_s           : out std_logic;
    
    data_out        : out std_logic_vector(7 downto 0));

end implied;

architecture gideon of implied is
    type t_int4_array is array(natural range <>) of integer range 0 to 4;
    -- ROMS for the upper (negative) implied instructions
    constant reg_sel_rom : t_int4_array(0 to 15) := ( 2,0,2,1,1,0,1,1,2,0,2,1,1,3,1,1 );  -- 0=A, 1=X, 2=Y, 3=S

--                                                      DTIITTDNTCCSTTNN
--                                                      EANNXAEOYLLEXSOO
--                                                      YYYXAXXPAVDDSXPP
--
--                                                      8ACE8ACE9BDF9BDF
--                                                      8888AAAA8888AAAA
--
--                                                      YAYXXAXXYAYXXSXX
    constant decr_rom   : std_logic_vector(0 to 15) := "1000001000000000";
    constant incr_rom   : std_logic_vector(0 to 15) := "0011000000000000";
    constant nz_flags   : std_logic_vector(0 to 15) := "1111111010000100";
    constant v_flag     : std_logic_vector(0 to 15) := "0000000001000000";
    constant d_flag     : std_logic_vector(0 to 15) := "0000000000110000";
    constant set_a_rom  : std_logic_vector(0 to 15) := "0000100010000000";
    constant set_x_rom  : std_logic_vector(0 to 15) := "0001011000000100";
    constant set_y_rom  : std_logic_vector(0 to 15) := "1110000000000000";
    constant set_s_rom  : std_logic_vector(0 to 15) := "0000000000001000";

    -- ROMS for the lower (positive) implied instructions
--                                                      PPPPARLRCSCSNNNN
--                                                      HLHLSOSOLELEOOOO
--                                                      PPAALLRRCCIIPPPP
--                                                      0246024613571357
--                                                      8888AAAA8888AAAA
    constant c_flag     : std_logic_vector(0 to 15) := "0000000011000000";
    constant i_flag     : std_logic_vector(0 to 15) := "0000000000110000";
    constant set_a_low  : std_logic_vector(0 to 15) := "0001111100000000";
    
    signal selected_reg : std_logic_vector(7 downto 0) := X"00";
    signal operation    : integer range 0 to 15;
    signal reg_sel      : integer range 0 to 3;
    signal result       : std_logic_vector(7 downto 0) := X"00";
    signal add          : std_logic_vector(7 downto 0) := X"00";
    signal carry        : std_logic := '0';
    signal zero         : std_logic := '0';
    signal do_nz        : std_logic := '0';

    signal n_hi         : std_logic;
    signal z_hi         : std_logic;
    signal v_hi         : std_logic;
    signal d_hi         : std_logic;
    
    signal n_lo         : std_logic;
    signal z_lo         : std_logic;
    signal c_lo         : std_logic;
    signal i_lo         : std_logic;
begin
    operation <= conv_integer(inst(4) & inst(1) & inst(6 downto 5));
    reg_sel   <= reg_sel_rom(operation);
    with reg_sel select selected_reg <=
        reg_a when 0,
        reg_x when 1,
        reg_y when 2,
        reg_s when others;
        
    add <= (others => decr_rom(operation));
    carry <= incr_rom(operation);
    
    result <= (selected_reg + add + carry) when inst(7)='1' else shift_data;
    
    zero <= '1' when result = X"00" else '0';
    
    data_out <= result;
    
    do_nz <= enable and ((nz_flags(operation) and inst(7)) or (set_a_low(operation) and not inst(7)));

    v_hi <= '0'       when enable='1' and v_flag(operation)='1'   else v_in;
    d_hi <= inst(5)   when enable='1' and d_flag(operation)='1'   else d_in;
    -- in high, C and I are never set

    c_lo <= inst(5)   when enable='1' and c_flag(operation)='1'   else c_in;
    i_lo <= inst(5)   when enable='1' and i_flag(operation)='1'   else i_in;
    -- in low, V and D are never set
    
    set_a <= enable and ((set_a_rom(operation) and inst(7)) or (set_a_low(operation) and not inst(7)));
    set_x <= enable and set_x_rom(operation) and inst(7);
    set_y <= enable and set_y_rom(operation) and inst(7);
    set_s <= enable and set_s_rom(operation) and inst(7);

    c_out <= c_in when inst(7)='1' else c_lo; -- C can only be set in lo
    i_out <= i_in when inst(7)='1' else i_lo; -- I can only be set in lo
    v_out <= v_hi when inst(7)='1' else v_in; -- V can only be set in hi
    d_out <= d_hi when inst(7)='1' else d_in; -- D can only be set in hi

    n_out <= result(7) when do_nz='1' else n_in; 
    z_out <= zero      when do_nz='1' else z_in; -- Z can only be set in hi
    
end gideon;
