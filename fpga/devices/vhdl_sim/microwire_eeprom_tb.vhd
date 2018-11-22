--------------------------------------------------------------------------------
-- Entity: microwire_eeprom_tb
-- Date:2018-08-14  
-- Author: gideon     
--
-- Description: Testbench for EEPROM
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.io_bus_bfm_pkg.all;

entity microwire_eeprom_tb is
end entity;

architecture test of microwire_eeprom_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;
    signal sel_in      : std_logic := '0';
    signal clk_in      : std_logic := '0';
    signal data_in     : std_logic := '0';
    signal data_out    : std_logic := '0';
    
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;
    
    i_mut: entity work.microwire_eeprom
    port map (
        clock    => clock,
        reset    => reset,
        io_req   => io_req,
        io_resp  => io_resp,
        sel_in   => sel_in,
        clk_in   => clk_in,
        data_in  => data_in,
        data_out => data_out
    );
    
    i_bfm: entity work.io_bus_bfm
    generic map(
        g_name => "io"
    )
    port map(
        clock  => clock,
        req    => io_req,
        resp   => io_resp
    );

    p_test: process
        procedure command(opcode : std_logic_vector(1 downto 0); address : unsigned;
                          data : std_logic_vector; extra_clocks : natural := 0) is
        begin
            clk_in <= '0';
            data_in <= '1';
            wait for 500 ns;
            sel_in <= '1';
            wait for 200 ns;
            if data_out /= '1' then
                wait until data_out = '1';
            end if;
            wait for 200 ns;
            clk_in <= '1';
            wait for 500 ns;
            clk_in <= '0';
            for i in opcode'range loop
                data_in <= opcode(i);
                wait for 500 ns;
                clk_in <= '1';
                wait for 500 ns;
                clk_in <= '0';
            end loop;
            for i in address'range loop
                data_in <= address(i);
                wait for 500 ns;
                clk_in <= '1';
                wait for 500 ns;
                clk_in <= '0';
            end loop;                
            for i in data'range loop
                data_in <= data(i);
                wait for 500 ns;
                clk_in <= '1';
                wait for 500 ns;
                clk_in <= '0';
            end loop;                
            data_in <= '1';
            for i in 1 to extra_clocks loop
                wait for 500 ns;
                clk_in <= '1';
                wait for 500 ns;
                clk_in <= '0';
            end loop;            
            sel_in <= '0';
            wait for 500 ns;                        
        end procedure;
        constant c_no_data : std_logic_vector(-1 downto 0) := (others => '0');
    begin
        wait until reset = '0';
        wait for 1 us;
        command("00", "1100000000", c_no_data); -- write enable
        command("01", "0000000001", X"1234"); -- write
        command("10", "0000000000", c_no_data, 64); -- read
        command("00", "0100000000", X"5678"); -- write all
        command("00", "1000001011", c_no_data); -- erase all
        command("01", "0000010001", X"1234"); -- write
        command("11", "0000010001", c_no_data); -- erase one
        report "Invalid writes";
        command("01", "000000000", X"3456"); -- invalid write, as address is too short
        command("01", "00000000000", X"3456"); -- invalid write, as address is too long
        command("00", "0000000000", c_no_data); -- write disable
        wait;
    end process;

end architecture;

