
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.slot_bus_pkg.all;
use work.slot_bus_master_bfm_pkg.all;

entity slot_bus_master_bfm is
generic (
    g_name      : string );
port (
    clock       : in    std_logic;

    req         : out   t_slot_req;
    resp        : in    t_slot_resp );

end slot_bus_master_bfm;

architecture bfm of slot_bus_master_bfm is
    shared variable this : p_slot_bus_master_bfm_object := null;
    signal bound : boolean := false;
    type t_state is (idle, exec);
    signal state : t_state := idle;
	signal delay	: integer := 0;
begin
    -- this process registers this instance of the bfm to the server package
    bind: process
    begin
        register_slot_bus_master_bfm(g_name, this);
        bound <= true;
        wait;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
			req.bus_write <= '0';
			req.io_read   <= '0';
			req.io_write  <= '0';
			this.irq_pending := (resp.irq = '1');

            case state is
            when idle =>
                req <= c_slot_req_init;
                if bound then
					delay <= 3;
					if this.command /= e_slot_none then
	                    req.io_address <= this.address;
	                    req.bus_address <= this.address;
	                    req.data <= this.data;
					end if;

					case this.command is
					when e_slot_io_read =>
						state <= exec;
					when e_slot_io_write =>
						state <= exec;
					when e_slot_bus_read =>
						state <= exec;
					when e_slot_bus_write =>
						req.bus_write <= '1';
						state <= exec;
					when others =>
						null;
					end case;
				end if;

            when exec =>
				if delay=0 then
					case this.command is
					when e_slot_io_read =>
						req.io_read <= '1';
					when e_slot_io_write =>
						req.io_write <= '1';
					when others =>
						null;
					end case;
					if resp.reg_output='1' then
						this.data := resp.data;
					else
						this.data := (others => 'X');
					end if;						
                    this.command := e_slot_none;
					state <= idle;
				else
					delay <= delay - 1;
				end if;           	
            
            when others =>
                null;
            end case;
        end if;
    end process;

end bfm;
