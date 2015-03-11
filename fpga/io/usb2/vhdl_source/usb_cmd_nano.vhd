--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_cmd_nano
-- Date:2015-02-14  
-- Author: Gideon     
-- Description: I/O registers for controlling commands directly, 16 bits for
--              attachment to nano cpu.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.usb_cmd_pkg.all;

entity usb_cmd_nano is
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;

        io_addr     : in  unsigned(7 downto 0);
        io_write    : in  std_logic;
        io_read     : in  std_logic;
        io_wdata    : in  std_logic_vector(15 downto 0);
        io_rdata    : out std_logic_vector(15 downto 0);

        cmd_req     : out t_usb_cmd_req;
        cmd_resp    : in  t_usb_cmd_resp );

end entity;

architecture arch of usb_cmd_nano is
    signal done_latch   : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if cmd_resp.done = '1' then
                cmd_req.request <= '0';
                done_latch <= '1';
            end if;

            if io_write = '1' then
                case io_addr is
                when X"60" => -- command request
                    cmd_req.request <= '1';
                    done_latch <=  '0';
                    cmd_req.togglebit <= io_wdata(11);
                    --cmd_req.do_split <= io_wdata(7);
                    cmd_req.do_data <= io_wdata(6);
                    cmd_req.command <= c_usb_commands_decoded(to_integer(unsigned(io_wdata(2 downto 0))));
    
                when X"61" => -- data buffer control
                    cmd_req.buffer_index <= unsigned(io_wdata(15 downto 14));
                    cmd_req.no_data <= io_wdata(13);
                    cmd_req.data_length(9 downto 0) <= unsigned(io_wdata(9 downto 0));
    
                when X"62" => -- device/endpoint
                    cmd_req.device_addr <= unsigned(io_wdata(14 downto 8));
                    cmd_req.endp_addr <= unsigned(io_wdata(3 downto 0));
    
                when X"63" => -- split info
                    cmd_req.do_split <= io_wdata(15);
                    cmd_req.split_hub_addr <= unsigned(io_wdata(14 downto 8));
                    cmd_req.split_port_addr <= unsigned(io_wdata(3 downto 0));
                    cmd_req.split_sc <= io_wdata(7);
                    cmd_req.split_sp <= io_wdata(6);
                    cmd_req.split_et <= io_wdata(5 downto 4);
                    
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
    
    process(io_read, io_addr, cmd_resp, done_latch) 
    begin
        io_rdata <= (others => '0');
        
        if io_read = '1' then
            case io_addr is
            when X"64" =>
                io_rdata(15) <= done_latch;
                io_rdata(14 downto 12) <= std_logic_vector(to_unsigned(t_usb_result'pos(cmd_resp.result), 3));
                io_rdata(11) <= cmd_resp.togglebit;
                io_rdata(10) <= cmd_resp.no_data;
                io_rdata(9 downto 0) <= std_logic_vector(cmd_resp.data_length(9 downto 0));            

            when others =>
                null;
            end case;
        end if;
    end process;
end arch;
