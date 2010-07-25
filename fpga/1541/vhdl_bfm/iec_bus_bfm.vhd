
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library std;
use std.textio.all;

package iec_bus_bfm_pkg is

    type t_iec_bus_bfm_object;
    type p_iec_bus_bfm_object is access t_iec_bus_bfm_object;
    
    type t_iec_status  is (ok, no_devices, no_response, timeout, no_eoi_ack);
    type t_iec_state   is (idle, talker, listener);
    type t_iec_command is (none, send_atn, send_msg, atn_to_listen);
    
    type t_iec_data is array(natural range <>) of std_logic_vector(7 downto 0);
    type t_iec_message is record
        data      : t_iec_data(0 to 256);
        len       : integer;
    end record;
    
    type t_iec_to_bfm is
    record
        command : t_iec_command;
    end record;
    
    type t_iec_from_bfm is
    record
        busy    : boolean;
    end record;

    constant c_iec_to_bfm_init : t_iec_to_bfm := (
        command => none );
    
    constant c_iec_from_bfm_init : t_iec_from_bfm := (
        busy    => false );

    type t_iec_bus_bfm_object is record
        next_bfm    : p_iec_bus_bfm_object;
        name        : string(1 to 256);

        -- interface to the user
        status      : t_iec_status;
        state       : t_iec_state;
        stopped     : boolean;
        sample_time : time;

        -- buffer
        msg_buf     : t_iec_message;

        -- internal to bfm
        to_bfm      : t_iec_to_bfm;
        
        -- internal from bfm
        from_bfm    : t_iec_from_bfm;

    end record;
    
    constant c_atn_to_ckl    : time := 5 us;
    constant c_atn_resp_max  : time := 1000 us;
    constant c_non_eoi       : time := 40 us;
    constant c_clk_low       : time := 50 us;
    constant c_clk_high      : time := 50 us;
    constant c_frame_hs_max  : time := 1000 us;
    constant c_frame_release : time := 20 us;
    constant c_byte_to_byte  : time := 100 us;
    constant c_eoi_min       : time := 200 us;
    constant c_eoi           : time := 500 us; -- was 250
    constant c_eoi_hold      : time := 60 us;
    constant c_tlkr_resp_dly : time := 60 us; -- max
    constant c_talk_atn_rel  : time := 30 us;
    constant c_talk_atn_ack  : time := 250 us; -- ?

    ------------------------------------------------------------------------------------
    shared variable iec_bus_bfms : p_iec_bus_bfm_object := null;
    ------------------------------------------------------------------------------------
    procedure register_iec_bus_bfm(named : string; variable pntr: inout p_iec_bus_bfm_object);
    procedure bind_iec_bus_bfm(named : string; variable pntr: inout p_iec_bus_bfm_object);
    ------------------------------------------------------------------------------------

    procedure iec_stop(variable bfm : inout p_iec_bus_bfm_object);
    procedure iec_talk(variable bfm : inout p_iec_bus_bfm_object);
    procedure iec_listen(variable bfm : inout p_iec_bus_bfm_object);

    procedure iec_send_atn(variable bfm : inout p_iec_bus_bfm_object;
                            byte : std_logic_vector(7 downto 0));

    procedure iec_turnaround(variable bfm : inout p_iec_bus_bfm_object);

    procedure iec_send_message(variable bfm : inout p_iec_bus_bfm_object;
                               msg: t_iec_message);

    procedure iec_send_message(variable bfm : inout p_iec_bus_bfm_object;
                               msg: string);

    procedure iec_get_message(variable bfm : inout p_iec_bus_bfm_object;
                              variable msg : inout t_iec_message);

    procedure iec_print_message(variable msg : inout t_iec_message);
end iec_bus_bfm_pkg;


