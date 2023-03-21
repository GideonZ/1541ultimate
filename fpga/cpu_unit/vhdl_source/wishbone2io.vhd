--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2022
-- Entity: wishbone2memio
-- Date:2022-02-15
-- Author: Gideon     
-- Description: This module bridges Wishbone to an 8-bit IO bus, Little Endian
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    
entity wishbone2io is
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;
        
        wb_tag_o    : in  std_ulogic_vector(02 downto 0); -- request tag
        wb_adr_o    : in  std_ulogic_vector(31 downto 0); -- address
        wb_dat_i    : out std_ulogic_vector(31 downto 0) := (others => 'U'); -- read data
        wb_dat_o    : in  std_ulogic_vector(31 downto 0); -- write data
        wb_we_o     : in  std_ulogic; -- read/write
        wb_sel_o    : in  std_ulogic_vector(03 downto 0); -- byte enable
        wb_stb_o    : in  std_ulogic; -- strobe
        wb_cyc_o    : in  std_ulogic; -- valid cycle
        wb_lock_o   : in  std_ulogic; -- exclusive access request
        wb_ack_i    : out std_ulogic := 'L'; -- transfer acknowledge
        wb_err_i    : out std_ulogic := 'L'; -- transfer error

        io_busy     : out std_logic;
        io_req      : out t_io_req;
        io_resp     : in  t_io_resp );

end entity;

architecture arch of wishbone2io is
    type t_state is (idle, io_access);
    signal state       : t_state;
    signal io_req_i    : t_io_req;
    type t_int4_array is array(natural range <>) of integer range 0 to 3;
    --                                               0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  => 1,2,4,8 byte, 3,C word, F dword
    constant c_remain   : t_int4_array(0 to 15) := ( 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 3 );
    signal remain       : integer range 0 to 3;
begin
    wb_err_i <= '0';
    
    -- 32 bit to 8 bit conversion for write
    process(io_req_i, wb_dat_o)
    begin
        io_req <= io_req_i;

        -- Fill in the byte to write, based on the address
        case io_req_i.address(1 downto 0) is
        when "00" =>
            io_req.data <= std_logic_vector(wb_dat_o(07 downto 00));
        when "01" =>
            io_req.data <= std_logic_vector(wb_dat_o(15 downto 08));
        when "10" =>
            io_req.data <= std_logic_vector(wb_dat_o(23 downto 16));
        when "11" =>
            io_req.data <= std_logic_vector(wb_dat_o(31 downto 24));
        when others =>
            null;
        end case; 
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            io_req_i.read <= '0';
            io_req_i.write <= '0';
            wb_ack_i <= '0';
            case state is
            when idle =>
                wb_dat_i <= (others => '0');
                if wb_stb_o = '1' then
                    io_req_i.address <= unsigned(wb_adr_o(io_req_i.address'range));
                    remain <= c_remain(to_integer(unsigned(wb_sel_o)));
                    
                    if wb_adr_o(31 downto 28) = X"1" then -- I/O
                        if wb_we_o = '1' then
                            io_req_i.write <= '1';
                        else
                            io_req_i.read <= '1';
                        end if;
                        state <= io_access;
                    end if;
                end if;

            when io_access =>
                case io_req_i.address(1 downto 0) is
                when "00" =>
                    wb_dat_i(7 downto 0) <= std_ulogic_vector(io_resp.data);
                when "01" =>
                    wb_dat_i(15 downto 8) <= std_ulogic_vector(io_resp.data);
                when "10" =>
                    wb_dat_i(23 downto 16) <= std_ulogic_vector(io_resp.data);
                when "11" =>
                    wb_dat_i(31 downto 24) <= std_ulogic_vector(io_resp.data);
                when others =>
                    null;
                end case;

                if io_resp.ack = '1' then
                    if remain = 0 then
                        state <= idle;
                        wb_ack_i <= '1';
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
    io_busy <= '1' when state = io_access else '0';
    
end architecture;
