--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_cmd_io
-- Date:2015-01-18  
-- Author: Gideon     
-- Description: I/O registers for controlling commands directly
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.usb_cmd_pkg.all;

entity usb_cmd_io is
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;

        io_req      : in  t_io_req;
        io_resp     : out t_io_resp;
        
        cmd_req     : out t_usb_cmd_req;
        cmd_resp    : in  t_usb_cmd_resp );

end entity;

architecture arch of usb_cmd_io is
    signal done_latch   : std_logic;
begin

    process(clock)
    begin
        if rising_edge(clock) then
            if cmd_resp.done = '1' then
                cmd_req.request <= '0';
                done_latch <= '1';
            end if;

            io_resp <= c_io_resp_init;
            if io_req.write = '1' then
                io_resp.ack <= '1';

                case io_req.address(3 downto 0) is
                when X"1" =>
                    cmd_req.request <= '1';
                    done_latch <=  '0';
                    cmd_req.do_split <= io_req.data(7);
                    cmd_req.do_data <= io_req.data(6);
                    cmd_req.command <= c_usb_commands_decoded(to_integer(unsigned(io_req.data(2 downto 0))));
    
                when X"6" => -- high of data buffer control
                    cmd_req.buffer_index <= unsigned(io_req.data(7 downto 6));
                    cmd_req.no_data <= io_req.data(5);
                    cmd_req.togglebit <= io_req.data(4);
                    cmd_req.data_length(9 downto 8) <= unsigned(io_req.data(1 downto 0));
                when X"7" =>            
                    cmd_req.data_length(7 downto 0) <= unsigned(io_req.data);
    
                when X"A" =>
                    cmd_req.device_addr <= unsigned(io_req.data(6 downto 0));
    
                when X"B" =>
                    cmd_req.endp_addr <= unsigned(io_req.data(3 downto 0));
    
                when X"E" =>
                    cmd_req.split_hub_addr <= unsigned(io_req.data(6 downto 0));
                
                when X"F" =>
                    cmd_req.split_port_addr <= unsigned(io_req.data(3 downto 0));
                    cmd_req.split_sc <= io_req.data(7);
                    cmd_req.split_sp <= io_req.data(6);
                    cmd_req.split_et <= io_req.data(5 downto 4);
                    
                when others =>
                    null;
                end case;
            elsif io_req.read = '1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when X"0" =>
                    io_resp.data(7) <= done_latch;
                    io_resp.data(6 downto 4) <= std_logic_vector(to_unsigned(t_usb_result'pos(cmd_resp.result), 3));
                    io_resp.data(2) <= cmd_resp.no_data;
                    io_resp.data(3) <= cmd_resp.togglebit;
                    io_resp.data(1 downto 0) <= std_logic_vector(cmd_resp.data_length(9 downto 8));

                when X"1" =>
                    io_resp.data <= std_logic_vector(cmd_resp.data_length(7 downto 0));            
                when others =>
                    null;
                end case;
            end if;

            if reset='1' then
                done_latch <= '0';
                cmd_req.request <= '0';
            end if;
        end if;
    end process;
end arch;

