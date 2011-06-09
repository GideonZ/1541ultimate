-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator
-------------------------------------------------------------------------------
-- File       : char_generator.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Character generator top
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.char_generator_pkg.all;

entity char_generator is
generic (
    g_divider       : integer := 5 );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
        
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;

    h_sync          : in  std_logic;
    v_sync          : in  std_logic;
    
    pixel_active    : out std_logic;
    pixel_data      : out std_logic;
    
    sync_out_n      : out std_logic );

end entity;

architecture structural of char_generator is

    signal clock_en        : std_logic;
    signal control         : t_chargen_control;
    signal screen_addr     : unsigned(10 downto 0);
    signal screen_data     : std_logic_vector(7 downto 0);
    signal char_addr       : unsigned(10 downto 0);
    signal char_data       : std_logic_vector(7 downto 0);

begin

    i_regs: entity work.char_generator_regs
    port map (
        clock           => clock,
        reset           => reset,
        
        io_req          => io_req,
        io_resp         => io_resp,
        
        control         => control );

    i_timing: entity work.char_generator_timing
    generic map (
        g_divider       => g_divider )
    port map (
        clock           => clock,
        reset           => reset,
                                       
        h_sync          => h_sync,
        v_sync          => v_sync,
                                       
        control         => control,
                                       
        screen_addr     => screen_addr,
        screen_data     => screen_data,
                                       
        char_addr       => char_addr,
        char_data       => char_data,
                                       
        pixel_active    => pixel_active,
        pixel_data      => pixel_data,
        
        clock_en        => clock_en,
        sync_n          => sync_out_n );

    i_rom: entity work.char_generator_rom
    port map (
        clock       => clock,
        enable      => clock_en,
        address     => char_addr,
        data        => char_data );

    process(clock)
    begin
        if rising_edge(clock) then
            if clock_en='1' then
                screen_data <= std_logic_vector(screen_addr(7 downto 0));
            end if;
        end if;
    end process;

--    i_ram: entity work.char_generator_rom
--    port map (
--        clock       => clock,
--        enable      => clock_en,
--        address     => screen_addr,
--        data        => screen_data );

end structural;
