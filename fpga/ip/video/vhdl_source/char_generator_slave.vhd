-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator
-------------------------------------------------------------------------------
-- File       : char_generator_slave.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Character generator
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.char_generator_pkg.all;

entity char_generator_slave is
generic (
	g_screen_size	: natural := 11 );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    h_count         : in  unsigned(11 downto 0);
    v_count         : in  unsigned(11 downto 0);
    
    control         : in  t_chargen_control;

    screen_addr     : out unsigned(g_screen_size-1 downto 0);
    screen_data     : in  std_logic_vector(7 downto 0);
	color_data		: in  std_logic_vector(7 downto 0);
	
    char_addr       : out unsigned(10 downto 0);
    char_data       : in  std_logic_vector(7 downto 0);

    pixel_active    : out std_logic;
    pixel_opaque    : out std_logic;
    pixel_data      : out unsigned(3 downto 0) );

end entity;


architecture gideon of char_generator_slave is
    signal pointer          : unsigned(g_screen_size-1 downto 0) := (others => '0');
    signal char_x           : unsigned(6 downto 0)  := (others => '0');
    signal char_y           : unsigned(3 downto 0)  := (others => '0');
    signal char_y_d         : unsigned(3 downto 0)  := (others => '0');
    signal pixel_count      : unsigned(2 downto 0)  := (others => '0');
    signal remaining_lines  : unsigned(5 downto 0)  := (others => '0');

    type t_state is (idle, active_line, draw);
    signal state            : t_state;

    -- pipeline
    signal color_data_d     : std_logic_vector(7 downto 0);
    signal active_d1        : std_logic;
    signal pixel_sel_d1     : unsigned(2 downto 0);
    signal active_d2        : std_logic;
    signal pixel_sel_d2     : unsigned(2 downto 0);
    
begin
    process(clock)
    begin
        if rising_edge(clock) then
            active_d1 <= '0';
            char_y_d  <= char_y;
            color_data_d <= color_data;
            
            case state is
            when idle =>
                pointer <= control.pointer(pointer'range);
                char_y  <= (others => '0');
                remaining_lines <= control.active_lines;
                if v_count = control.y_on then
                    state <= active_line;
                end if;
                
            when active_line =>
                char_x <= (others => '0');
                pixel_count <= control.char_width;
                
                if remaining_lines = 0 then
                    state <= idle;
                elsif h_count = control.x_on then
                    state <= draw;
                end if;
            
            when draw =>
                if pixel_count = 1 then
                    pixel_count <= control.char_width;
                    char_x <= char_x + 1;
                    if char_x = control.chars_per_line-1 then
                        state <= active_line;
                        char_x <= (others => '0');
                        if char_y = control.char_height-1 then
                            pointer <= pointer + control.chars_per_line;
                            char_y <= (others => '0');
                            remaining_lines <= remaining_lines - 1;
                        else                        
                            char_y <= char_y + 1;
                        end if;
                    end if;
                else
                    pixel_count <= pixel_count - 1;
                end if;
                active_d1    <= '1';

            when others =>
                null;
            end case;

            -- pipeline forwards
            pixel_sel_d1 <= pixel_count - 1;
            pixel_sel_d2 <= pixel_sel_d1;
            active_d2    <= active_d1;

            -- pixel output
            pixel_active <= active_d2;
			if active_d2='1' then
				if char_data(to_integer(pixel_sel_d2))='1' then
		            pixel_data <= unsigned(color_data_d(3 downto 0));
                    if color_data_d(3 downto 0) = control.transparent then
                        pixel_opaque <= '0';
                    else
                        pixel_opaque <= '1';
                    end if;
		        else
		        	pixel_data <= unsigned(color_data_d(7 downto 4));
                    if color_data_d(7 downto 4) = control.transparent then
                        pixel_opaque <= '0';
                    else
                        pixel_opaque <= '1';
                    end if;
		        end if;
			else
				pixel_data   <= (others => '0');
                pixel_opaque <= '0';
		    end if;           

            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;
    
    screen_addr <= pointer + char_x;
    char_addr   <= unsigned(screen_data) & char_y_d(2 downto 0) when char_y_d(3)='0' else 
                   screen_data(7) & "0000000000";
    
end architecture;
