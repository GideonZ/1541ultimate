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
use work.sid_io_regs_pkg.all;

entity sid_io_regs is
generic (
    g_filter_div  : natural := 221; -- for 50 MHz
    g_num_voices  : natural := 16 );
port (
    clock         : in  std_logic;
    reset         : in  std_logic;
                  
    io_req        : in  t_io_req;
    io_resp       : out t_io_resp;

    control       : out t_sid_control );

end sid_io_regs;


architecture registers of sid_io_regs is
    signal control_i    : t_sid_control;
begin
    control  <= control_i;
    
    p_bus: process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_sid_base_left    =>
                    control_i.base_left <= unsigned(io_req.data);
                when c_sid_base_right   =>
                    control_i.base_right <= unsigned(io_req.data);
                when c_sid_snoop_left   =>
                    control_i.snoop_left   <= io_req.data(0);
                when c_sid_snoop_right  =>
                    control_i.snoop_right  <= io_req.data(0);
                when c_sid_enable_left  =>
                    control_i.enable_left  <= io_req.data(0);
                when c_sid_enable_right =>
                    control_i.enable_right <= io_req.data(0);
                when c_sid_extend_left  =>
                    control_i.extend_left  <= io_req.data(0);
                when c_sid_extend_right =>
                    control_i.extend_right <= io_req.data(0);
                when c_sid_wavesel_left =>
                    control_i.comb_wave_left <= io_req.data(0);
                when c_sid_wavesel_right =>
                    control_i.comb_wave_right <= io_req.data(0);
                when others =>
                    null;
                end case;
            elsif io_req.read='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_sid_voices       =>
                    io_resp.data <= std_logic_vector(to_unsigned(g_num_voices, 8));
                when c_sid_filter_div   =>
                    io_resp.data <= std_logic_vector(to_unsigned(g_filter_div, 8));
                when c_sid_base_left    =>
                    io_resp.data <= std_logic_vector(control_i.base_left);
                when c_sid_base_right   =>
                    io_resp.data <= std_logic_vector(control_i.base_right);
                when c_sid_snoop_left   =>
                    io_resp.data(0) <= control_i.snoop_left;
                when c_sid_snoop_right  =>
                    io_resp.data(0) <= control_i.snoop_right;
                when c_sid_enable_left  =>
                    io_resp.data(0) <= control_i.enable_left;
                when c_sid_enable_right =>
                    io_resp.data(0) <= control_i.enable_right;
                when c_sid_extend_left  =>
                    io_resp.data(0) <= control_i.extend_left;
                when c_sid_extend_right =>
                    io_resp.data(0) <= control_i.extend_right;
                when c_sid_wavesel_left =>
                    io_resp.data(0) <= control_i.comb_wave_left;
                when c_sid_wavesel_right =>
                    io_resp.data(0) <= control_i.comb_wave_right;
                when others =>
                    null;
                end case;
            end if;
                        
            if reset='1' then
                control_i <= c_sid_control_init;
            end if;
        end if;
    end process;
end architecture;
