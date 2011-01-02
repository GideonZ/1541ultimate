library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
package command_if_pkg is

    constant c_cif_io_slot_base         : unsigned(3 downto 0) := X"0"; 
    constant c_cif_io_slot_enable       : unsigned(3 downto 0) := X"1";
    constant c_cif_io_handshake_out     : unsigned(3 downto 0) := X"2"; -- write will also cause pointers to be reset
    constant c_cif_io_handshake_in      : unsigned(3 downto 0) := X"3";
    constant c_cif_io_command_start     : unsigned(3 downto 0) := X"4"; -- read only; tells software where the buffers are.
    constant c_cif_io_command_end       : unsigned(3 downto 0) := X"5";
    constant c_cif_io_response_start    : unsigned(3 downto 0) := X"6"; -- read only; tells software where the buffers are.
    constant c_cif_io_response_end      : unsigned(3 downto 0) := X"7";
    constant c_cif_io_status_start      : unsigned(3 downto 0) := X"8"; -- read only; tells software where the buffers are.
    constant c_cif_io_status_end        : unsigned(3 downto 0) := X"9";
    constant c_cif_io_status_length     : unsigned(3 downto 0) := X"A"; -- write will reset status readout
    constant c_cif_io_response_len_l    : unsigned(3 downto 0) := X"C"; -- write will reset response readout
    constant c_cif_io_response_len_h    : unsigned(3 downto 0) := X"D";
    constant c_cif_io_command_len_l     : unsigned(3 downto 0) := X"E"; -- read only
    constant c_cif_io_command_len_h     : unsigned(3 downto 0) := X"F"; 

    constant c_cif_slot_control         : unsigned(1 downto 0) := "00"; -- R/W
    constant c_cif_slot_command         : unsigned(1 downto 0) := "01"; -- WO
    constant c_cif_slot_response        : unsigned(1 downto 0) := "10"; -- RO
    constant c_cif_slot_status          : unsigned(1 downto 0) := "11"; -- RO

    constant c_cmd_if_command_buffer_addr  : unsigned(10 downto 0) := to_unsigned(   0, 11);
    constant c_cmd_if_response_buffer_addr : unsigned(10 downto 0) := to_unsigned( 896, 11);
    constant c_cmd_if_status_buffer_addr   : unsigned(10 downto 0) := to_unsigned(1792, 11);
    constant c_cmd_if_command_buffer_size  : integer := 896;
    constant c_cmd_if_response_buffer_size : integer := 896;
    constant c_cmd_if_status_buffer_size   : integer := 256;
    constant c_cmd_if_command_buffer_end   : unsigned(10 downto 0) := to_unsigned(   0 + c_cmd_if_command_buffer_size-1, 11);
    constant c_cmd_if_response_buffer_end  : unsigned(10 downto 0) := to_unsigned( 896 + c_cmd_if_response_buffer_size-1, 11);
    constant c_cmd_if_status_buffer_end    : unsigned(10 downto 0) := to_unsigned(1792 + c_cmd_if_status_buffer_size-1, 11);

end package;
