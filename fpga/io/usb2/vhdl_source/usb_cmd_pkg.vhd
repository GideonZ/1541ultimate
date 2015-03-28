--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2015
-- Entity: usb_cmd_pkg
-- Date:2015-01-18  
-- Author: Gideon     
-- Description: This package defines the commands that can be sent to the
--              sequencer. 
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.usb_pkg.all;

package usb_cmd_pkg is

    -- Protocol Event
    type t_usb_command is ( setup, out_data, in_request, ping );
    type t_usb_result  is ( res_data, res_ack, res_nak, res_nyet, res_stall, res_error );
     
    type t_usb_cmd_req is record
        request         : std_logic;
        
        -- command and modifiers (4 bits?)
        command         : t_usb_command;
        do_split        : std_logic;
        do_data         : std_logic;
        
        -- data buffer controls (14 bits)
        buffer_index    : unsigned(1 downto 0);
        data_length     : unsigned(9 downto 0);
        togglebit       : std_logic;
        no_data         : std_logic;

        -- USB addressing (11 bits)
        device_addr     : unsigned(6 downto 0);
        endp_addr       : unsigned(3 downto 0);

        -- USB addressing (15 bits)
        split_hub_addr  : unsigned(6 downto 0);
        split_port_addr : unsigned(3 downto 0); -- hubs with more than 16 ports are not supported
        split_sc        : std_logic; -- 0=start 1=complete
        split_sp        : std_logic; -- 0=full, 1=low
        split_et        : std_logic_vector(1 downto 0); -- 00=control, 01=iso, 10=bulk, 11=interrupt
    end record;

    type t_usb_cmd_resp is record
        done            : std_logic;
        result          : t_usb_result;

        -- data descriptor
        data_length     : unsigned(9 downto 0);
        togglebit       : std_logic;
        no_data         : std_logic;
    end record;        

--    type t_usb_result_encoded is array (t_usb_result range<>) of std_logic_vector(2 downto 0);
--    constant c_usb_result_encoded : t_usb_result_encoded := (
--        res_data => "001",
--        res_ack  => "010",
--        res_nak  => "011",
--        res_nyet => "100",
--        res_error => "111" );

    type t_usb_command_array is array(natural range <>) of t_usb_command;
    constant c_usb_commands_decoded : t_usb_command_array(0 to 3) := ( setup, out_data, in_request, ping );
    
    constant c_usb_cmd_init : t_usb_cmd_req := (
        request         => '0',
        command         => setup,
        do_split        => '0',
        do_data         => '0',
        buffer_index    => "00",
        data_length     => "0000000000",
        togglebit       => '0',
        no_data         => '1',
        device_addr     => "0000000",
        endp_addr       => X"0",
        split_hub_addr  => "0000000",
        split_port_addr => X"0",
        split_sc        => '0',
        split_sp        => '0',
        split_et        => "00" );
        
    function encode_result (pid : std_logic_vector(3 downto 0)) return t_usb_result;
    
end package;

package body usb_cmd_pkg is

    function encode_result (pid : std_logic_vector(3 downto 0)) return t_usb_result is
        variable res : t_usb_result;
    begin
        case pid is
            when c_pid_ack => res := res_ack;
            when c_pid_nak => res := res_nak;
            when c_pid_nyet => res := res_nyet;
            when c_pid_stall => res := res_stall;
            when others => res := res_error;
        end case;
        return res;    
    end function;

end package body;

