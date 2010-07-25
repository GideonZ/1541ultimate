
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.mem_bus_master_bfm_pkg.all;

library std;
use std.textio.all;

entity mem_bus_master_bfm is
generic (
    g_name      : string );
port (
    clock       : in    std_logic;

    req         : out   t_mem_req;
    resp        : in    t_mem_resp );

end mem_bus_master_bfm;

architecture bfm of mem_bus_master_bfm is
    shared variable this : p_mem_bus_master_bfm_object := null;
    signal bound : boolean := false;
    type t_state is (idle, wait_for_rack, wait_for_data);
    signal state : t_state := idle;

begin
    -- this process registers this instance of the bfm to the server package
    bind: process
    begin
        register_mem_bus_master_bfm(g_name, this);
        bound <= true;
        wait;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                req <= c_mem_req_init;
                if bound then
                    if this.command = e_mem_read then
                        req.tag <= this.tag;
                        req.address <= this.address;
                        req.request <= '1';
                        req.read_writen <= '1';
                        state <= wait_for_data;
                    elsif this.command = e_mem_write then
                        req.tag <= this.tag;
                        req.address <= this.address;
                        req.request <= '1';
                        req.read_writen <= '0';
                        req.data <= this.data;
                        state <= wait_for_rack;
                    end if;
                end if;

            when wait_for_rack =>
                if resp.rack='1' then
                    req.request <= '0';
                    this.command := e_mem_none;
                    state <= idle;
                end if;
            
            when wait_for_data =>
                if resp.rack='1' then
                    req.request <= '0';
                end if;
                if to_integer(unsigned(resp.dack_tag)) /= 0 then
                    this.data := resp.data;
                    this.command := e_mem_none;
                    state <= idle;
                end if;

            when others =>
                null;
            end case;
        end if;
    end process;

end bfm;
