library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity slot_trace is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;

    data_in         : in  std_logic_vector(31 downto 0);
    trigger         : in  std_logic;

    io_req          : in  t_io_req;
    io_resp         : out t_io_resp
);
end entity;    

architecture gideon of slot_trace is
    signal addr     : unsigned(8 downto 0);
    signal state    : std_logic;
    signal rd_ack   : std_logic;
    signal rd_data  : std_logic_vector(7 downto 0);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            if state = '0' then
                if trigger = '1' then
                    addr <= (others => '0');
                    state <= '1';
                end if;
            else
                addr <= addr + 1;
                if addr = "111111111" then
                    state <= '0';
                end if;
            end if;
            if reset = '1' then
                state <= '0';
                addr <= (others => '0');
            end if;
        end if;
    end process;

    dpram_8x32_inst: entity work.dpram_8x32
    port map (
        CLKA  => clock,
        SSRA  => reset,
        ENA   => io_req.read,
        WEA   => '0',
        ADDRA => std_logic_vector(io_req.address(10 downto 0)),
        DIA   => X"00",
        DOA   => rd_data,

        CLKB  => clock,
        SSRB  => reset,
        ENB   => state,
        WEB   => state,
        ADDRB => std_logic_vector(addr),
        DIB   => data_in,
        DOB   => open
    );

    io_resp.ack  <= (io_req.read or io_req.write) when rising_edge(clock);
    rd_ack       <= io_req.read when rising_edge(clock);
    io_resp.data <= rd_data when rd_ack = '1' else X"00";
    
--    pseudo_dpram_8x32_inst: entity work.pseudo_dpram_8x32
--    port map (
--        rd_clock   => clock,
--        rd_address => io_req.address(10 downto 0),
--        rd_data    => rd_data,
--        rd_en      => io_req.read,
--        wr_clock   => clock,
--        wr_address => addr,
--        wr_data    => data_in,
--        wr_en      => state
--    );


end architecture;
