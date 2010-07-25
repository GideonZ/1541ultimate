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
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

library work;
use work.floppy_emu_pkg.all;

entity tb_floppy_stream is
end tb_floppy_stream;

architecture tb of tb_floppy_stream is
    signal clock           : std_logic := '0';
    signal clock_en        : std_logic;  -- combi clk/cke that yields 4 MHz; eg. 16/4
    signal reset           : std_logic;

    signal drv_rdata       : std_logic_vector(7 downto 0) := X"01";

    signal motor_on        : std_logic;
    signal mode            : std_logic;
    signal write_prot_n    : std_logic;
    signal step            : std_logic_vector(1 downto 0) := "00";
    signal soe             : std_logic;
    signal rate_ctrl       : std_logic_vector(1 downto 0);

    signal track           : std_logic_vector(6 downto 0);
    
    signal byte_ready      : std_logic;
    signal sync            : std_logic;
    signal read_data       : std_logic_vector(7 downto 0);
    signal write_data      : std_logic_vector(7 downto 0) := X"55";
    
    signal fifo_put        : std_logic;
    signal fifo_command    : std_logic_vector(2 downto 0);
    signal fifo_parameter  : std_logic_vector(10 downto 0);

    type t_buffer_array is array (natural range <>) of std_logic_vector(7 downto 0);
    shared variable my_buffer : t_buffer_array(0 to 15) := (others => X"FF");
    
    type t_integer_array is array (natural range <>) of integer;
    constant rate_table : t_integer_array(0 to 63) := (
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        2, 2, 2, 2, 2, 2, 2,
        1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 );
begin
    clock <= not clock after 31.25 ns;
    reset <= '1', '0' after 400 ns;

    process
    begin
        wait until clock='1';
        clock_en <= '0';
        wait until clock='1';
        wait until clock='1';
        wait until clock='1';
        clock_en <= '1';
    end process;
            
    mut: entity work.floppy_stream
    port map (
        clock           => clock,
        clock_en        => clock_en,  -- combi clk/cke that yields 4 MHz; eg. 16/4
        reset           => reset,
        
        drv_rdata       => drv_rdata,
        floppy_inserted => '1',
    	write_data		=> write_data,
        
        fifo_put        => fifo_put,
        fifo_command    => fifo_command,
        fifo_parameter  => fifo_parameter,

        track           => track,
        
        motor_on        => motor_on,
        sync            => sync,
        mode            => mode,
        write_prot_n    => write_prot_n,
        step            => step,
        byte_ready      => byte_ready,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        
        read_data       => read_data );

    test: process
    begin
        motor_on     <= '1';
        mode         <= '1';
        write_prot_n <= '1';
        soe          <= '1';
        wait for 700 us;
        mode <= '0'; -- switch to write
        
        wait;
    end process;
    
    fill: process
    begin
        wait until fifo_put='1';
        wait for 10 ns;
        if fifo_command = c_cmd_next then
            drv_rdata <= drv_rdata + 1;
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
    
    rate_ctrl <= conv_std_logic_vector(rate_table(conv_integer(track(6 downto 1))), 2);    
end tb;