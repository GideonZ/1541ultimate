-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Floppy Emulator
-------------------------------------------------------------------------------
-- File       : floppy.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements the emulator of the floppy drive.
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.mem_bus_pkg.all;
    
entity floppy is
generic (
    g_big_endian    : boolean;
    g_tag           : std_logic_vector(7 downto 0) := X"01" );
port (
    sys_clock       : in  std_logic;
    drv_reset       : in  std_logic;
    tick_16MHz      : in  std_logic;
        
    -- signals from MOS 6522 VIA
    motor_on        : in  std_logic;
    mode            : in  std_logic;
    write_prot_n    : in  std_logic;
    side            : in  std_logic := '0';
    step            : in  std_logic_vector(1 downto 0);
    rate_ctrl       : in  std_logic_vector(1 downto 0);
    byte_ready      : out std_logic;
    sync            : out std_logic;
    
    read_data       : out std_logic_vector(7 downto 0);
    write_data      : in  std_logic_vector(7 downto 0);
    
    track           : out std_logic_vector(6 downto 0);
    track_is_0      : out std_logic;
    
    -- signals connected to sd-cpu
    cpu_write       : in  std_logic;
    cpu_ram_en      : in  std_logic;
    cpu_addr        : in  std_logic_vector(10 downto 0);
    cpu_wdata       : in  std_logic_vector(7 downto 0);
    cpu_rdata       : out std_logic_vector(7 downto 0);

---
    floppy_inserted : in  std_logic := '0';
    do_head_bang    : out std_logic;
    do_track_out    : out std_logic;
    do_track_in     : out std_logic;
    en_hum          : out std_logic;
    en_slip         : out std_logic;
---
    mem_req         : out t_mem_req;
    mem_resp        : in  t_mem_resp );

end floppy;

architecture structural of floppy is
    signal mem_rdata        : std_logic_vector(7 downto 0);
    signal do_read          : std_logic;
    signal do_write         : std_logic;
    signal do_advance       : std_logic;
    signal track_start      : std_logic_vector(25 downto 0);
    signal max_offset       : std_logic_vector(13 downto 0);
    signal track_i          : std_logic_vector(6 downto 0);
    signal bit_time         : unsigned(9 downto 0);
begin
    en_hum  <= motor_on and not floppy_inserted;
    en_slip <= motor_on and floppy_inserted;
    track   <= track_i;

    stream: entity work.floppy_stream
    port map (
        clock           => sys_clock,
        reset           => drv_reset,
        tick_16MHz      => tick_16MHz,
        
		mem_rdata		=> mem_rdata,
        do_read         => do_read,
    	do_write	    => do_write,
        do_advance      => do_advance,

        floppy_inserted => floppy_inserted,

        track           => track_i,
        track_is_0      => track_is_0,

        do_head_bang    => do_head_bang,
        do_track_in     => do_track_in,
        do_track_out    => do_track_out,
        
        motor_on        => motor_on,
        sync            => sync,
        mode            => mode,
        write_prot_n    => write_prot_n,
        step            => step,
        byte_ready      => byte_ready,
        rate_ctrl       => rate_ctrl,
        bit_time        => bit_time,
        
        read_data       => read_data );

    params: entity work.floppy_param_mem
    generic map (
        g_big_endian    => g_big_endian )
    port map (
        clock       => sys_clock,
        reset       => drv_reset,
        
        cpu_write   => cpu_write,
        cpu_ram_en  => cpu_ram_en,
        cpu_addr    => cpu_addr,
        cpu_wdata   => cpu_wdata,
        cpu_rdata   => cpu_rdata,
    
        track       => track_i,
        side        => side,
        track_start => track_start,
        max_offset  => max_offset,
        bit_time    => bit_time );

    fetch_wb: entity work.floppy_mem
    generic map (
        g_tag           => g_tag )
    port map (
        clock           => sys_clock,
        reset           => drv_reset,
        
		drv_rdata		=> mem_rdata,
        drv_wdata       => write_data,
        do_read         => do_read,
        do_write        => do_write,
        do_advance      => do_advance,

        track_start     => track_start,
        max_offset      => max_offset,

        mem_req         => mem_req,
        mem_resp        => mem_resp );
		
end structural;

