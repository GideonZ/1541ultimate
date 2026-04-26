-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator Registers
-------------------------------------------------------------------------------
-- File       : char_generator_regs.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Registers for the character generator
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.char_generator_pkg.all;

entity char_generator_regs is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;

    control         : out t_chargen_control );
end entity;

architecture gideon of char_generator_regs is
    signal control_i        : t_chargen_control := c_chargen_control_init;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            
            if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                    when c_chargen_line_clocks_hi =>
                        control_i.clocks_per_line(10 downto 8) <= unsigned(io_req.data(2 downto 0));
                    when c_chargen_line_clocks_lo =>
                        control_i.clocks_per_line(7 downto 0) <= unsigned(io_req.data); 
                    when c_chargen_char_width =>
                        control_i.char_width <= unsigned(io_req.data(3 downto 0));
                    when c_chargen_char_height =>
                        control_i.char_height <= unsigned(io_req.data(4 downto 0));
                        control_i.big_font <= io_req.data(6);
                        control_i.stretch_y <= io_req.data(7);
                    when c_chargen_chars_per_line =>
                        control_i.chars_per_line <= unsigned(io_req.data);
                    when c_chargen_active_lines =>
                        control_i.active_lines <= unsigned(io_req.data(5 downto 0));
                    when c_chargen_x_on_hi =>
                        control_i.x_on(11 downto 8) <= unsigned(io_req.data(3 downto 0));
                    when c_chargen_x_on_lo =>
                        control_i.x_on(7 downto 0) <= unsigned(io_req.data);
                    when c_chargen_y_on_hi =>
                        control_i.y_on(11 downto 8) <= unsigned(io_req.data(3 downto 0));
                    when c_chargen_y_on_lo =>
                        control_i.y_on(7 downto 0) <= unsigned(io_req.data);
                    when c_chargen_pointer_hi =>
                        control_i.pointer(14 downto 8) <= unsigned(io_req.data(6 downto 0));
                    when c_chargen_pointer_lo =>
                        control_i.pointer(7 downto 0) <= unsigned(io_req.data);
                    when c_chargen_perform_sync =>
                        control_i.perform_sync <= io_req.data(0);
                    when c_chargen_transparency =>
                        control_i.transparent  <= io_req.data(3 downto 0);
                        control_i.own_keyboard <= io_req.data(6);
                        control_i.overlay_on   <= io_req.data(7);
                    when others =>
                        null;
                end case;
            elsif io_req.read='1' then
                io_resp.ack <= '1';
            end if;

            if reset='1' then
--                control_i <= c_chargen_control_init;
            end if;
        end if;
    end process;
    
    control <= control_i;
end gideon;
