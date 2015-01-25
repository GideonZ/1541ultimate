
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package usb_pkg is

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

    constant c_pid_pre          : std_logic_vector(3 downto 0) := X"C"; -- token ->
    constant c_pid_err          : std_logic_vector(3 downto 0) := X"C"; -- handshake <-
    constant c_pid_split        : std_logic_vector(3 downto 0) := X"8"; -- token ->
    constant c_pid_ping         : std_logic_vector(3 downto 0) := X"4"; -- token
    constant c_pid_reserved     : std_logic_vector(3 downto 0) := X"0";

    function is_token(i : std_logic_vector(3 downto 0)) return boolean;
    function is_split(i : std_logic_vector(3 downto 0)) return boolean;
    function is_handshake(i : std_logic_vector(3 downto 0)) return boolean;
    function get_togglebit(i : std_logic_vector(3 downto 0)) return std_logic;
    
    type t_token is record
        device_addr     : std_logic_vector(6 downto 0);
        endpoint_addr   : std_logic_vector(3 downto 0);
    end record;

    constant c_token_init      : t_token := ( "0000000", "0000" );
    constant c_token_undefined : t_token := ( "XXXXXXX", "XXXX" );

    type t_split_token is record
        hub_address     : std_logic_vector(6 downto 0);
        sc              : std_logic;
        port_address    : std_logic_vector(6 downto 0);
        s               : std_logic; -- start/speed (isochronous out start split), for interrupt/control transfers: Speed. 0=full, 1=low
        e               : std_logic; -- end    (isochronous out start split) 00=middle, 10=beginning, 01=end, 11=all
        et              : std_logic_vector(1 downto 0); -- 00=control, 01=iso, 10=bulk, 11=interrupt
    end record;

    constant c_split_token_init : t_split_token := (
        hub_address     => "0000000",
        sc              => '0',
        port_address    => "0000000",
        s               => '0',
        e               => '0',
        et              => "00" );
       
    constant c_split_token_undefined : t_split_token := (
        hub_address     => "XXXXXXX",
        sc              => 'X',
        port_address    => "XXXXXXX",
        s               => 'X',
        e               => 'X',
        et              => "XX" );

    constant c_split_token_bogus : t_split_token := (
        hub_address     => "0000001",
        sc              => '0',
        port_address    => "0000010",
        s               => '0',
        e               => '0',
        et              => "11" );

    type t_usb_rx is record
        receiving       : std_logic;
        valid_token     : std_logic;
        valid_split     : std_logic;
        valid_handsh    : std_logic;
        valid_packet    : std_logic;
        error           : std_logic;
        
        pid             : std_logic_vector(3 downto 0);
        token           : t_token;
        split_token     : t_split_token;
    
        data_valid      : std_logic;
        data_start      : std_logic;
        data            : std_logic_vector(7 downto 0);
    end record;

    type t_usb_tx_req is record
        send_token      : std_logic;
        send_split      : std_logic;
        send_handsh     : std_logic;
        send_packet     : std_logic;
                        
        pid             : std_logic_vector(3 downto 0);
        token           : t_token;
        split_token     : t_split_token;
                        
        no_data         : std_logic;
        data            : std_logic_vector(7 downto 0);
        data_valid      : std_logic;
        data_last       : std_logic;
    end record;
    
    type t_usb_tx_resp is record
        request_ack     : std_logic;
        busy            : std_logic;
        data_wait       : std_logic;
    end record;

    type t_usb_tx_req_array is array(natural range <>) of t_usb_tx_req;

    -- function or_reduce(a : t_usb_tx_req_array) return t_usb_tx_req;

    constant c_usb_rx_init : t_usb_rx := (
        receiving       => '0',
        valid_token     => '0',
        valid_split     => '0',
        valid_handsh    => '0',
        valid_packet    => '0',
        error           => '0',
        
        pid             => X"0",
        token           => c_token_init,
        split_token     => c_split_token_init,
    
        data_valid      => '0',
        data_start      => '0',
        data            => X"00" );

    constant c_usb_tx_req_init : t_usb_tx_req := (
        send_token      => '0',
        send_split      => '0',
        send_handsh     => '0',
        send_packet     => '0',
                        
        pid             => c_pid_reserved,
        token           => c_token_init,
        split_token     => c_split_token_init,
                        
        no_data         => '0',
        data            => X"00",
        data_valid      => '0',
        data_last       => '0' );
    
    constant c_usb_tx_ack : t_usb_tx_req := (
        send_token      => '0',
        send_split      => '0',
        send_handsh     => '1',
        send_packet     => '0',
                        
        pid             => c_pid_ack,
        token           => c_token_undefined,
        split_token     => c_split_token_undefined,
                        
        no_data         => 'X',
        data            => "XXXXXXXX",
        data_valid      => '0',
        data_last       => 'X' );

    constant c_usb_tx_nack : t_usb_tx_req := (
        send_token      => '0',
        send_split      => '0',
        send_handsh     => '1',
        send_packet     => '0',
                        
        pid             => c_pid_nak,
        token           => c_token_undefined,
        split_token     => c_split_token_undefined,
                        
        no_data         => 'X',
        data            => "XXXXXXXX",
        data_valid      => '0',
        data_last       => 'X' );

    constant c_usb_tx_nyet : t_usb_tx_req := (
        send_token      => '0',
        send_split      => '0',
        send_handsh     => '1',
        send_packet     => '0',
                        
        pid             => c_pid_nyet,
        token           => c_token_undefined,
        split_token     => c_split_token_undefined,
                        
        no_data         => 'X',
        data            => "XXXXXXXX",
        data_valid      => '0',
        data_last       => 'X' );

    constant c_usb_tx_stall : t_usb_tx_req := (
        send_token      => '0',
        send_split      => '0',
        send_handsh     => '1',
        send_packet     => '0',
                        
        pid             => c_pid_stall,
        token           => c_token_undefined,
        split_token     => c_split_token_undefined,
                        
        no_data         => 'X',
        data            => "XXXXXXXX",
        data_valid      => '0',
        data_last       => 'X' );

    constant c_usb_tx_data_out0 : t_usb_tx_req := (
        send_token      => '0',
        send_split      => '0',
        send_handsh     => '0',
        send_packet     => '1',
                        
        pid             => c_pid_data0,
        token           => c_token_undefined,
        split_token     => c_split_token_undefined,
                        
        no_data         => '0',
        data            => "XXXXXXXX",
        data_valid      => '0',
        data_last       => '0' );
    
    constant c_usb_tx_data_out1 : t_usb_tx_req := (
        send_token      => '0',
        send_split      => '0',
        send_handsh     => '0',
        send_packet     => '1',
                        
        pid             => c_pid_data1,
        token           => c_token_undefined,
        split_token     => c_split_token_undefined,
                        
        no_data         => '0',
        data            => "XXXXXXXX",
        data_valid      => '0',
        data_last       => '0' );

    function token_to_vector(t : t_token) return std_logic_vector;
    function vector_to_token(v : std_logic_vector) return t_token;
    function split_token_to_vector(t : t_split_token) return std_logic_vector;
    function vector_to_split_token(v : std_logic_vector) return t_split_token;
    
