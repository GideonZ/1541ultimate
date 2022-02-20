--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2022
-- Entity: wishbone2memio
-- Date:2022-02-15
-- Author: Gideon     
-- Description: This module bridges the Wishbone memory bus to the ultimate
--              memory bus
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    
entity wishbone2mem is
    generic (
        g_tag           : std_logic_vector(7 downto 0) := X"AE" );
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

        mem_req     : out t_mem_req_32;
        mem_resp    : in  t_mem_resp_32 );

end entity;

architecture arch of wishbone2mem is
    type t_state is (idle, mem_read, mem_write);
    signal state       : t_state;
    signal mem_req_i   : t_mem_req_32 := c_mem_req_32_init;
    signal read_ack    : std_logic;
    signal write_ack   : std_logic;
    signal request_accepted : std_logic := '0';
    signal in_range         : std_logic;
begin
    mem_req <= mem_req_i;

    mem_req_i.address(mem_req_i.address'high downto 2) <= unsigned(wb_adr_o(mem_req_i.address'high downto 2));
    mem_req_i.address(1 downto 0) <= "00";
    mem_req_i.byte_en <= wb_sel_o;
    mem_req_i.data <= wb_dat_o;
    mem_req_i.read_writen <= not wb_we_o;
    mem_req_i.tag <= g_tag;
    mem_req_i.request <= wb_stb_o and in_range and not request_accepted;
    
    in_range <= '1' when wb_adr_o(31 downto 28) = X"0" else '0';
    read_ack <= '1'  when mem_resp.dack_tag = g_tag and mem_req_i.read_writen = '1' else '0';
    write_ack <= '1' when mem_resp.rack_tag = g_tag and mem_req_i.read_writen = '0' else '0';
    
    wb_ack_i <= read_ack or write_ack;
    wb_err_i <= '0';
    
    process(clock)
    begin
        if rising_edge(clock) then
            wb_dat_i <= (others => '0');

            case state is
            when idle =>
                if mem_req_i.request = '1' then
                    if wb_we_o = '1' then
                        state <= mem_write;
                    else
                        state <= mem_read;
                    end if;
                end if;

            when mem_read =>
                if mem_resp.rack_tag = g_tag then
                    request_accepted <= '1';
                end if;
                if mem_resp.dack_tag = g_tag then
                    request_accepted <= '0';
                    wb_dat_i <= mem_resp.data;
                    state <= idle;
                end if;
                
            when mem_write =>
                if mem_resp.rack_tag = g_tag then
                    state <= idle;
                end if;

            when others =>
                null;
            end case;

            if reset='1' then
                state <= idle;
                request_accepted <= '0';
            end if;
        end if;
    end process;
    
end architecture;