package body iec_bus_bfm_pkg is        

    procedure register_iec_bus_bfm(named         : string;
                                   variable pntr : inout p_iec_bus_bfm_object) is
    begin
        -- Allocate a new BFM object in memory
        pntr := new t_iec_bus_bfm_object;

        -- Initialize object
        pntr.next_bfm  := null;
        pntr.name(named'range) := named;
        pntr.status      := ok;
        pntr.state       := idle;
        pntr.stopped     := false; -- active;
        pntr.sample_time := 1 us;
        pntr.to_bfm      := c_iec_to_bfm_init;
        pntr.from_bfm    := c_iec_from_bfm_init;
        
        -- add this pointer to the head of the linked list
        if iec_bus_bfms = null then -- first entry
            iec_bus_bfms := pntr;
        else -- insert new entry
            pntr.next_bfm := iec_bus_bfms;
            iec_bus_bfms := pntr;
        end if;
    end register_iec_bus_bfm;

    procedure bind_iec_bus_bfm(named         : string;
                               variable pntr : inout p_iec_bus_bfm_object) is
        variable p : p_iec_bus_bfm_object;
    begin
        pntr := null;
        wait for 1 ns;  -- needed to make sure that binding takes place after registration

        p := iec_bus_bfms; -- start at the root
        L1: while p /= null loop
            if p.name(named'range) = named then
                pntr := p;
                exit L1;
            else
                p := p.next_bfm;
            end if;
        end loop;
    end bind_iec_bus_bfm;

------------------------------------------------------------------------------

    procedure iec_stop(variable bfm : inout p_iec_bus_bfm_object) is
    begin
        bfm.stopped := true;
    end procedure;
    
    procedure iec_talk(variable bfm : inout p_iec_bus_bfm_object) is
    begin
        bfm.state := talker;
    end procedure;
    
    procedure iec_listen(variable bfm : inout p_iec_bus_bfm_object) is
    begin
        bfm.state := listener;
    end procedure;

    procedure iec_send_atn(variable bfm : inout p_iec_bus_bfm_object;
                           byte : std_logic_vector(7 downto 0)) is
    begin
        bfm.msg_buf.data(0) := byte;
        bfm.msg_buf.len     := 1;
        bfm.to_bfm.command  := send_atn;
        wait for bfm.sample_time;
        wait for bfm.sample_time;
        while bfm.from_bfm.busy loop
            wait for bfm.sample_time;
        end loop;
    end procedure;

    procedure iec_turnaround(variable bfm : inout p_iec_bus_bfm_object) is
    begin
        bfm.to_bfm.command := atn_to_listen;
        wait for bfm.sample_time;
        wait for bfm.sample_time;
        while bfm.from_bfm.busy loop
            wait for bfm.sample_time;
        end loop;
    end procedure;        

    procedure iec_send_message(variable bfm : inout p_iec_bus_bfm_object;
                               msg: t_iec_message) is
    begin
        bfm.msg_buf        := msg;
        bfm.to_bfm.command := send_msg;
        wait for bfm.sample_time;
        wait for bfm.sample_time;
        while bfm.from_bfm.busy loop
            wait for bfm.sample_time;
        end loop;
    end procedure;

    procedure iec_send_message(variable bfm : inout p_iec_bus_bfm_object;
                               msg: string) is
        variable leng   : integer;
    begin
        leng := msg'length;
        for i in 1 to leng loop
            bfm.msg_buf.data(i-1) := conv_std_logic_vector(character'pos(msg(i)), 8);
        end loop;
        bfm.msg_buf.len := leng;
        iec_send_message(bfm, bfm.msg_buf);
    end procedure;

    procedure iec_get_message(variable bfm : inout p_iec_bus_bfm_object;
                              variable msg : inout t_iec_message) is
    begin
        wait for bfm.sample_time;
        wait for bfm.sample_time;
        while bfm.state = listener loop        
            wait for bfm.sample_time;
        end loop;
        msg := bfm.msg_buf;
    end procedure;

    procedure iec_print_message(variable msg : inout t_iec_message) is
        variable L : line;
        variable c : character;
    begin
        for i in 0 to msg.len-1 loop
            c := character'val(conv_integer(msg.data(i)));
            write(L, c);
        end loop;
        writeline(output, L);
    end procedure;
end;

------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith;

library work;
use work.iec_bus_bfm_pkg.all;

library std;
use std.textio.all;

entity iec_bus_bfm is
port (
    iec_clock   : inout std_logic;
    iec_data    : inout std_logic;
    iec_atn     : inout std_logic );
end iec_bus_bfm;

architecture bfm of iec_bus_bfm is
    shared variable this : p_iec_bus_bfm_object := null;
    signal bound : boolean := false;

    signal clk_i    : std_logic;
    signal clk_o    : std_logic;
    signal data_i   : std_logic;
    signal data_o   : std_logic;
    signal atn_i    : std_logic;
    signal atn_o    : std_logic;

begin
    -- this process registers this instance of the bfm to the server package
    bind: process
    begin
        register_iec_bus_bfm(iec_bus_bfm'path_name, this);
        bound <= true;
        wait;
    end process;

    -- open collector logic
    clk_i  <= iec_clock and '1';
    data_i <= iec_data  and '1';
    atn_i  <= iec_atn   and '1';

    iec_clock <= '0' when clk_o='0'  else 'H';
    iec_data  <= '0' when data_o='0' else 'H';
    iec_atn   <= '0' when atn_o='0'  else 'H';
    
--         |<--------- Byte sent under attention (to devices) ------------>|
--
-- ___    ____                                                        _____ _____
-- ATN       |________________________________________________________|
--           :                                                        :
-- ___    ______     ________ ___ ___ ___ ___ ___ ___ ___ ___         :
-- CLK       : |_____|      |_| |_| |_| |_| |_| |_| |_| |_| |______________ _____
--           :       :        :                             :         :
--           : Tat : :Th: Tne :                             : Tf : Tr :
-- ____   ________ : :  :___________________________________:____:
-- DATA   ___|\\\\\__:__|    |__||__||__||__||__||__||__||__|    |_________ _____
--                      :     0   1   2   3   4   5   6   7      :
--                      :    LSB                         MSB     :
--                      :     :                                  :
--                      :     : Data Valid          Listener: Data Accepted
--                      : Listener READY-FOR-DATA

    protocol: process
        procedure do_send_atn is
        begin
            atn_o <= '0';
            wait for c_atn_to_ckl;
            clk_o <= '0';
            if data_i='1' then
                wait until data_i='0' for c_atn_resp_max;
            end if;
            if data_i='1' then
                this.status := no_devices;
                return;
            end if;
            clk_o <= '1';
            wait until data_i='1'; -- for... (listener hold-off could be infinite)
            wait for c_non_eoi;
            for i in 0 to 7 loop
                clk_o  <= '0';
                data_o <= this.msg_buf.data(0)(i);
                wait for c_clk_low;
                clk_o  <= '1';
                wait for c_clk_high;
            end loop;
            clk_o  <= '0';
            data_o <= '1';
            
            wait until data_i='0' for c_frame_hs_max;
            if data_i='1' then
                this.status := no_response;
            else
                this.status := ok;
            end if;
            wait for c_frame_release;
            atn_o <= '1';

        end procedure;

        procedure send_byte(byte : std_logic_vector(7 downto 0)) is
        begin
            clk_o <= '1';
            wait until data_i='1'; -- for... (listener hold-off could be infinite)
            wait for c_non_eoi;
            for i in 0 to 7 loop
                clk_o  <= '0';
                data_o <= byte(i);
                wait for c_clk_low;
                clk_o  <= '1';
                wait for c_clk_high;
            end loop;
            clk_o  <= '0';
            data_o <= '1';
            
            wait until data_i='0' for c_frame_hs_max;
            if data_i='1' then
                this.status := no_response;
            else
                this.status := ok;
            end if;
            wait for c_byte_to_byte;
        end procedure;

        procedure end_handshake(byte : std_logic_vector(7 downto 0)) is
        begin
            clk_o <= '1';
            wait until data_i='1'; -- for... (listener hold-off could be infinite)
--            wait for c_eoi;
--            data_o <= '0';
--            wait for c_eoi_hold;
--            data_o <= '1';
            wait until data_i='0' for c_eoi; -- wait for 250 µs to see that listener has acked eoi
            if data_i='1' then
                this.status := no_eoi_ack;
                return;
            end if;
            wait until data_i='1'; -- wait for listener to be ready again

            wait for c_tlkr_resp_dly;
            for i in 0 to 7 loop
                clk_o  <= '0';
                data_o <= byte(i);
                wait for c_clk_low;
                clk_o  <= '1';
                wait for c_clk_high;
            end loop;

            clk_o  <= '0';
            data_o <= '1';

            wait until data_i='0' for c_frame_hs_max;
            if data_i='1' then
                this.status := no_response;
            else
                this.status := ok;
            end if;
        end procedure;

        procedure talk_atn_turnaround is
        begin
            wait for c_talk_atn_rel;
            clk_o  <= '1';
            data_o <= '0';
            wait for c_talk_atn_rel;
            wait until clk_i = '0';
            
            this.state       := listener;
            this.msg_buf.len := 0;  -- clear buffer for incoming data
        end procedure;

        procedure receive_byte is
            variable b   : std_logic_vector(7 downto 0);
            variable eoi : boolean;
            variable c   : character;
            variable L   : LINE;
        begin
            eoi := false;

            if clk_i='0' then
                wait until clk_i='1';
            end if;
            wait for c_clk_low; -- dummy
            data_o <= '1';
            
            -- check for end of message handshake (data pulses low after >200 µs for >60 µs)
            wait until clk_i = '0' for c_eoi_min;
            if clk_i='1' then -- eoi timeout
                eoi := true;
                -- ack eoi
                data_o <= '0';
                wait for c_eoi_hold;
                data_o <= '1';
            end if;
                                        
            for i in 0 to 7 loop
                wait until clk_i='1';
                b(i) := data_i;
            end loop;

--            c := character'val(conv_integer(b));
--            write(L, c);
--            writeline(output, L);
--
            this.msg_buf.data(this.msg_buf.len) := b;
            this.msg_buf.len := this.msg_buf.len + 1;
            wait until clk_i='0';

            if eoi then
                this.state := idle;
                data_o <= '1';
            else
                data_o <= '0';
            end if;

        end procedure;

    begin
        atn_o  <= '1';
        data_o <= '1';
        clk_o  <= '1';
        wait until bound;
        
        while not this.stopped loop
            wait for this.sample_time;
            
            case this.to_bfm.command is
            when none =>
                null;

            when send_atn =>
                this.from_bfm.busy := true;
                do_send_atn;
                this.from_bfm.busy := false;
            
            when send_msg =>
                this.from_bfm.busy := true;
                if this.msg_buf.len > 1 then
                    L1: for i in 0 to this.msg_buf.len-2 loop
                        send_byte(this.msg_buf.data(i));
                        if this.status /= ok then
                            exit L1;
                        end if;
                    end loop;
                end if;
                assert this.status = ok
                    report "Sending data message failed."
                    severity error;
                    
                end_handshake(this.msg_buf.data(this.msg_buf.len-1));

                assert this.status = ok
                    report "Sending data message failed (Last Byte)."
                    severity error;

                this.from_bfm.busy := false;
                
            when atn_to_listen =>
                this.from_bfm.busy := true;
                talk_atn_turnaround;
                this.from_bfm.busy := false;
                
            end case;            

            this.to_bfm.command := none;

            if this.state = listener then
                receive_byte;
            end if;

        end loop;
        
        wait;
    end process;

    -- if in idle state, and atn_i becomes '0', then become device and listen
    -- but that is only needed for devices... (not for the controller)
    
    -- if listener (means that I am addressed), listen to all bytes
    -- if end of message is detected, switch back to idle state.
end bfm;

