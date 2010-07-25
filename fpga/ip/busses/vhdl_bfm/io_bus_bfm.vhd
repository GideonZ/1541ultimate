
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;

library std;
use std.textio.all;

entity io_bus_bfm is
generic (
    g_name      : string );
port (
    clock       : in    std_logic;

    req         : out   t_io_req;
    resp        : in    t_io_resp );

end io_bus_bfm;

architecture bfm of io_bus_bfm is
    shared variable this : p_io_bus_bfm_object := null;
    signal bound : boolean := false;
    type t_state is (idle, wait_for_response);
    signal state : t_state := idle;

begin
    -- this process registers this instance of the bfm to the server package
    bind: process
    begin
        register_io_bus_bfm(g_name, this);
        bound <= true;
        wait;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            req <= c_io_req_init;
            
            case state is
            when idle =>
                if bound then
                    if this.command = e_io_read then
                        req.address <= this.address;
                        req.read <= '1';
                        state <= wait_for_response;
                    elsif this.command = e_io_write then
                        req.address <= this.address;
                        req.write <= '1';
                        req.data <= this.data;
                        state <= wait_for_response;
                    end if;
                end if;
            when wait_for_response =>
                if resp.ack='1' then
                    this.data := resp.data;
                    this.command := e_io_none;
                    state <= idle;
                end if;
            when others =>
                null;
            end case;
        end if;
    end process;

end bfm;
