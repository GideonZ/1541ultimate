--------------------------------------------------------------------------------
-- Entity: amd_flash
-- Date:2018-08-12  
-- Author: gideon     
--
-- Description: Emulation of AMD flash, in this case 29F040 (512K)
-- This is a behavioral model of a Flash chip. It does not store the actual data
-- The 'allow_write' signal tells the client of this model to store the data
-- into the array. Erase functions will need to be performed by software to
-- modify the array accordingly. For this purpose, the erase byte can be polled
-- and cleared by software. A non-zero value indicates the sector that needs
-- to be erased. One bit for each sector. When 'FF' a chip-erase is requested.
-- In case of a pending erase, the rdata indicates whether the erase is done.
-- When rdata_valid is active, the client should forward the rdata from this
-- module, rather than the data from the array.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;

entity amd_flash is
port  (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
    io_irq      : out std_logic;
    
    allow_write : out std_logic;
    
    -- flash bus
    address     : in  unsigned(18 downto 0);
    wdata       : in  std_logic_vector(7 downto 0);
    write       : in  std_logic;
    read        : in  std_logic;
    rdata       : out std_logic_vector(7 downto 0);
    rdata_valid : out std_logic );   
end entity;

architecture arch of amd_flash is
    type t_state is (idle, prot1, prot2, program, erase1, erase2, erase3, erasing, suspend, auto_select);
    signal state    : t_state;
    signal toggle   : std_logic;
    signal dirty    : std_logic;
    signal erase_sectors    : std_logic_vector(7 downto 0);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            if io_req.read = '1' then
                io_resp.ack <= '1';
                if io_req.address(0) = '0' then
                    io_resp.data <= erase_sectors;
                else
                    io_resp.data(0) <= dirty;
                end if;
            elsif io_req.write = '1' then
                io_resp.ack <= '1';
                erase_sectors <= X"00";
                io_irq <= '0';
            end if;

            if write = '1' then
                if wdata = X"F0" then
                    allow_write <= '0';
                    state <= idle;
                else
                    case state is
                    when idle =>
                        if address(14 downto 0) = "101010101010101" and wdata = X"AA" then
                            state <= prot1;
                        end if;
                    
                    when prot1 =>
                        if address(14 downto 0) = "010101010101010" and wdata = X"55" then
                            state <= prot2;
                        else
                            state <= idle;
                        end if;
                        
                    when prot2 =>
                        if address(14 downto 0) = "101010101010101" then
                            case wdata is
                            when X"90" =>
                                state <= auto_select;
                            when X"A0" =>
                                allow_write <= '1';
                                state <= program;
                            when X"80" =>
                                state <= erase1;
                            when others =>
                                state <= idle;
                            end case;
                        else
                            state <= idle;
                        end if;
                    
                    when program =>
                        allow_write <= '0';
                        dirty <= '1';
                        state <= idle;
                    
                    when erase1 => 
                        if address(14 downto 0) = "101010101010101" and wdata = X"AA" then
                            state <= erase2;
                        else
                            state <= idle;
                        end if;
                    
                    when erase2 =>
                        if address(14 downto 0) = "010101010101010" and wdata = X"55" then
                            state <= erase3;
                        else
                            state <= idle;
                        end if;
                    
                    when erase3 =>
                        if address(14 downto 0) = "101010101010101" and wdata = X"10" then
                            erase_sectors <= (others => '1');
                            state <= erasing;
                            io_irq <= '1';
                        elsif wdata = X"30" then
                            erase_sectors(to_integer(address(18 downto 16))) <= '1';
                            state <= erasing;
                            io_irq <= '1';
                        else
                            state <= idle;
                        end if;
                    
                    when erasing =>
                        if wdata = X"B0" then
                            state <= suspend;
                        end if;
                    
                    when suspend =>
                        if wdata = X"30" then
                            state <= erasing;
                        end if;
                    
                    when others =>
                        null;
                    end case;
                end if;
            elsif read = '1' then
                case state is
                when idle | prot1 | prot2 | auto_select | erase1 | erase2 | erase3 =>
                    state <= idle;
                    toggle <= '0';
                when erasing =>
                    toggle <= not toggle;
                when others =>
                    null;
                end case;
            end if;    

            if state = erasing and erase_sectors = X"00" then
                state <= idle;
            end if;

            if reset = '1' then
                state <= idle;
                allow_write <= '0';
                erase_sectors <= (others => '0');
                io_irq <= '0';
                toggle <= '0';
            end if;
        end if;
    end process;

    process(state, address, toggle)
    begin
        rdata_valid <= '0';
        rdata <= X"00";
        
        case state is
        when erasing =>
            rdata_valid <= '1';
            rdata <= '0' & toggle & "000000";
        when auto_select =>
            rdata_valid <= '1';
            if address(7 downto 0) = X"00" then
                rdata <= X"01";
            elsif address(7 downto 0) = X"01" then
                rdata <= X"A4";
            end if;
        when others =>
            null;
        end case;
    end process;
end architecture;

