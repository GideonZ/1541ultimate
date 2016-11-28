
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package ulpi_pkg is
    constant c_ulpireg_func_ctrl    : std_logic_vector(5 downto 0) := "000100";
    constant c_ulpireg_otg_ctrl     : std_logic_vector(5 downto 0) := "001010";


    constant func_ctrl_xcvr_hs          : std_logic_vector(7 downto 0) := "01000000";
    constant func_ctrl_xcvr_fs          : std_logic_vector(7 downto 0) := "01000001";
    constant func_ctrl_xcvr_ls          : std_logic_vector(7 downto 0) := "01000010";
    constant func_ctrl_xcvr_preamb      : std_logic_vector(7 downto 0) := "01000011";

    constant func_ctrl_term_select_0    : std_logic_vector(7 downto 0) := "01000000";
    constant func_ctrl_term_select_1    : std_logic_vector(7 downto 0) := "01000100";

    constant func_ctrl_opmode_normal    : std_logic_vector(7 downto 0) := "01000000";
    constant func_ctrl_opmode_no_drv    : std_logic_vector(7 downto 0) := "01001000";
    constant func_ctrl_opmode_no_nrzi   : std_logic_vector(7 downto 0) := "01010000";
    
    constant func_ctrl_reset            : std_logic_vector(7 downto 0) := "01100000";
    constant func_ctrl_suspend_not      : std_logic_vector(7 downto 0) := "00000000";

    constant otg_ctrl_id_pullup         : std_logic_vector(7 downto 0) := "00000001";
    constant otg_ctrl_dp_pulldown       : std_logic_vector(7 downto 0) := "00000010";
    constant otg_ctrl_dm_pulldown       : std_logic_vector(7 downto 0) := "00000100";
    constant otg_ctrl_discharge_vbus    : std_logic_vector(7 downto 0) := "00001000";
    constant otg_ctrl_charge_vbus       : std_logic_vector(7 downto 0) := "00010000";
    constant otg_ctrl_drive_vbus        : std_logic_vector(7 downto 0) := "00100000";
    constant otg_ctrl_drive_vbus_ext    : std_logic_vector(7 downto 0) := "01000000";
    constant otg_ctrl_use_ext_vbus      : std_logic_vector(7 downto 0) := "10000000";

    function map_speed(i : std_logic_vector(1 downto 0)) return std_logic_vector;

end package;
    
package body ulpi_pkg is

    function map_speed(i : std_logic_vector(1 downto 0)) return std_logic_vector is
    begin
        case i is
        when "00" =>
            return func_ctrl_xcvr_ls or func_ctrl_term_select_1; -- LS mode
        when "01" =>
            return func_ctrl_xcvr_fs or func_ctrl_term_select_1; -- FS mode
        when "10" =>
            return func_ctrl_xcvr_hs or func_ctrl_term_select_0; -- HS mode
        when others =>
            return func_ctrl_xcvr_hs or func_ctrl_term_select_0 or func_ctrl_opmode_no_nrzi; -- stay in chirp mode
        end case;
        return X"00";
    end function;

end package body;

    