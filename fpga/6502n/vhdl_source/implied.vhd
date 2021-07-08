library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pkg_6502_decode.all;

entity implied is
port (
    inst            : in  std_logic_vector(7 downto 0);
    
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
    data_in         : in  std_logic_vector(7 downto 0);
    
    c_out           : out std_logic;
    i_out           : out std_logic;
    n_out           : out std_logic;
    z_out           : out std_logic;
    d_out           : out std_logic;
    v_out           : out std_logic;
    
    flags_imm       : out std_logic;

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
--
--                                                      0246024613571357
--                                                      8888AAAA8888AAAA
    constant c_flag     : std_logic_vector(0 to 15) := "0000000011000000";
    constant i_flag     : std_logic_vector(0 to 15) := "0000000000110000";

    constant set_a_low  : std_logic_vector(0 to 15) := "0001000000000000";
    
    signal selected_reg : std_logic_vector(7 downto 0) := X"00";
    signal operation    : integer range 0 to 15;
    signal reg_sel      : integer range 0 to 3;
    signal result       : std_logic_vector(7 downto 0) := X"00";
    signal add          : unsigned(7 downto 0) := X"00";
    signal carry        : unsigned(0 downto 0) := "0";
    signal zero         : std_logic := '0';
    signal do_nz        : std_logic := '0';

    signal v_hi         : std_logic;
    signal d_hi         : std_logic;
    
    signal c_lo         : std_logic;
    signal i_lo         : std_logic;
    
    signal enable       : std_logic;
    signal las, ane, shy : std_logic;
    signal inc_dec_copy_result  : std_logic_vector(7 downto 0);
begin
    enable    <= '1' when is_implied(inst) else '0';

    operation <= to_integer(unsigned(inst(4) & inst(1) & inst(6 downto 5)));

    -- Note, for BB, the operation will be: 1101 => 13, and corresponds to TSX
    -- 76543210
    -- 10111011

    -- Note, for 8B, the operation will be: 0100 => 4, and corresponds to TXA  (set_a is already set)
    -- 76543210
    -- 10001011
    -- Required operation: A = (A | $EF) & X & Imm

    -- 9B
    -- 76543210 
    -- 10011011
    --  1100  => 12 set S is set, which is good. (If only IS_IMPL was set for this instruction ;)
    -- 
    reg_sel   <= reg_sel_rom(operation);
    with reg_sel select selected_reg <=
        reg_a when 0,
        reg_x when 1,
        reg_y when 2,
        reg_s when others;
        
    add <= (others => decr_rom(operation));
    carry(0) <= incr_rom(operation);
    
    inc_dec_copy_result <= std_logic_vector(unsigned(selected_reg) + add + carry);

    -- This is a TWEAK.. should not happen like this
    las <= '1' when (inst = X"BB") else '0';
    ane <= '1' when (inst = X"8B") else '0';
    shy <= '1' when (inst = X"9B") else '0';
    
    result <=   (reg_a and reg_x) when shy = '1' else -- Tweakkkk
                (reg_a or X"EF") and reg_x and data_in when ane = '1' else -- Tweakkkk!
                (data_in and reg_s) when las = '1' else
                inc_dec_copy_result when inst(7)='1' else
                data_in;
    
    zero <= '1' when result = X"00" else '0';
    
    data_out <= result;
    
    do_nz <= ((nz_flags(operation) and inst(7)) or (set_a_low(operation) and not inst(7)));

    v_hi <= '0'       when v_flag(operation)='1'   else v_in;
    d_hi <= inst(5)   when d_flag(operation)='1'   else d_in;
    -- in high, C and I are never set

    c_lo <= inst(5)   when c_flag(operation)='1'   else c_in;
    i_lo <= inst(5)   when i_flag(operation)='1'   else i_in;
    -- in low, V and D are never set
    
    set_a <= las or ane or (enable and ((set_a_rom(operation) and inst(7)) or (set_a_low(operation) and not inst(7))));
    set_x <= las or (enable and set_x_rom(operation) and inst(7));
    set_y <= enable and set_y_rom(operation) and inst(7);
    set_s <= las or shy or (enable and set_s_rom(operation) and inst(7));

    c_out <= c_in when inst(7)='1' else c_lo; -- C can only be set in lo
    v_out <= v_hi when inst(7)='1' else v_in; -- V can only be set in hi

    n_out <= result(7) when do_nz='1' else n_in; 
    z_out <= zero      when do_nz='1' else z_in;

    d_out <= d_hi when inst(7)='1' and enable='1' else d_in; -- D can only be set in hi
    i_out <= i_lo when inst(7)='0' and enable='1' else i_in; -- I can only be set in lo

    process(inst)
    begin
        case inst is
        when X"18" | X"38" | X"58" | X"78" | X"B8" | X"D8" | X"F8" =>
            flags_imm <= '1';
        when others =>
            flags_imm <= '0';
        end case;
    end process;

end gideon;
