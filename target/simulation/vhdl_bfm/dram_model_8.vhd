-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : DRAM model
-------------------------------------------------------------------------------
-- File       : dram_model_8.vhd
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This simple DRAM model uses the flat memory model package.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.tl_flat_memory_model_pkg.all;
use work.tl_string_util_pkg.all;

entity dram_model_8 is
generic (
    g_given_name  : string;
    g_cas_latency : positive := 2;
    g_burst_len_r : positive := 1;
    g_burst_len_w : positive := 1;
    g_column_bits : positive := 10;
    g_row_bits    : positive := 13;
    g_bank_bits   : positive := 2 );
port (
    CLK  : in    std_logic;
    CKE  : in    std_logic;
    A    : in    std_logic_vector(g_row_bits-1 downto 0);
    BA   : in    std_logic_vector(g_bank_bits-1 downto 0);
    CSn  : in    std_logic;
    RASn : in    std_logic;
    CASn : in    std_logic;
    WEn  : in    std_logic;
    DQM  : in    std_logic;
    
    DQ   : inout std_logic_vector(7 downto 0) );
    
end dram_model_8;

architecture bfm of dram_model_8 is
    shared variable this : h_mem_object;
    signal bound         : boolean := false;
    signal command       : std_logic_vector(2 downto 0);
    constant c_banks    : integer := 2 ** g_bank_bits;
    type t_row_array is array(0 to c_banks-1) of std_logic_vector(g_row_bits-1 downto 0);
    signal bank_rows    : t_row_array;
    signal bank         : integer;

    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    signal r_queue      : t_byte_array(0 to g_cas_latency + g_burst_len_r) := (others => (others => 'Z'));

--    constant c_col      : integer := 0;
--    constant c_bank     : integer := g_column_bits;
--    constant c_row      : integer := g_column_bits + g_bank_bits;
begin
    bind: process
    begin
        register_mem_model(dram_model_8'path_name, g_given_name, this);
        bound <= true;
        wait;
    end process;

    command <= WEn & CASn & RASn;
    bank <= to_integer(unsigned(BA));
    
    DQ <= transport r_queue(0) after 6 ns;
    
    process(CLK)
        variable raddr : std_logic_vector(31 downto 0) := (others => '0');
        variable waddr : std_logic_vector(31 downto 0) := (others => '0');
        variable more_writes : integer := 0;

        function map_address(bank_bits : std_logic_vector(g_bank_bits-1 downto 0);
                             row_bits  : std_logic_vector(g_row_bits-1 downto 0);
                             col_bits  : std_logic_vector(g_column_bits-1 downto 0) ) return std_logic_vector is
            variable ret    : std_logic_vector(31 downto 0) := (others => '0');

        begin
            -- mapping used in v5_sdr
            --addr_bank   <= address_fifo(3 downto 2);
            --addr_row    <= address_fifo(24 downto 12);
            --addr_column <= address_fifo(11 downto 4) & address_fifo(1 downto 0);
            ret(g_bank_bits+1 downto 2) := bank_bits;
            ret(1 downto 0) := col_bits(1 downto 0);
            ret(g_column_bits+g_bank_bits-1 downto g_bank_bits+2) := col_bits(g_column_bits-1 downto 2);
            ret(g_bank_bits+g_column_bits+g_row_bits-1 downto g_bank_bits+g_column_bits) := row_bits;
            return ret;
        end function;

    begin
        if rising_edge(CLK) then
        	if bound and CKE='1' then
	            r_queue <= r_queue(1 to r_queue'high) & ("ZZZZZZZZ");
	            if more_writes > 0 then
	                waddr := std_logic_vector(unsigned(waddr) + 1);
	                if to_integer(unsigned(waddr)) mod g_burst_len_w = 0 then
	                    waddr := std_logic_vector(unsigned(waddr) - g_burst_len_w);
	                end if;
	                if DQM='0' then
	                    write_memory_8(this, waddr, DQ);
	                end if;
	                more_writes := more_writes - 1;
	            end if;
	                
	            if CSn='0' then
	                case command is
	                when "110" => -- RAS, register bank address
	                    bank_rows(bank) <= A(g_row_bits-1 downto 0);
	                    
	                when "101" => -- CAS, start read burst
	                    raddr := map_address(BA, bank_rows(bank), A(g_column_bits-1 downto 0));
	                    --raddr(c_bank+g_bank_bits-1 downto c_bank) := BA;
	                    --raddr(c_row+g_row_bits-1 downto c_row) := bank_rows(bank);
	                    --raddr(c_col+g_column_bits-1 downto c_col) := A(g_column_bits-1 downto 0);
	                    --report hstr(BA) & " " & hstr(bank_rows(bank)) & " " & hstr(A) & ": " & hstr(raddr);
	                    
	                    for i in 0 to g_burst_len_r-1 loop
	                        r_queue(g_cas_latency-1 + i) <= read_memory_8(this, raddr);
	                        raddr := std_logic_vector(unsigned(raddr) + 1);
	                        if to_integer(unsigned(raddr)) mod g_burst_len_r = 0 then
	                            raddr := std_logic_vector(unsigned(raddr) - g_burst_len_r);
	                        end if;
	                    end loop;
	
	                when "001" => -- CAS & WE, start write burst
	                    waddr := map_address(BA, bank_rows(bank), A(g_column_bits-1 downto 0));
	                    --waddr(c_bank+g_bank_bits-1 downto c_bank) := BA;
	                    --waddr(c_row+g_row_bits-1 downto c_row) := bank_rows(bank);
	                    --waddr(c_col+g_column_bits-1 downto c_col) := A(g_column_bits-1 downto 0);
	                    more_writes := g_burst_len_w - 1;
	                    if DQM='0' then
	                        write_memory_8(this, waddr, DQ);
	                    end if;
	
	                when others =>
	                    null;                                        
	                end case;
				end if;
            end if;
        end if;
    end process;
end bfm;
