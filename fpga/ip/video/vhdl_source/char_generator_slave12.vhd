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
use work.char_generator_pkg.all;

entity char_generator_slave12 is
generic (
	g_screen_size	: natural := 11 );
port (
    clock           : in  std_logic;
    clock_en        : in  std_logic := '1';
    reset           : in  std_logic;
        
    data_enable     : in  std_logic;
    h_count         : in  unsigned(11 downto 0);
    v_count         : in  unsigned(11 downto 0);
    
    control         : in  t_chargen_control;
    overlay_ena     : in  std_logic;
    
    screen_addr     : out unsigned(g_screen_size-1 downto 0);
    screen_data     : in  std_logic_vector(7 downto 0);
	color_data		: in  std_logic_vector(7 downto 0);
	
    char_addr_8     : out unsigned(10 downto 0);
    char_data_8     : in  std_logic_vector(7 downto 0);
    char_addr_12    : out unsigned(9 downto 0);
    char_data_12    : in  std_logic_vector(35 downto 0);

    pixel_active    : out std_logic;
    pixel_opaque    : out std_logic;
    pixel_data      : out unsigned(3 downto 0) );

end entity;


architecture gideon of char_generator_slave12 is
    signal pointer          : unsigned(g_screen_size-1 downto 0) := (others => '0');
    signal char_x           : unsigned(6 downto 0)  := (others => '0');
    signal char_y           : unsigned(4 downto 0)  := (others => '0');
    signal char_y_d         : unsigned(4 downto 0)  := (others => '0');
    signal pixel_count      : unsigned(3 downto 0)  := (others => '0');
    signal remaining_lines  : unsigned(5 downto 0)  := (others => '0');

    type t_state is (idle, active_line, draw);
    signal state            : t_state;

    -- pipeline
    signal color_data_d     : std_logic_vector(7 downto 0);
    signal active_d1        : std_logic;
    signal pixel_sel_d1     : unsigned(3 downto 0);
    signal active_d2        : std_logic;
    signal pixel_sel_d2     : unsigned(3 downto 0);
    signal reverse          : std_logic;
begin
    process(clock)
        variable v_char_data : std_logic_vector(11 downto 0);
    begin
        if rising_edge(clock) then
            if clock_en = '1' then
                active_d1 <= '0';
                char_y_d  <= char_y;
                color_data_d <= color_data;
                
                case state is
                when idle =>
                    pointer <= control.pointer(pointer'range);
                    char_y  <= (others => '0');
                    remaining_lines <= control.active_lines;
                    if (v_count = control.y_on) and (overlay_ena = '1') and (data_enable = '1') then
                        state <= active_line;
                    end if;
                    
                when active_line =>
                    char_x <= (others => '0');
                    pixel_count <= control.char_width;
                    
                    if remaining_lines = 0 then
                        state <= idle;
                    elsif h_count = control.x_on and (data_enable = '1') then
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
                            -- count 0, 1, 2, 4, 5, 6, 8, 9 ... for char_heights of at least 16 (use other ROM)
                            elsif char_y(1 downto 0) = "10" and control.char_height(4) = '1' then 
                                char_y <= char_y + 2;
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
                    if control.char_height(4) = '1' then
                        if char_y(1 downto 0) = "00" then
                            v_char_data := char_data_12(11 downto 0);
                        elsif char_y(1 downto 0) = "01" then
                            v_char_data := char_data_12(23 downto 12);
                        else
                            v_char_data := char_data_12(35 downto 24);
                        end if;
                    else
                        v_char_data := X"0" & char_data_8;
                        if char_data_8 /= X"18" and char_y(3) = '1' and control.stretch_y = '0' then -- allow a vertical line to continue
                            v_char_data := X"000"; -- otherwise insert blank
                        end if;
                    end if;

                    if v_char_data(to_integer(pixel_sel_d2)) = not(reverse) then
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
            end if;

            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;
    
    screen_addr <= pointer + char_x;

    char_addr_8  <= '0' & unsigned(screen_data(6 downto 0)) & char_y_d(3 downto 1) when control.stretch_y = '1' else 
                    '0' & unsigned(screen_data(6 downto 0)) & char_y_d(2 downto 0) when char_y_d(3)='0' else 
                    '0' & unsigned(screen_data(6 downto 0)) & "111"; -- keep repeating the last line
    char_addr_12 <= unsigned(screen_data(6 downto 0)) & char_y_d(4 downto 2);
    reverse      <= screen_data(7) when rising_edge(clock);

end architecture;
