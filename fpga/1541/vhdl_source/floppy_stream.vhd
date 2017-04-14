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
    reset           : in  std_logic;
    
    -- data from memory
    mem_rdata       : in  std_logic_vector(7 downto 0);
    do_read         : out std_logic;
    do_write        : out std_logic;
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
    bit_time        : in  unsigned(9 downto 0); -- in steps of 10 ns
        
    -- data to drive CPU
    read_data       : out std_logic_vector(7 downto 0) );
    
end floppy_stream;    

architecture gideon of floppy_stream is
    signal bit_square  : std_logic;
    signal bit_tick    : std_logic;
    signal bit_timer   : unsigned(8 downto 0);
    signal bit_carry   : std_logic;
    
    signal mem_bit_cnt : unsigned(2 downto 0);
    signal rd_bit_cnt  : unsigned(2 downto 0) := "000";
    signal mem_shift   : std_logic_vector(7 downto 0);
    signal rd_shift    : std_logic_vector(9 downto 0) := (others => '0');
    signal sync_i      : std_logic;
    signal byte_rdy_i  : std_logic;
    alias  mem_rd_bit  : std_logic is mem_shift(7);
    --signal track_c     : unsigned(6 downto 2);
    signal track_i     : unsigned(6 downto 0);
    signal mode_d      : std_logic;
    signal write_delay : integer range 0 to 3;

    -- weak bit implementation
    signal random_data : std_logic_vector(15 downto 0);
    signal bit_slip    : std_logic;
    signal bit_flip    : std_logic;
    signal weak_count  : integer range 0 to 63 := 0;
    signal enable_slip : std_logic;
begin
    p_clock_div: process(clock)
    begin
        if rising_edge(clock) then
            bit_tick <= '0';
            if bit_timer = 0 then
                bit_tick <= motor_on;
                bit_carry <= not bit_carry and bit_time(0); -- toggle if bit 0 is set
                if bit_carry='1' then
                    bit_timer <= bit_time(9 downto 1);
                else
                    bit_timer <= bit_time(9 downto 1) - 1;
                end if;
            else
                bit_timer <= bit_timer - 1;
            end if;
            bit_square <= '0';
            if bit_timer < ('0' & bit_time(9 downto 2)) then
                bit_square <= '1';
            end if;
            
            if reset='1' then
                bit_timer <= to_unsigned(10, bit_timer'length);
                bit_carry <= '0';
            end if;
        end if;            
    end process;
    
    i_noise: entity work.noise_generator
    generic map (
        g_type          => "Galois",
        g_polynom       => X"1020",
        g_seed          => X"569A"
    )
    port map (
        clock           => clock,
        enable          => bit_tick,
        reset           => reset,
        q               => random_data  );
    
    -- stream from memory
    p_stream: process(clock)
        variable history : std_logic_vector(4 downto 0) := "11111";
    begin
        if rising_edge(clock) then
            do_read <= '0';
            if bit_tick='1' then
                history  := history(3 downto 0) & mem_rd_bit;
                bit_slip <= '0';
                bit_flip <= '0';
                if history = "00000" and mode = '1' then -- something weird can happen now:
                -- nothing
                -- bit flip
                -- bit slip (generates less bits)
                    bit_slip <= random_data(2) and random_data(7) and random_data(11) and enable_slip; -- 12.5%
                    bit_flip <= random_data(6) and random_data(14); -- 25%                    
                    if weak_count = 63 then
                        enable_slip <= '1';
                    else
                        weak_count <= weak_count + 1;
                    end if;
                else
                    weak_count <= 0;
                    enable_slip <= '0';
                end if;

                mem_bit_cnt <= mem_bit_cnt + 1;
                if mem_bit_cnt="000" then
                    mem_shift <= mem_rdata;
                    do_read <= mode; --'1'; does not pulse when in write mode
                else
                    mem_shift <= mem_shift(6 downto 0) & '1';
                end if;
            end if;
            if reset='1' then
                mem_shift    <= (others => '1');
                mem_bit_cnt  <= "000";
                bit_flip     <= '0';
                bit_slip     <= '0';
                enable_slip  <= '0';
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
                do_advance <= '1';
            end if;
            
            do_write <= '0';
            if rd_bit_cnt = "111" and mode='0' and bit_tick='1' then
                if write_delay = 0 then
                    do_write <= floppy_inserted; --'1';
                else
                    do_advance <= '0';
                    write_delay <= write_delay - 1;
                end if;
            end if;
            
            if bit_tick='1' and bit_slip = '0' then
                rd_shift   <= rd_shift(8 downto 0) & (mem_rd_bit or bit_flip);
                rd_bit_cnt <= rd_bit_cnt + 1;
            end if;
            if s = '0' then
                rd_bit_cnt <= "000";
            end if;

            if (rd_bit_cnt="111") and (soe = '1') and (bit_square='1') and (bit_slip = '0') then
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

            if motor_on='1' then
                st := std_logic_vector(track_i(1 downto 0)) & step;
    
                case st is
                when "0001" | "0110" | "1011" | "1100" => -- up
                    do_track_in <= '1';
                    if track_i /= 83 then
                        track_i <= track_i + 1;
                    end if;
                when "0011" | "0100" | "1001" | "1110" => -- down
                    do_track_out <= '1';
                    if track_i /= 0 then
                        track_i <= track_i - 1;
                    end if;
                when others =>
                    null;
                end case;
            end if;
                            
            if reset='1' then
                track_i <= "0100000";
            end if;            
        end if;
    end process;

    -- track_i    <= track_c & unsigned(step_d);

    -- outputs
    sync       <= sync_i;
    read_data  <= rd_shift(7 downto 0);
    byte_ready <= byte_rdy_i;
    track      <= std_logic_vector(track_i);
    track_is_0 <= '1' when track_i = "0000000" else '0';
end gideon;