end package;

package body usb_pkg is

    function is_token(i : std_logic_vector(3 downto 0)) return boolean is
    begin
        case i is
            when c_pid_out   => return true;
            when c_pid_in    => return true;
            when c_pid_sof   => return true;
            when c_pid_setup => return true;
            when c_pid_pre   => return true;
            when c_pid_ping  => return true;
            when others      => return false;
        end case;
        return false;
    end function;

    function is_split(i : std_logic_vector(3 downto 0)) return boolean is
    begin
        return (i = c_pid_split);
    end function;

    function is_handshake(i : std_logic_vector(3 downto 0)) return boolean is
    begin
        case i is
            when c_pid_ack   => return true;
            when c_pid_nak   => return true;
            when c_pid_nyet  => return true;
            when c_pid_stall => return true;
            when c_pid_err   => return true; -- reused! HUB reply to CSPLIT
            when others => return false;
        end case;
        return false;
    end function;
    
    function get_togglebit(i : std_logic_vector(3 downto 0)) return std_logic is
    begin
        return i(3);
    end function;

    function split_token_to_vector(t : t_split_token) return std_logic_vector is
        variable ret : std_logic_vector(18 downto 0);
    begin
        ret(18 downto 17) := t.et;
        ret(16)           := t.e;
        ret(15)           := t.s;
        ret(14 downto 8)  := t.port_address;
        ret(7)            := t.sc;
        ret(6 downto 0)   := t.hub_address;
        return ret;
    end function;

    function vector_to_split_token(v : std_logic_vector) return t_split_token is
        variable va  : std_logic_vector(18 downto 0);
        variable ret : t_split_token;
    begin
        va := v;
        ret.et           := va(18 downto 17);
        ret.e            := va(16);
        ret.s            := va(15);
        ret.port_address := va(14 downto 8);
        ret.sc           := va(7);
        ret.hub_address  := va(6 downto 0);
        return ret;
    end function;

    -- Token conversion
    function token_to_vector(t : t_token) return std_logic_vector is
        variable ret : std_logic_vector(10 downto 0);
    begin
        ret := t.endpoint_addr & t.device_addr;
        return ret;
    end function;
    
    function vector_to_token(v : std_logic_vector) return t_token is
        alias va : std_logic_vector(10 downto 0) is v;
        variable ret : t_token;
    begin
        ret.device_addr := va(6 downto 0);
        ret.endpoint_addr := va(10 downto 7);
        return ret;
    end function;

end;
