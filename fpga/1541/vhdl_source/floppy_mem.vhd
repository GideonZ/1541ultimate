-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Floppy
-------------------------------------------------------------------------------
-- File       : floppy_mem.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements the interface to the buffer, for the 
--              floppy model
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity floppy_mem is
generic (
    g_tag           : std_logic_vector(7 downto 0) := X"01" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    drv_wdata       : in  std_logic_vector(7 downto 0);
	drv_rdata		: out std_logic_vector(7 downto 0);
    do_read         : in  std_logic;
    do_write        : in  std_logic;
    do_advance      : in  std_logic;
    
    track_start     : in  std_logic_vector(25 downto 0);
    max_offset      : in  std_logic_vector(13 downto 0);
    
    mem_req         : out t_mem_req;
    mem_resp        : in  t_mem_resp );
    
end floppy_mem;

architecture gideon of floppy_mem is
    type t_state is (idle, reading, writing);
    signal state        : t_state;

    signal mem_rack     : std_logic;
    signal mem_dack     : std_logic;
begin
    mem_rack <= '1' when mem_resp.rack_tag = g_tag else '0';
    mem_dack <= '1' when mem_resp.dack_tag = g_tag else '0';

    process(clock)
        variable offset_count : unsigned(13 downto 0);

        procedure advance is
        begin
            if offset_count >= unsigned(max_offset) then
                offset_count := (others => '0');
            else
                offset_count := offset_count + 1;
            end if;
        end procedure;
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                if do_read='1' then
                    advance;
                    state <= reading;
                    mem_req.read_writen <= '1';
                    mem_req.request <= '1';
                elsif do_write='1' then
                    advance;
                    state <= writing;
                    mem_req.read_writen <= '0';
                    mem_req.request <= '1';
                elsif do_advance='1' then
                    advance;
                end if;
                mem_req.data <= drv_wdata;
                mem_req.address  <= unsigned(track_start) + offset_count;

            when reading =>
                if mem_rack='1' then
                    mem_req.request <= '0';
                end if;
				if mem_dack='1' then
					drv_rdata    <= mem_resp.data;
					state        <= idle;
				end if;
                
            when writing =>
                if mem_rack='1' then
                    mem_req.request <= '0';
					drv_rdata    <= mem_resp.data;
					state        <= idle;
				end if;
            
            when others =>
                null;
            end case;
            
            if reset='1' then
                offset_count := (others => '0');
                state        <= idle;
                mem_req      <= c_mem_req_init;
                mem_req.tag  <= g_tag;
                drv_rdata    <= X"FF";
            end if;
        end if;
    end process;
    
end gideon;   
    