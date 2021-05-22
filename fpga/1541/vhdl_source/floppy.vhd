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
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;
    
entity floppy is
generic (
    g_big_endian    : boolean;
    g_tag           : std_logic_vector(7 downto 0) := X"01" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
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

    track           : out std_logic_vector(6 downto 0);
    track_is_0      : out std_logic;
    
    read_data       : out std_logic_vector(7 downto 0);
    write_data      : in  std_logic_vector(7 downto 0);
    
    -- signals connected to sd-cpu
    io_req_param    : in  t_io_req;
    io_resp_param   : out t_io_resp;
    io_req_dirty    : in  t_io_req;
    io_resp_dirty   : out t_io_resp;

    floppy_inserted : in  std_logic := '0';
    do_head_bang    : out std_logic;
    do_track_out    : out std_logic;
    do_track_in     : out std_logic;
    en_hum          : out std_logic;
    en_slip         : out std_logic;
    dirty_led_n     : out std_logic;
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
        clock           => clock,
        reset           => reset,
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
        clock       => clock,
        reset       => reset,
        
        io_req      => io_req_param,
        io_resp     => io_resp_param,
    
        track       => track_i,
        side        => side,
        track_start => track_start,
        max_offset  => max_offset,
        bit_time    => bit_time );

    fetch_wb: entity work.floppy_mem
    generic map (
        g_tag           => g_tag )
    port map (
        clock           => clock,
        reset           => reset,
        
		drv_rdata		=> mem_rdata,
        drv_wdata       => write_data,
        do_read         => do_read,
        do_write        => do_write,
        do_advance      => do_advance,

        track_start     => track_start,
        max_offset      => max_offset,

        mem_req         => mem_req,
        mem_resp        => mem_resp );
		
    b_dirty: block
        signal any_dirty  : std_logic;
        signal dirty_bits : std_logic_vector(127 downto 0) := (others => '0');
        signal wa         : integer range 0 to 127 := 0;
        signal wr, wd     : std_logic;
    begin
        process(clock)
        begin
            if rising_edge(clock) then
                wa <= to_integer(unsigned(side & track_i(6 downto 1)));
                wd <= '1';
                wr <= '0';
                if mode = '0' and motor_on='1' and floppy_inserted='1' then
                    wr <= '1';
                    any_dirty <= '1';
                end if;                

                io_resp_dirty <= c_io_resp_init;
                if io_req_dirty.read = '1' then
                    io_resp_dirty.ack <= '1';
                    io_resp_dirty.data(7) <= any_dirty;
                    io_resp_dirty.data(0) <= dirty_bits(to_integer(io_req_dirty.address(6 downto 0)));
                end if;
                if io_req_dirty.write = '1' then
                    io_resp_dirty.ack <= '1';
                    if io_req_dirty.data(7) = '1' then
                        any_dirty <= '0';
                    else
                        wa <= to_integer(io_req_dirty.address(6 downto 0));
                        wr <= '1';
                        wd <= '0';
                    end if;
                end if;
                if wr = '1' then
                    dirty_bits(wa) <= wd;
                end if;
                if reset = '1' then
                    any_dirty <= '0';
                end if;
            end if;
        end process;
        dirty_led_n <= not any_dirty;
    end block;

end structural;
