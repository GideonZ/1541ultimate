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
    g_support_calc  : boolean := false;
    g_tag           : std_logic_vector(7 downto 0) := X"01" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    drv_wdata       : in  std_logic_vector(7 downto 0);
	drv_rdata		: out std_logic_vector(7 downto 0);
    do_read         : in  std_logic;
    do_write        : in  std_logic;
    do_advance      : in  std_logic;
    
    bit_time        : in  unsigned(9 downto 0);
    track_start     : in  std_logic_vector(25 downto 0);
    max_offset      : in  std_logic_vector(13 downto 0);
    
    mem_req         : out t_mem_req;
    mem_resp        : in  t_mem_resp );
    
end floppy_mem;

architecture gideon of floppy_mem is
    type t_state is (idle, reading, writing, check_track, mult, div);
    signal state        : t_state;

    signal mem_rack     : std_logic;
    signal mem_dack     : std_logic;
    signal bit_time_d   : unsigned(bit_time'range);
    signal offset_count : unsigned(13 downto 0);
    signal shifter      : unsigned(13 downto 0);
    signal multiplicant : unsigned(23 downto 0);
    signal accu         : unsigned(23 downto 0);
    signal count        : integer range 0 to 15;
begin
    mem_rack <= '1' when mem_resp.rack_tag = g_tag else '0';
    mem_dack <= '1' when mem_resp.dack_tag = g_tag else '0';

    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                if do_read='1' then
                    offset_count <= offset_count + 1;
                    state <= reading;
                    mem_req.read_writen <= '1';
                    mem_req.request <= '1';
                elsif do_write='1' then
                    offset_count <= offset_count + 1;
                    state <= writing;
                    mem_req.read_writen <= '0';
                    mem_req.request <= '1';
                elsif do_advance='1' then
                    offset_count <= offset_count + 1;
                    state <= check_track;
                end if;
                mem_req.data <= drv_wdata;
                mem_req.address  <= unsigned(track_start) + offset_count;

            when reading =>
                if mem_rack='1' then
                    mem_req.request <= '0';
                end if;
				if mem_dack='1' then
					drv_rdata    <= mem_resp.data;
					state        <= check_track;
				end if;
                
            when writing =>
                if mem_rack='1' then
                    mem_req.request <= '0';
					drv_rdata    <= mem_resp.data;
					state        <= check_track;
				end if;
            
            when check_track =>
                bit_time_d <= bit_time;
                if bit_time /= bit_time_d and g_support_calc then
                    accu         <= (others => '0');
                    multiplicant <= "0000000000" & offset_count;
                    shifter      <= "0000" & bit_time_d;
                    state        <= mult;
                    count        <= bit_time'length - 1;
                else
                    state        <= idle;
                end if;

            when mult =>
                if shifter(0) = '1' then
                    accu <= accu + multiplicant;
                end if;

                shifter <= "0" & shifter(shifter'high downto 1);
                if count = 0 then
                    count <= offset_count'length + 1;
                    state <= div;
                    multiplicant <= bit_time & "00000000000000";
                else
                    multiplicant <= multiplicant(multiplicant'high-1 downto 0) & "0";
                    count <= count - 1;
                end if;

            when div =>
                shifter <= shifter(shifter'high-1 downto 0) & '0';
                if accu >= multiplicant then
                    shifter(0) <= '1';
                    accu <= accu - multiplicant;
                end if;
                multiplicant <= "0" & multiplicant(multiplicant'high downto 1);
                if count = 0 then
                    offset_count <= shifter;
                    state <= idle;
                else
                    count <= count - 1;
                end if;

            when others =>
                null;
            end case;

            if offset_count >= unsigned(max_offset) then
                offset_count <= (others => '0');
            end if;

            if reset='1' then
                count        <= 0;
                offset_count <= to_unsigned(5000, offset_count'length); --(others => '0');
                state        <= idle;
                mem_req      <= c_mem_req_init;
                mem_req.tag  <= g_tag;
                drv_rdata    <= X"FF";
                bit_time_d   <= bit_time;
            end if;
        end if;
    end process;
    
end gideon;   
    