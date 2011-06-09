-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator
-------------------------------------------------------------------------------
-- File       : char_generator_timing.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Character generator
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.char_generator_pkg.all;

entity char_generator_timing is
generic (
    g_divider       : integer := 5 );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    h_sync          : in  std_logic;
    v_sync          : in  std_logic;
    
    control         : in  t_chargen_control;

    screen_addr     : out unsigned(10 downto 0);
    screen_data     : in  std_logic_vector(7 downto 0);

    char_addr       : out unsigned(10 downto 0);
    char_data       : in  std_logic_vector(7 downto 0);

    sync_n          : out std_logic;
    clock_en        : out std_logic;
    
    pixel_active    : out std_logic;
    pixel_data      : out std_logic );

end entity;


architecture gideon of char_generator_timing is
    signal clock_div        : integer range 0 to g_divider-1;
    signal clock_en_i       : std_logic;
    signal force_sync       : std_logic;
    signal x_counter        : unsigned(10 downto 0) := (others => '0');
    signal y_counter        : unsigned(8 downto 0)  := (others => '0');
    signal pointer          : unsigned(10 downto 0) := (others => '0');
    signal char_x           : unsigned(6 downto 0)  := (others => '0');
    signal char_y           : unsigned(3 downto 0)  := (others => '0');
    signal char_y_d         : unsigned(3 downto 0)  := (others => '0');
    signal pixel_count      : unsigned(2 downto 0)  := (others => '0');
    signal remaining_lines  : unsigned(4 downto 0)  := (others => '0');

    signal h_sync_c         : std_logic;
    signal h_sync_d         : std_logic;
    signal v_sync_c         : std_logic;

    type t_state is (idle, active_line, draw);
    signal state            : t_state;

    type t_line_type is (normal, half, serration);
    signal line_type        : t_line_type;
    
    -- pipeline
    signal active_d1        : std_logic;
    signal pixel_sel_d1     : unsigned(2 downto 0);
    signal active_d2        : std_logic;
    signal pixel_sel_d2     : unsigned(2 downto 0);
    
begin
    clock_en <= clock_en_i;
    
    process(clock)
    begin
        if rising_edge(clock) then
            h_sync_c <= h_sync;
            h_sync_d <= h_sync_c;
            v_sync_c <= v_sync;
            
--            force_sync <= '0';
--            if control.perform_sync='1' and v_sync_c='1' and h_sync_d='0' and h_sync_c='1' then 
--                y_counter <= (others => '0');
--                x_counter <= (others => '0');
--            end if;

--            -- x/y counters, zonder gehinderd door enige sync-kennis
--            if clock_en_i = '1' then
--                if x_counter = control.clocks_per_line-1 then
--                    x_counter <= (others => '0');
--                    if y_counter = 311 then
--                        y_counter <= (others => '0');
--                        force_sync <= '1';
--                    else
--                        y_counter <= y_counter + 1;
--                    end if;
--                else
--                    x_counter <= x_counter + 1;
--                end if;
--            end if;

            if h_sync_c='1' and h_sync_d='0' then -- rising_edge
                x_counter <= (others => '0');
                if v_sync_c='1' then
                    y_counter <= (others => '0');
                else
                    y_counter <= y_counter + 1;
                end if;
                clock_en_i <= '0';
                clock_div <= g_divider-1;
            elsif clock_div = 0 then
                x_counter <= x_counter + 1;
                clock_en_i <= '1';
                clock_div <= g_divider-1;
            else
                clock_en_i <= '0';
                clock_div <= clock_div - 1;
            end if;

        end if;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            if clock_en_i='1' then
                line_type <= normal;
                if (y_counter < 3) or (y_counter > 305) then
                    line_type <= half;
                end if;
                if (y_counter > 308) then
                    line_type <= serration;
                end if;
    
                if x_counter=0 then
                    sync_n <= '0';
                else
                    case line_type is
                    when normal =>
                        if x_counter = 64 then
                            sync_n <= '1';
                        end if;
                    when half =>
                        if x_counter = 32 then
                            sync_n <= '1';
                        elsif x_counter = 448 then
                            sync_n <= '0';
                        elsif x_counter = 480 then
                            sync_n <= '1';
                        end if;
                    when serration =>
                        if x_counter = 416 then
                            sync_n <= '1';
                        elsif x_counter = 448 then
                            sync_n <= '0';
                        elsif x_counter = 864 then
                            sync_n <= '1';
                        end if;
                    when others =>
                        null;
                    end case;
                end if;
            end if;
        end if;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            if clock_en_i='1' then
                active_d1 <= '0';
                char_y_d  <= char_y;
                
                case state is
                when idle =>
                    pointer <= control.pointer;
                    char_y  <= (others => '0');
                    remaining_lines <= control.active_lines;
                    if y_counter = control.y_on then
                        state <= active_line;
                    end if;
                    
                when active_line =>
                    char_x <= (others => '0');
                    pixel_count <= control.char_width;
                    
                    if remaining_lines = 0 then
                        state <= idle;
                    elsif x_counter = control.x_on then
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
                pixel_data   <= active_d2 and char_data(to_integer(pixel_sel_d2));            
    
                if force_sync='1' then
                    state <= idle;
                end if;
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
