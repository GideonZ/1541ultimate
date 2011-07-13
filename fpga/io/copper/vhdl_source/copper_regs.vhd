-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.copper_pkg.all;

entity copper_regs is
port (
    clock         : in  std_logic;
    reset         : in  std_logic;
                  
    io_req        : in  t_io_req;
    io_resp       : out t_io_resp;

    control       : out t_copper_control;
    status        : in  t_copper_status );

end copper_regs;


architecture registers of copper_regs is
    signal control_i    : t_copper_control;
begin
    control  <= control_i;
    
    p_bus: process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;

            control_i.command <= X"0";
            control_i.stop    <= '0';
            
            if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_copper_command    =>
                    control_i.command <= io_req.data(3 downto 0);
                when c_copper_framelen_l =>
                    control_i.frame_length(7 downto 0) <= unsigned(io_req.data);
                when c_copper_framelen_h =>
                    control_i.frame_length(15 downto 8) <= unsigned(io_req.data);
                when c_copper_break      =>
                    control_i.stop <= '1';
                when others =>
                    null;
                end case;
            elsif io_req.read='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_copper_status     =>
                    io_resp.data(0) <= status.running;
                when c_copper_measure_l  =>
                    io_resp.data <= std_logic_vector(status.measured_time(7 downto 0));
                when c_copper_measure_h  =>
                    io_resp.data <= std_logic_vector(status.measured_time(15 downto 8));
                when c_copper_framelen_l =>
                    io_resp.data <= std_logic_vector(control_i.frame_length(7 downto 0));
                when c_copper_framelen_h =>
                    io_resp.data <= std_logic_vector(control_i.frame_length(15 downto 8));
                when others =>
                    null;
                end case;
            end if;
                        
            if reset='1' then
                control_i <= c_copper_control_init;
            end if;
        end if;
    end process;
end architecture;
