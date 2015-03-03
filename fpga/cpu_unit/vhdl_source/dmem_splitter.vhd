--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: dmem_splitter
-- Date:2015-02-28  
-- Author: Gideon     
-- Description: This module takes the Wishbone alike memory bus and splits it
--              into a 32 bit DRAM bus and an 8-bit IO bus
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    
library mblite;
    use mblite.core_Pkg.all;

entity dmem_splitter is
    generic (
        g_tag           : std_logic_vector(7 downto 0) := X"AE";
        g_support_io    : boolean := true );
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;
        
        dmem_i      : out dmem_in_type;
        dmem_o      : in  dmem_out_type;
        
        mem_req     : out t_mem_req_32;
        mem_resp    : in  t_mem_resp_32;
        
        io_req      : out t_io_req;
        io_resp     : in  t_io_resp );

end entity;

architecture arch of dmem_splitter is
    type t_state is (idle, mem_read, mem_write, io_access);
    signal state        : t_state;

    signal mem_req_i   : t_mem_req_32 := c_mem_req_32_init;
    signal io_req_i    : t_io_req;
    type t_int4_array is array(natural range <>) of integer range 0 to 3;
    --                                               0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  => 1,2,4,8 byte, 3,C word, F dword
    constant c_remain   : t_int4_array(0 to 15) := ( 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 3 );
    signal remain       : integer range 0 to 3;
begin
    io_req <= io_req_i;
    mem_req <= mem_req_i;
    
    process(state, mem_resp)
    begin
        dmem_i.ena_i <= '0';
        case state is
        when idle =>
            dmem_i.ena_i <= '1';
        when mem_read | io_access =>
            dmem_i.ena_i <= '0';
        when mem_write =>
--            if mem_resp.rack = '1' then
--                dmem_i.ena_i <= '1';
--            end if;
        when others =>
            dmem_i.ena_i <= '0';
        end case;
    end process;
    
    process(clock)
        impure function get_next_io_byte(a : unsigned(1 downto 0)) return std_logic_vector is
        begin
            case a is
            when "00" =>
                return dmem_o.dat_o(23 downto 16);
            when "01" =>
                return dmem_o.dat_o(15 downto 8);
            when "10" =>
                return dmem_o.dat_o(7 downto 0);
            when "11" =>
                return dmem_o.dat_o(31 downto 24);
            when others =>
                return "XXXXXXXX";
            end case;                            
        end function;
    begin
        if rising_edge(clock) then
            io_req_i.read <= '0';
            io_req_i.write <= '0';
            
            case state is
            when idle =>
                dmem_i.dat_i <= (others => 'X');
                if dmem_o.ena_o = '1' then
                    mem_req_i.address <= unsigned(dmem_o.adr_o(mem_req_i.address'range));
                    mem_req_i.address(1 downto 0) <= "00";
                    mem_req_i.byte_en <= dmem_o.sel_o;
                    mem_req_i.data <= dmem_o.dat_o;
                    mem_req_i.read_writen <= not dmem_o.we_o;
                    mem_req_i.tag <= g_tag;
                    io_req_i.address <= unsigned(dmem_o.adr_o(19 downto 0));
                    io_req_i.data <= get_next_io_byte("11");
                    
                    if dmem_o.adr_o(26) = '0' or not g_support_io then
                        mem_req_i.request <= '1';
                        if dmem_o.we_o = '1' then
                            state <= mem_write;
                        else
                            state <= mem_read;
                        end if;
                    else -- I/O
                        remain <= c_remain(to_integer(unsigned(dmem_o.sel_o)));
                        if dmem_o.we_o = '1' then
                            io_req_i.write <= '1';
                        else
                            io_req_i.read <= '1';
                        end if;
                        state <= io_access;
                    end if;
                end if;

            when mem_read =>
                if mem_resp.rack_tag = g_tag then
                    mem_req_i.request <= '0';
                end if;
                if mem_resp.dack_tag = g_tag then
                    dmem_i.dat_i <= mem_resp.data;
                    state <= idle;
                end if;
                
            when mem_write =>
                if mem_resp.rack_tag = g_tag then
                    mem_req_i.request <= '0';
                    state <= idle;
                end if;

            when io_access =>
                case io_req_i.address(1 downto 0) is
                when "00" =>
                    dmem_i.dat_i(31 downto 24) <= io_resp.data;
                when "01" =>
                    dmem_i.dat_i(23 downto 16) <= io_resp.data;
                when "10" =>
                    dmem_i.dat_i(15 downto 8) <= io_resp.data;
                when "11" =>
                    dmem_i.dat_i(7 downto 0) <= io_resp.data;
                when others =>
                    null;
                end case;

                if io_resp.ack = '1' then
                    io_req_i.data <= get_next_io_byte(io_req_i.address(1 downto 0));

                    if remain = 0 then
                        state <= idle;
                    else
                        remain <= remain - 1;
                        io_req_i.address(1 downto 0) <= io_req_i.address(1 downto 0) + 1;
                        if mem_req_i.read_writen = '0' then
                            io_req_i.write <= '1';
                        else
                            io_req_i.read <= '1';
                        end if;
                    end if;
                end if;

            when others =>
                null;
            end case;

            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;
end arch;

