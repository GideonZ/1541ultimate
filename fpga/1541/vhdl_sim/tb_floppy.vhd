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
use work.flat_memory_model.all;

entity tb_floppy is
end tb_floppy;

architecture tb of tb_floppy is
    signal clock           : std_logic := '0';
    signal clock_en        : std_logic;  -- combi clk/cke that yields 4 MHz; eg. 16/4
    signal reset           : std_logic;

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
    
---
    signal mem_clock       : std_logic := '0';
    signal mem_req         : std_logic;
    signal mem_rwn         : std_logic;
    signal mem_rack        : std_logic := '0';
    signal mem_dack        : std_logic := '0';
    signal mem_addr        : std_logic_vector(31 downto 0) := (others => '0');
    signal mem_wdata       : std_logic_vector(7 downto 0);
    signal mem_rdata       : std_logic_vector(7 downto 0) := X"00";
    
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
	mem_clock <= not mem_clock after 100 ns; -- 5 Mhz
	
    process
    begin
        wait until clock='1';
        clock_en <= '0';
        wait until clock='1';
        wait until clock='1';
        wait until clock='1';
        clock_en <= '1';
    end process;
            
	mut: entity work.floppy
	port map (
	    drv_clock       => clock,
	    drv_clock_en    => clock_en,  -- combi clk/cke that yields 4 MHz; eg. 16/4
	    drv_reset       => reset,
	    
        motor_on        => motor_on,
        sync            => sync,
        mode            => mode,
        write_prot_n    => write_prot_n,
        step            => step,
        byte_ready      => byte_ready,
        soe             => soe,
        rate_ctrl       => rate_ctrl,
        
		write_data	    => write_data,
        read_data       => read_data,
        
        track           => track,

	    mem_clock       => mem_clock,
	    mem_reset       => reset,
	    
	    mem_req         => mem_req,
	    mem_rwn         => mem_rwn,
	    mem_rack        => mem_rack,
	    mem_dack        => mem_dack,
	    mem_addr        => mem_addr(19 downto 0), -- 1MB
	    
	    mem_wdata       => mem_wdata,
	    mem_rdata       => mem_rdata );

    test: process
    begin
        motor_on     <= '1';
        mode         <= '1';
        write_prot_n <= '1';
        soe          <= '1';
        wait for 700 us;
        wait until byte_ready='0';
		wait for 10 us;
        mode <= '0'; -- switch to write
        for i in 0 to 255 loop
	        wait until byte_ready='0';
			wait for 10 us;
        	write_data <= write_data + 1;
		end loop;
		wait until byte_ready='0';
		mode <= '1';
        wait;
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
        wait;
    end process;

    rate_ctrl <= conv_std_logic_vector(rate_table(conv_integer(track(6 downto 1))), 2);    
    
	memory: process
		variable h : h_mem_object;
	begin
		register_mem_model("my memory", h);
		for i in 0 to 15 loop
			write_memory_32(h, conv_std_logic_vector(i*4, 32), X"FFFFFFFF");
		end loop; 
		while true loop
			wait until mem_clock='1';
			if mem_req='1' then
				wait until mem_clock='1';
				wait until mem_clock='1';
				wait until mem_clock='1';
				if mem_rwn='0' then -- write
					write_memory_8(h, mem_addr, mem_wdata);
					mem_ack <= '1';
					wait until mem_clock='1';
					mem_ack <= '0';
				else
					mem_rdata <= read_memory_8(h, mem_addr);
					mem_ack <= '1';
					wait until mem_clock='1';
					mem_ack <= '0';
					mem_rdata <= X"00";
				end if;
			end if;
		end loop;			
	end process;
end tb;