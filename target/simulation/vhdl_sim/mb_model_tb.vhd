library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.tl_string_util_pkg.all;

library std;
use std.textio.all;

entity mb_model_tb is

end entity;

architecture test of mb_model_tb is

    signal clock  : std_logic := '0';
    signal reset  : std_logic;

    signal io_addr     : unsigned(31 downto 0);
    signal io_write    : std_logic;
    signal io_read     : std_logic;
    signal io_byte_en  : std_logic_vector(3 downto 0);
    signal io_wdata    : std_logic_vector(31 downto 0);
    signal io_rdata    : std_logic_vector(31 downto 0) := (others => 'Z');
    signal io_ack      : std_logic := '0'; 

begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    model: entity work.mb_model
    port map (
        clock     => clock,
        reset     => reset,
        io_addr   => io_addr,
        io_byte_en=> io_byte_en,
        io_write  => io_write,
        io_read   => io_read,
        io_wdata  => io_wdata,
        io_rdata  => io_rdata,
        io_ack    => io_ack  );
   

    -- memory and IO
    process(clock)
        variable s    : line;
        variable char : character;
        variable byte : std_logic_vector(7 downto 0);
    begin
        if rising_edge(clock) then
            io_ack <= '0';
            if io_write = '1' then
                io_ack <= '1';
                case io_addr(19 downto 0) is
                when X"00000" => -- interrupt
                    null;
                when X"00010" => -- UART_DATA
                    byte := io_wdata(31 downto 24);
                    char := character'val(to_integer(unsigned(byte)));
                    if byte = X"0D" then
                        -- Ignore character 13
                    elsif byte = X"0A" then
                        -- Writeline on character 10 (newline)
                        writeline(output, s);
                    else
                        -- Write to buffer
                        write(s, char);
                    end if;
                when others =>
                    report "I/O write to " & hstr(io_addr) & " dropped";
                end case;
            elsif io_read = '1' then
                io_ack <= '1';
                case io_addr(19 downto 0) is
                when X"0000C" => -- Capabilities
                    io_rdata <= X"00000002";
                when X"00012" => -- UART_FLAGS
                    io_rdata <= X"40404040";
                when X"2000A" => -- 1541_A memmap
                    io_rdata <= X"3F3F3F3F";
                when X"2000B" => -- 1541_A audiomap
                    io_rdata <= X"3E3E3E3E";
                when others =>
                    report "I/O read to " & hstr(io_addr) & " dropped";
                    io_rdata <= X"00000000";
                end case;
            end if;
        end if;
    end process;
    
end architecture;
