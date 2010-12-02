-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Floppy Emulator
-------------------------------------------------------------------------------
-- File       : floppy_stream.vhd
-- Author     : Gideon Zweijtzer  <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module implements the emulator of the floppy drive.
-------------------------------------------------------------------------------
 
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
--use ieee.std_logic_arith.all;
--use ieee.std_logic_unsigned.all;

--library work;
--use work.floppy_emu_pkg.all;

entity floppy_stream is
port (
    clock           : in  std_logic;
    clock_en        : in  std_logic;  -- combi clk/cke that yields 4 MHz; eg. 16/4
    reset           : in  std_logic;
    
    -- data from memory
    drv_rdata       : in  std_logic_vector(7 downto 0);
    do_read         : out std_logic;
	do_write	    : out std_logic;
    do_advance      : out std_logic;
    
    -- info about the head
    track           : out std_logic_vector(6 downto 0);
    track_is_0      : out std_logic;
    do_head_bang    : out std_logic;
    do_track_out    : out std_logic;
    do_track_in     : out std_logic;

    -- control i/o
    floppy_inserted : in  std_logic;
    motor_on        : in  std_logic;
    sync            : out std_logic;
    mode            : in  std_logic;
    write_prot_n    : in  std_logic;
    step            : in  std_logic_vector(1 downto 0);
    byte_ready      : out std_logic;
    soe             : in  std_logic;
    rate_ctrl       : in  std_logic_vector(1 downto 0);
    
    -- data to drive CPU
    read_data       : out std_logic_vector(7 downto 0) );
    
end floppy_stream;    

architecture gideon of floppy_stream is
    signal bit_div  : unsigned(3 downto 0);
    signal bit_tick : std_logic;

    signal mem_bit_cnt : unsigned(2 downto 0);
    signal rd_bit_cnt  : unsigned(2 downto 0) := "000";
    signal mem_shift   : std_logic_vector(7 downto 0);
    signal rd_shift    : std_logic_vector(9 downto 0) := (others => '0');
    signal sync_i      : std_logic;
    signal byte_rdy_i  : std_logic;
    alias  mem_rd_bit  : std_logic is mem_shift(7);

    signal track_c     : unsigned(6 downto 2);
    signal track_i     : unsigned(6 downto 0);
    signal up, down    : std_logic;
    signal step_d      : std_logic_vector(1 downto 0);
    signal step_dd0    : std_logic;
    signal mode_d      : std_logic;
    signal write_delay : integer range 0 to 3;
begin
    p_clock_div: process(clock)
    begin
        if rising_edge(clock) then
            bit_tick <= '0';
            if clock_en='1' then
                if bit_div="1111" then
                    bit_div  <= "00" & unsigned(rate_ctrl);
                    bit_tick <= motor_on;
                else
                    bit_div <= bit_div + 1;
                end if;
            end if;
            if reset='1' then
                bit_div <= "0000";
            end if;
        end if;            
    end process;
    
    -- stream from memory
    p_stream: process(clock)
    begin
        if rising_edge(clock) then
            do_read <= '0';
            if bit_tick='1' then
                mem_bit_cnt <= mem_bit_cnt + 1;
                if mem_bit_cnt="000" then
                    mem_shift <= drv_rdata;
                        -- issue command to fifo
                    do_read <= mode; --'1'; does not pulse when in write mode
                else
                    mem_shift <= mem_shift(6 downto 0) & '1';
                end if;
            end if;
            if reset='1' then
                mem_shift    <= (others => '1');
                mem_bit_cnt  <= "000";
            end if;
        end if;
    end process;
    
    -- parallelize stream and generate sync
    -- and handle writes
    p_reading: process(clock)
        variable s : std_logic;
    begin
        if rising_edge(clock) then
            if rd_shift = "1111111111" and mode='1' then
                s := '0';
            else
                s := '1';
            end if;
            sync_i <= s;
            
            do_advance <= '0';
            mode_d <= mode;
            if mode_d='1' and mode='0' then -- going to write
                write_delay <= 2;
                do_advance <= '0';
            end if;
            
            do_write <= '0';
			if rd_bit_cnt = "111" and mode='0' and bit_div = X"7" and clock_en='1' then
                if write_delay = 0 then
    				do_write <= floppy_inserted; --'1';
    		    else
                    do_advance <= '1';
    		        write_delay <= write_delay - 1;
    		    end if;
			end if;
            
            if bit_tick='1' then
                rd_shift   <= rd_shift(8 downto 0) & mem_rd_bit;
                rd_bit_cnt <= rd_bit_cnt + 1;
            end if;
            if s = '0' then
                rd_bit_cnt <= "000";
            end if;

            if (rd_bit_cnt="111") and (soe = '1') and (bit_div(3)='1') then
                byte_rdy_i <= '0';
            else
                byte_rdy_i <= '1';
            end if;
        end if;
    end process;    
    
    p_move: process(clock)
        variable st : std_logic_vector(3 downto 0);
    begin
        if rising_edge(clock) then
            do_track_in  <= '0';
            do_track_out <= '0';
            do_head_bang <= '0';

            step_d <= step;
            
            st := step_d & step;
            down      <= '0';
            up        <= '0';

			-- track counter logic
			if st = "0011" then -- rollover down
                if track_c /= 0 then
                    track_c <= track_c - 1;
				end if;
			elsif st = "1100" then -- rollover up
				if track_c /= 21 then
					track_c <= track_c + 1;
				end if;
			end if;

			-- sound logic
			if st = "1100" or st = "0110" then
				do_track_in <= '1';
			elsif st = "0011" or st = "1001" then
				if track_c = 0 then
					do_track_out <= '1';
				else
					do_track_out <= '1';
				end if;
			end if;				
				            
            if reset='1' then
                track_c <= "01000";
            end if;            
        end if;
    end process;

	track_i    <= track_c & unsigned(step_d);

    -- outputs
    sync       <= sync_i;
    read_data  <= rd_shift(7 downto 0);
    byte_ready <= byte_rdy_i;
    track      <= std_logic_vector(track_i);
    track_is_0 <= '1' when track_i = "0000000" else '0';
end gideon;
