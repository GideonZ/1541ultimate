-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Character Generator
-------------------------------------------------------------------------------
-- File       : char_generator_peripheral.vhd
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

entity char_generator_peripheral is
generic (
    g_color_ram     : boolean := false;
	g_screen_size	: natural := 11 );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    overlay_on      : out std_logic;

    keyb_row        : in    std_logic_vector(7 downto 0) := (others => '0');
    keyb_col        : inout std_logic_vector(7 downto 0) := (others => '0');
    pix_clock       : in  std_logic;
    pix_reset       : in  std_logic;
    h_count         : in  unsigned(11 downto 0);
    v_count         : in  unsigned(11 downto 0);
    pixel_active    : out std_logic;
    pixel_opaque    : out std_logic;
    pixel_data      : out unsigned(3 downto 0) );
    
end entity;

architecture structural of char_generator_peripheral is

    signal control         : t_chargen_control;
    signal screen_addr     : unsigned(g_screen_size-1 downto 0);
    signal screen_data     : std_logic_vector(7 downto 0);
    signal color_data      : std_logic_vector(7 downto 0) := X"0F";
    signal char_addr       : unsigned(10 downto 0);
    signal char_data       : std_logic_vector(7 downto 0);

	signal io_req_regs		: t_io_req  := c_io_req_init;
	signal io_req_scr		: t_io_req  := c_io_req_init;
	signal io_req_color		: t_io_req  := c_io_req_init;
	signal io_resp_regs		: t_io_resp := c_io_resp_init;
	signal io_resp_scr		: t_io_resp := c_io_resp_init;
	signal io_resp_color	: t_io_resp := c_io_resp_init;

begin
    overlay_on <= control.overlay_on;
    
	-- allocate 32K for character memory
	-- allocate 32K for color memory
	-- allocate another space for registers
	
    i_split: entity work.io_bus_splitter
    generic map (
        g_range_lo  => g_screen_size,
        g_range_hi  => g_screen_size+1,
        g_ports     => 3 )
    port map (
        clock    => clock,
        
        req      => io_req,
        resp     => io_resp,
    
        reqs(0)  => io_req_regs,  -- size=15: xxx0000, size=11: xxx0000
        reqs(1)  => io_req_scr,   -- size=15: xxx8000, size=11: xxx0800
        reqs(2)  => io_req_color, -- size=15: xx10000, size=11: xxx1000
        
        resps(0) => io_resp_regs,
        resps(1) => io_resp_scr,
        resps(2) => io_resp_color );

    i_regs: entity work.char_generator_regs
    port map (
        clock           => clock,
        reset           => reset,
        
        io_req          => io_req_regs,
        io_resp         => io_resp_regs,
        
        keyb_row        => keyb_row,
        keyb_col        => keyb_col,

        control         => control );

    i_timing: entity work.char_generator_slave
	generic map (
		g_screen_size	=> g_screen_size )
    port map (
        clock           => pix_clock,
        reset           => pix_reset,
                                       
        h_count         => h_count,
        v_count         => v_count,
                                       
        control         => control,
                                       
        screen_addr     => screen_addr,
        screen_data     => screen_data,
		color_data		=> color_data,
		                                       
        char_addr       => char_addr,
        char_data       => char_data,
                                       
        pixel_active    => pixel_active,
        pixel_opaque    => pixel_opaque,
        pixel_data      => pixel_data );

    i_rom: entity work.char_generator_rom
    port map (
        clock       => pix_clock,
        enable      => '1',
        address     => char_addr,
        data        => char_data );

--    process(pix_clock)
--    begin
--        if rising_edge(pix_clock) then
--            screen_data <= std_logic_vector(screen_addr(7 downto 0));
--            color_data  <= std_logic_vector(screen_addr(10 downto 3));
--        end if;
--    end process;

	i_screen: entity work.dpram_io
    generic map (
        g_depth_bits            => g_screen_size, -- max = 15 for 1920x1080
        g_default               => X"20",
        g_storage               => "block" )
    port map (
        a_clock                 => pix_clock,
        a_address               => screen_addr,
        a_rdata                 => screen_Data,

        b_clock                 => clock,
		b_req					=> io_req_scr,
		b_resp					=> io_resp_scr );

    r_color: if g_color_ram generate
    	i_color: entity work.dpram_io
        generic map (
            g_depth_bits            => g_screen_size, -- max = 15 for 1920x1080
            g_default               => X"0F",
            g_storage               => "block" )
        port map (
            a_clock                 => pix_clock,
            a_address               => screen_addr,
            a_rdata                 => color_data,
    
            b_clock                 => clock,
    		b_req					=> io_req_color,
    		b_resp					=> io_resp_color );
    end generate;
end structural;
