
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package usb_pkg is

    type t_transaction_type  is ( control, bulk, interrupt, isochronous );
    type t_transaction_state is ( none, busy, done, error );
    type t_pipe_state        is ( invalid, initialized, stalled, aborted );
    type t_direction         is ( dir_in, dir_out );
    type t_transfer_mode     is ( direct, use_preamble, use_split );
    
    type t_pipe is record
        state               : t_pipe_state;
        direction           : t_direction;
        device_address      : std_logic_vector(6 downto 0);
        device_endpoint     : std_logic_vector(3 downto 0);
        max_transfer        : unsigned(10 downto 0); -- could be encoded in less (3) bits (only 2^x)
        data_toggle         : std_logic;
        control             : std_logic;   -- '1' if this pipe is treated as a control pipe
        timeout             : std_logic;
        --transfer_mode       : t_transfer_mode;        
    end record; -- 18 bits now with encoded max transfer, otherwise 26
    
    type t_transaction is record
        transaction_type    : t_transaction_type;
        state               : t_transaction_state;
        pipe_pointer        : unsigned(4 downto 0); -- 32 pipes enough?
        transfer_length     : unsigned(10 downto 0); -- max 2K
        buffer_address      : unsigned(10 downto 0); -- 2K buffer
        link_to_next		: std_logic;   -- when '1', no other events will take place (such as sof)
    end record; -- 32 bits now
    
    function data_to_t_pipe(i: std_logic_vector(31 downto 0)) return t_pipe;
    function t_pipe_to_data(i: t_pipe) return std_logic_vector;

    function data_to_t_transaction(i: std_logic_vector(31 downto 0)) return t_transaction;
    function t_transaction_to_data(i: t_transaction) return std_logic_vector;

    constant c_pid_out          : std_logic_vector(3 downto 0) := X"1"; -- token
    constant c_pid_in           : std_logic_vector(3 downto 0) := X"9"; -- token
    constant c_pid_sof          : std_logic_vector(3 downto 0) := X"5"; -- token
    constant c_pid_setup        : std_logic_vector(3 downto 0) := X"D"; -- token

    constant c_pid_data0        : std_logic_vector(3 downto 0) := X"3"; -- data
    constant c_pid_data1        : std_logic_vector(3 downto 0) := X"B"; -- data
    constant c_pid_data2        : std_logic_vector(3 downto 0) := X"7"; -- data
    constant c_pid_mdata        : std_logic_vector(3 downto 0) := X"F"; -- data

    constant c_pid_ack          : std_logic_vector(3 downto 0) := X"2"; -- handshake
    constant c_pid_nak          : std_logic_vector(3 downto 0) := X"A"; -- handshake
    constant c_pid_stall        : std_logic_vector(3 downto 0) := X"E"; -- handshake
    constant c_pid_nyet         : std_logic_vector(3 downto 0) := X"6"; -- handshake

    constant c_pid_pre          : std_logic_vector(3 downto 0) := X"C"; -- token
    constant c_pid_err          : std_logic_vector(3 downto 0) := X"C"; -- handshake
    constant c_pid_split        : std_logic_vector(3 downto 0) := X"8"; -- token
    constant c_pid_ping         : std_logic_vector(3 downto 0) := X"4"; -- token
    constant c_pid_reserved     : std_logic_vector(3 downto 0) := X"0";

    function is_token(i : std_logic_vector(3 downto 0)) return boolean;
    function is_handshake(i : std_logic_vector(3 downto 0)) return boolean;

    constant c_cmd_get_status   : std_logic_vector(3 downto 0) := X"1";
    constant c_cmd_get_speed    : std_logic_vector(3 downto 0) := X"2";
    constant c_cmd_get_done     : std_logic_vector(3 downto 0) := X"3";
    constant c_cmd_do_reset_hs  : std_logic_vector(3 downto 0) := X"4";
    constant c_cmd_do_reset_fs  : std_logic_vector(3 downto 0) := X"5";
    constant c_cmd_disable_host : std_logic_vector(3 downto 0) := X"6";
    constant c_cmd_abort        : std_logic_vector(3 downto 0) := X"7";
    constant c_cmd_sof_enable   : std_logic_vector(3 downto 0) := X"8";
    constant c_cmd_sof_disable  : std_logic_vector(3 downto 0) := X"9";
    constant c_cmd_set_gap      : std_logic_vector(3 downto 0) := X"A";
    constant c_cmd_set_busy     : std_logic_vector(3 downto 0) := X"B";
    constant c_cmd_clear_busy   : std_logic_vector(3 downto 0) := X"C";
    constant c_cmd_set_debug    : std_logic_vector(3 downto 0) := X"D";
    constant c_cmd_disable_scan : std_logic_vector(3 downto 0) := X"E";
    constant c_cmd_enable_scan  : std_logic_vector(3 downto 0) := X"F";
    
    function map_speed(i : std_logic_vector(1 downto 0)) return std_logic_vector;

end package;

