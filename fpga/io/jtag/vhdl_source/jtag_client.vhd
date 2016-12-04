--------------------------------------------------------------------------------
-- Entity: jtag_client
-- Date:2016-11-08  
-- Author: Gideon     
--
-- Description: Client for Virtual JTAG module
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity jtag_client is
port (
    avm_clock           : in  std_logic;
    avm_reset           : in  std_logic;
    avm_read            : out std_logic;
    avm_write           : out std_logic;
    avm_byteenable      : out std_logic_vector(3 downto 0);
    avm_address         : out std_logic_vector(31 downto 0);
    avm_writedata       : out std_logic_vector(31 downto 0);
    avm_readdata        : in  std_logic_vector(31 downto 0);
    avm_readdatavalid   : in  std_logic;
    avm_waitrequest     : in  std_logic;

    clock_1         : in  std_logic;
    clock_2         : in  std_logic;
    sample_vector   : in  std_logic_vector(47 downto 0);
    write_vector    : out std_logic_vector(7 downto 0) );
end entity;

architecture arch of jtag_client is
    component virtual_jtag is
        port (
            tdi                : out std_logic;                                       -- tdi
            tdo                : in  std_logic                    := 'X';             -- tdo
            ir_in              : out std_logic_vector(3 downto 0);                    -- ir_in
            ir_out             : in  std_logic_vector(3 downto 0) := (others => 'X'); -- ir_out
            virtual_state_cdr  : out std_logic;                                       -- virtual_state_cdr
            virtual_state_sdr  : out std_logic;                                       -- virtual_state_sdr
            virtual_state_e1dr : out std_logic;                                       -- virtual_state_e1dr
            virtual_state_pdr  : out std_logic;                                       -- virtual_state_pdr
            virtual_state_e2dr : out std_logic;                                       -- virtual_state_e2dr
            virtual_state_udr  : out std_logic;                                       -- virtual_state_udr
            virtual_state_cir  : out std_logic;                                       -- virtual_state_cir
            virtual_state_uir  : out std_logic;                                       -- virtual_state_uir
            tck                : out std_logic                                        -- clk
        );
    end component virtual_jtag;

    constant c_rom            : std_logic_vector(31 downto 0) := X"DEAD1541";
    signal shiftreg_sample    : std_logic_vector(sample_vector'range);
    signal shiftreg_write     : std_logic_vector(write_vector'range);
    signal shiftreg_debug     : std_logic_vector(24 downto 0);
    signal shiftreg_clock     : std_logic_vector(7 downto 0);
    signal debug_bits         : std_logic_vector(4 downto 0);
    signal clock_1_shift      : std_logic_vector(7 downto 0) := X"00";
    signal clock_2_shift      : std_logic_vector(7 downto 0) := X"00";

    signal bypass_reg         : std_logic;
    signal bit_count          : unsigned(4 downto 0) := (others => '0');    
    signal tdi                : std_logic;                                       -- tdi
    signal tdo                : std_logic                    := 'X';             -- tdo
    signal ir_in              : std_logic_vector(3 downto 0);                    -- ir_in
    signal ir_out             : std_logic_vector(3 downto 0) := (others => 'X'); -- ir_out
    signal virtual_state_cdr  : std_logic;                                       -- virtual_state_cdr
    signal virtual_state_sdr  : std_logic;                                       -- virtual_state_sdr
    signal virtual_state_e1dr : std_logic;                                       -- virtual_state_e1dr
    signal virtual_state_pdr  : std_logic;                                       -- virtual_state_pdr
    signal virtual_state_e2dr : std_logic;                                       -- virtual_state_e2dr
    signal virtual_state_udr  : std_logic;                                       -- virtual_state_udr
    signal virtual_state_cir  : std_logic;                                       -- virtual_state_cir
    signal virtual_state_uir  : std_logic;                                       -- virtual_state_uir
    signal tck                : std_logic;                                       -- clk

    signal write_fifo_full    : std_logic;
    signal write_fifo_put     : std_logic := '0';
    signal write_fifo_din     : std_logic_vector(11 downto 0);
    signal read_fifo_get      : std_logic;
    signal shiftreg_fifo      : std_logic_vector(15 downto 0);
    signal read_fifo_count    : unsigned(7 downto 0);
    signal read_fifo_data     : std_logic_vector(7 downto 0);
    signal cmd_count          : unsigned(7 downto 0) := X"00";
    signal last_command       : std_logic_vector(11 downto 0);
    -- signals that live in the avm clock domain
    type t_state is (idle, do_write, do_read, do_read2, do_read3);
    signal state            : t_state;
    signal incrementing     : std_logic;
    signal byte_count       : integer range 0 to 3;
    signal read_count       : unsigned(7 downto 0);
    signal avm_read_reg     : std_logic_vector(31 downto 0) := (others => '0');
    signal write_enabled    : std_logic;
    signal write_data       : std_logic_vector(31 downto 0) := (others => '0');
    signal write_be         : std_logic_vector(3 downto 0) := (others => '0');
    signal address          : unsigned(31 downto 0) := (others => '0');
    signal avm_rfifo_put    : std_logic;
    signal avm_rfifo_full   : std_logic;
    signal avm_wfifo_get    : std_logic;
    signal avm_wfifo_valid  : std_logic;
    signal avm_read_i       : std_logic;
    signal avm_write_i      : std_logic;
    signal avm_wfifo_dout   : std_logic_vector(11 downto 0);
    signal avm_wfifo_count  : unsigned(4 downto 0);
    signal avm_exec_count   : unsigned(2 downto 0) := "000";
    signal avm_clock_count  : unsigned(2 downto 0) := "000";
begin
    i_vj: virtual_jtag
    port map (
        tdi                => tdi,
        tdo                => tdo,
        ir_in              => ir_in,
        ir_out             => ir_out,
        virtual_state_cdr  => virtual_state_cdr,
        virtual_state_sdr  => virtual_state_sdr,
        virtual_state_e1dr => virtual_state_e1dr,
        virtual_state_pdr  => virtual_state_pdr,
        virtual_state_e2dr => virtual_state_e2dr,
        virtual_state_udr  => virtual_state_udr,
        virtual_state_cir  => virtual_state_cir,
        virtual_state_uir  => virtual_state_uir,
        tck                => tck
    );
    
    process(tck)
    begin
        if rising_edge(tck) then
            read_fifo_get <= '0';
            write_fifo_put <= '0';
            if write_fifo_put = '1' then
                last_command <= write_fifo_din;
            end if;

            if virtual_state_sdr = '1' then
                bypass_reg <= tdi;
                shiftreg_sample <= tdi & shiftreg_sample(shiftreg_sample'high downto 1);
                shiftreg_write <= tdi & shiftreg_write(shiftreg_write'high downto 1);
                shiftreg_debug <= tdi & shiftreg_debug(shiftreg_debug'high downto 1);
                shiftreg_clock <= tdi & shiftreg_clock(shiftreg_clock'high downto 1);
                bit_count <= bit_count + 1;
                if ir_in = X"4" then
                    if bit_count(2 downto 0) = "111" then
                        shiftreg_fifo <= X"00" & read_fifo_data;
                        read_fifo_get <= not tdi;
                    else
                        shiftreg_fifo <= '0' & shiftreg_fifo(shiftreg_fifo'high downto 1);
                    end if;
                elsif ir_in = X"5" then
                    shiftreg_fifo <= tdi & shiftreg_fifo(shiftreg_fifo'high downto 1);
                    if bit_count(3 downto 0) = "1111" then
                        write_fifo_put <= '1';
                        cmd_count <= cmd_count + 1;
                    end if;
                elsif ir_in = X"6" then
                    shiftreg_fifo <= tdi & shiftreg_fifo(shiftreg_fifo'high downto 1);
                    if bit_count(2 downto 0) = "111" then
                        write_fifo_put <= '1';
                        cmd_count <= cmd_count + 1;
                    end if;
                end if;                
            end if;

            if virtual_state_cdr = '1' then
                shiftreg_write  <= (others => '0');
                shiftreg_sample <= sample_vector;
                bit_count <= (others => '0');
                shiftreg_fifo <= X"00" & std_logic_vector(read_fifo_count);
                shiftreg_debug <= debug_bits & last_command & std_logic_vector(cmd_count);
                if ir_in = X"8" then
                    shiftreg_clock <= clock_1_shift;
                end if;  
                if ir_in = X"9" then
                    shiftreg_clock <= clock_2_shift;
                end if;  
            end if;
           
            if virtual_state_udr = '1' then
                case ir_in is
                when X"2" =>
                    write_vector <= shiftreg_write;
                when others =>
                    null;
                end case;
            end if;
        end if;
    end process;

    process(clock_1)
    begin
        if rising_edge(clock_1) then
            clock_1_shift <= clock_1_shift(6 downto 0) & not clock_1_shift(7);
        end if;
    end process;
    
    process(clock_2)
    begin
        if rising_edge(clock_2) then
            clock_2_shift <= clock_2_shift(6 downto 0) & not clock_2_shift(7);
        end if;
    end process;

    process(ir_in, bit_count, shiftreg_sample, shiftreg_write, bypass_reg, shiftreg_fifo, shiftreg_debug, shiftreg_clock)
    begin
        case ir_in is
        when X"0" =>
            tdo <= c_rom(to_integer(bit_count));
        when X"1" =>
            tdo <= shiftreg_sample(0);
        when X"2" =>
            tdo <= shiftreg_write(0);
        when X"3" =>
            tdo <= shiftreg_debug(0);
        when X"4" =>
            tdo <= shiftreg_fifo(0);
        when X"8" | X"9" =>
            tdo <= shiftreg_clock(0);
        when others =>
            tdo <= bypass_reg;
        end case;            
    end process;
    
    -- Avalon JTAG commands
    -- E000 => write data (byte is data)
    -- x0010 => Start Write Non Incrementing  (only upper bit of byte used)
    -- x0011 => Start Write incrementing  (only upper bit of byte used)
    -- x010 => Do Read Non-Incrementing (byte is length)
    -- x011 => Do Read incrementing (byte is length)
    -- x1xx => Set address (bytes is address data) xx = index of address byte
    -- 
    write_fifo_din <= ("1000" & shiftreg_fifo(15 downto 8)) when ir_in = X"6" else
                      (shiftreg_fifo(11 downto 0));
                      
    i_write_fifo: entity work.async_fifo_ft
    generic map(
        g_data_width => 12,
        g_depth_bits => 4
    )
    port map (
        wr_clock     => tck,
        wr_reset     => '0',
        wr_en        => write_fifo_put,
        wr_din       => write_fifo_din,
        wr_full      => write_fifo_full,
        rd_clock     => avm_clock,
        rd_reset     => avm_reset,
        rd_next      => avm_wfifo_get,
        rd_dout      => avm_wfifo_dout,
        rd_valid     => avm_wfifo_valid,
        rd_count     => avm_wfifo_count
    );
    
    i_read_fifo: entity work.async_fifo_ft
    generic map(
        g_data_width => 8,
        g_depth_bits => 7 )
    port map(
        wr_clock     => avm_clock,
        wr_reset     => avm_reset,
        wr_en        => avm_rfifo_put,
        wr_din       => avm_read_reg(7 downto 0),
        wr_full      => avm_rfifo_full,
        rd_clock     => tck,
        rd_reset     => '0',
        rd_next      => read_fifo_get,
        rd_dout      => read_fifo_data,
        rd_valid     => open,
        rd_count     => read_fifo_count
    );

    process(avm_clock)
        variable v_cmd  : std_logic_vector(2 downto 0);
    begin
        if rising_edge(avm_clock) then
            avm_clock_count <= avm_clock_count + 1;
            case state is
            when idle =>
                avm_rfifo_put <= '0';
                if avm_wfifo_valid = '1' then -- command?
                    avm_exec_count <= avm_exec_count + 1;
                    v_cmd := avm_wfifo_dout(10 downto 8);
                    case v_cmd is
                    when "000" =>
                        if write_enabled = '1' then
                            write_be(3) <= avm_wfifo_dout(11);
                            write_data(31 downto 24) <= avm_wfifo_dout(7 downto 0);
                            write_be(2 downto 0) <= write_be(3 downto 1);
                            write_data(23 downto 00) <= write_data(31 downto 8);
                            if byte_count = 3 then
                                avm_write_i <= '1';
                                state <= do_write;
                                byte_count <= 0;
                            else
                                byte_count <= byte_count + 1;
                            end if;
                        end if;
                    when "001" =>
                        byte_count <= 0;
                        write_enabled <= '1';
                        incrementing <= avm_wfifo_dout(7);
                    when "010" | "011" =>
                        write_enabled <= '0';
                        read_count <= unsigned(avm_wfifo_dout(7 downto 0));
                        state <= do_read;
                        incrementing <= v_cmd(0);
                    when "100" =>
                        write_enabled <= '0';
                        address(7 downto 0) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when "101" =>
                        write_enabled <= '0';
                        address(15 downto 8) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when "110" =>
                        write_enabled <= '0';
                        address(23 downto 16) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when "111" =>
                        write_enabled <= '0';
                        address(31 downto 24) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when others =>
                        null;
                    end case;
                end if;
            when do_write =>
                if avm_waitrequest = '0' then
                    avm_write_i <= '0';
                    state <= idle;
                    if incrementing = '1' then
                        address(31 downto 2) <= address(31 downto 2) + 1;
                    end if;
                end if;
            when do_read =>
                write_be <= "1111";
                avm_read_i <= '1';
                state <= do_read2;
            when do_read2 =>
                if avm_waitrequest = '0' then
                    avm_read_i <= '0';
                end if;
                if avm_readdatavalid = '1' then
                    avm_read_reg <= avm_readdata;
                    byte_count <= 0;
                    avm_rfifo_put <= '1';
                    state <= do_read3;
                end if;
            when do_read3 =>
                if avm_rfifo_full = '0' then
                    if byte_count = 3 then
                        byte_count <= 0;
                        avm_rfifo_put <= '0';
                        read_count <= read_count - 1;
                        if read_count = 0 then
                            state <= idle;
                        else
                            state <= do_read;
                            if incrementing = '1' then
                                address(31 downto 2) <= address(31 downto 2) + 1;
                            end if;
                        end if;
                    else
                        byte_count <= byte_count + 1;
                        avm_read_reg(23 downto 0) <= avm_read_reg(31 downto 8);
                    end if;
                end if;
            when others =>
                null;
            end case;

            if avm_reset = '1' then
                state <= idle;
                incrementing <= '0';
                byte_count <= 0;
                read_count <= (others => '0');
                write_enabled <= '0';
                write_be <= "0000";
                avm_read_i <= '0';
                avm_write_i <= '0';
                avm_rfifo_put <= '0';
                avm_exec_count <= "000";
            end if;                        
                            
        end if;
    end process;

    with state select debug_bits(2 downto 0) <=
        "000" when idle,
        "001" when do_write,
        "010" when do_read,
        "011" when do_read2,
        "100" when do_read3,
        "111" when others;

    debug_bits(3) <= avm_read_i;
    debug_bits(4) <= avm_write_i;
    
    avm_read      <= avm_read_i;
    avm_write     <= avm_write_i;
    avm_wfifo_get <= '1' when avm_wfifo_valid = '1' and state = idle else '0';
    
    avm_address <= std_logic_vector(address(31 downto 2) & "00");
    avm_byteenable <= write_be;
    avm_writedata <= write_data;
    
end arch;
