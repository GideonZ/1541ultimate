-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Floppy Emulator
-------------------------------------------------------------------------------
-- File       : tb_floppy_stream.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements the emulator of the floppy drive.
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.tl_flat_memory_model_pkg.all;

entity tb_floppy_stream is

end tb_floppy_stream;

architecture tb of tb_floppy_stream is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;

    signal mem_rdata       : std_logic_vector(7 downto 0) := X"01";

    signal motor_on        : std_logic;
    signal mode            : std_logic;
    signal write_prot_n    : std_logic;
    signal step            : unsigned(1 downto 0) := "00";
    signal soe             : std_logic;
    signal bit_time        : unsigned(8 downto 0) := to_unsigned(335, 9);
    signal track           : std_logic_vector(6 downto 0);
    
    signal byte_ready      : std_logic;
    signal sync            : std_logic;
    signal read_data       : std_logic_vector(7 downto 0);
    signal write_data      : std_logic_vector(7 downto 0) := X"55";
    signal read_latched    : std_logic_vector(7 downto 0) := (others => '-');
    
    signal do_read          : std_logic;
    signal do_write         : std_logic;
    signal do_advance       : std_logic;
    
    type t_buffer_array is array (natural range <>) of std_logic_vector(7 downto 0);
    shared variable my_buffer : t_buffer_array(0 to 15) := (others => X"FF");
    
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 400 ns;
            
    mut: entity work.floppy_stream
    port map (
        clock           => clock,
        reset           => reset,
        
        mem_rdata       => mem_rdata,
        floppy_inserted => '1',
        
        do_read         => do_read,
        do_write        => do_write,
        do_advance      => do_advance,

        track           => track,
        
        motor_on        => motor_on,
        sync            => sync,
        mode            => mode,
        write_prot_n    => write_prot_n,
        step            => std_logic_vector(step),
        byte_ready      => byte_ready,
        soe             => soe,
        bit_time        => bit_time,
                
        read_data       => read_data );

    test: process
    begin
        motor_on     <= '1';
        mode         <= '1';
        write_prot_n <= '1';
        soe          <= '1';
        --wait for 700 us;
        --mode <= '0'; -- switch to write
        
        wait;
    end process;
    
    process(byte_ready)
    begin
        if falling_edge(byte_ready) then
            read_latched <= read_data;
        end if;
    end process;

    memory: process(clock)
        variable h : h_mem_object;
        variable h_initialized : boolean := false;
        variable address : unsigned(31 downto 0) := X"000002AE";
    begin
        if rising_edge(clock) then
            if not h_initialized then
                register_mem_model("my_memory", "my memory", h);
                load_memory("../../../g64/720_s0.g64", 1, X"00000000");
                h_initialized := true; 
                write_memory_32(h, X"00000400", X"00000000");
                write_memory_32(h, X"00000404", X"00000000");
                write_memory_32(h, X"00000408", X"00000000");
                write_memory_32(h, X"0000040C", X"00000000");
                write_memory_32(h, X"00000410", X"00000000");
                write_memory_32(h, X"00000414", X"00000000");
                write_memory_32(h, X"00000418", X"00000000");
                write_memory_32(h, X"0000041C", X"00000000");
                write_memory_32(h, X"00000420", X"00000000");
            end if;
            if do_write = '1' then
                write_memory_8(h, std_logic_vector(address), write_data);
                address := address + 1;
            elsif do_read = '1' then
                mem_rdata <= read_memory_8(h, std_logic_vector(address));
                address := address + 1;
            elsif do_advance = '1' then
                address := address + 1;
            end if;
        end if;
    end process;
    
    move: process
    begin
        wait for 2 us;
        for i in 0 to 100 loop
            step <= step + 1;
            wait for 2 us;
        end loop;
        wait for 2 us;
        for i in 0 to 100 loop
            step <= step - 1;
            wait for 2 us;
        end loop;
    end process;
    
end tb;