package body usb_pkg is
    function data_to_t_pipe(i: std_logic_vector(31 downto 0)) return t_pipe is
        variable ret : t_pipe;
    begin
        case i(1 downto 0) is
        when "01" =>
            ret.state := initialized;
        when "10" =>
            ret.state := stalled;
        when "11" =>
            ret.state := aborted;
        when others =>
            ret.state := invalid;
        end case;
        if i(2) = '1' then
            ret.direction := dir_out;
        else
            ret.direction := dir_in;
        end if;
        ret.device_address      := i(9 downto 3);
        ret.device_endpoint     := i(13 downto 10);
        ret.max_transfer        := unsigned(i(24 downto 14));
--        max_transfer(3 + to_integer(unsigned(i(16 downto 14)))) <= '1'; -- set one bit
        ret.data_toggle         := i(25);
        ret.control             := i(26);
        ret.timeout             := i(31);
--        case i(28 downto 27) is
--        when "00" =>
--            ret.transfer_mode := direct;
--        when "01" =>
--            ret.transfer_mode := use_preamble;
--        when "10" =>
--            ret.transfer_mode := use_split;
--        when others =>
--            ret.transfer_mode := direct;        
--        end case;
        return ret;
    end function;

    function t_pipe_to_data(i: t_pipe) return std_logic_vector is
        variable ret : std_logic_vector(31 downto 0);
    begin
        ret := (others => '0');
        case i.state is
        when initialized => ret(1 downto 0) := "01";
        when stalled     => ret(1 downto 0) := "10";
        when aborted     => ret(1 downto 0) := "11";
        when others      => ret(1 downto 0) := "00";
        end case;
        if i.direction = dir_out then
            ret(2) := '1';
        else
            ret(2) := '0';
        end if;
        ret(9 downto 3)   := i.device_address;
        ret(13 downto 10) := i.device_endpoint;
        ret(24 downto 14) := std_logic_vector(i.max_transfer);
        ret(25)           := i.data_toggle;
        ret(26)           := i.control;
        ret(31)           := i.timeout;
--        case i.transfer_mode is
--        when direct       => ret(28 downto 27) := "00";
--        when use_preamble => ret(28 downto 27) := "01";
--        when use_split    => ret(28 downto 27) := "10";
--        when others       => ret(28 downto 27) := "00";
--        end case;
        return ret;
    end function;

    function data_to_t_transaction(i: std_logic_vector(31 downto 0)) return t_transaction is
        variable ret : t_transaction;
    begin
        case i(1 downto 0) is        
        when "00" => ret.state := none;
        when "01" => ret.state := busy;
        when "10" => ret.state := done;
        when others => ret.state := error;
        end case;
        case i(3 downto 2) is
        when "00"   => ret.transaction_type := control;
        when "01"   => ret.transaction_type := bulk;
        when "10"   => ret.transaction_type := interrupt;
        when others => ret.transaction_type := isochronous;
        end case;
        ret.pipe_pointer    := unsigned(i(8 downto 4));
        ret.transfer_length := unsigned(i(19 downto 9));
        ret.buffer_address  := unsigned(i(30 downto 20));
        ret.link_to_next	:= i(31);
        return ret;
    end function;

    function t_transaction_to_data(i: t_transaction) return std_logic_vector is
        variable ret : std_logic_vector(31 downto 0);
    begin
        ret := (others => '0');
        case i.state is
        when none  => ret(1 downto 0) := "00";
        when busy  => ret(1 downto 0) := "01";
        when done  => ret(1 downto 0) := "10";
        when error => ret(1 downto 0) := "11";
        when others => ret(1 downto 0) := "11";
        end case;
        case i.transaction_type is
        when control     => ret(3 downto 2) := "00";
        when bulk        => ret(3 downto 2) := "01";
        when interrupt   => ret(3 downto 2) := "10";
        when isochronous => ret(3 downto 2) := "11";
        when others      => ret(3 downto 2) := "11";
        end case;
        ret(8 downto 4)  := std_logic_vector(i.pipe_pointer);
        ret(19 downto 9) := std_logic_vector(i.transfer_length);
        ret(30 downto 20):= std_logic_vector(i.buffer_address);
		ret(31) := i.link_to_next;
        return ret;                
    end function;

    function is_token(i : std_logic_vector(3 downto 0)) return boolean is
    begin
        case i is
            when c_pid_out   => return true;
            when c_pid_in    => return true;
            when c_pid_sof   => return true;
            when c_pid_setup => return true;
            when c_pid_pre   => return true;
            when c_pid_split => return true;
            when c_pid_ping  => return true;
            when others      => return false;
        end case;
        return false;
    end function;

    function is_handshake(i : std_logic_vector(3 downto 0)) return boolean is
    begin
        case i is
            when c_pid_ack   => return true;
            when c_pid_nak   => return true;
            when c_pid_nyet  => return true;
            when c_pid_stall => return true;
            when c_pid_err   => return true; -- reused!
            when others => return false;
        end case;
        return false;
    end function;

    function map_speed(i : std_logic_vector(1 downto 0)) return std_logic_vector is
    begin
        case i is
        when "00" =>
            return X"46"; -- LS mode
        when "01" =>
            return X"45"; -- FS mode
        when "10" =>
            return X"40"; -- HS mode
        when others =>
            return X"50"; -- stay in chirp mode
        end case;
        return X"00";
    end function;

end;
