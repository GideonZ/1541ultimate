-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : sync_fifo_tb
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: Quick test bench for sync fifo
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
entity sync_fifo_tb is
end entity;

architecture test of sync_fifo_tb is
    signal clock       : std_logic := '0';
    signal reset       : std_logic;
    signal rd_en       : std_logic;
    signal rd_en_d     : std_logic;
    signal wr_en       : std_logic;
    signal din         : std_logic_vector(7 downto 0);
    signal dout        : std_logic_vector(7 downto 0);
    signal flush       : std_logic;
    signal full        : std_logic;
    signal almost_full : std_logic;
    signal empty       : std_logic;
    signal valid       : std_logic;
    signal count       : integer range 0 to 15;
begin
    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_fifo: entity work.sync_fifo
    generic map (
        g_depth        => 15,
        g_data_width   => 8,
        g_threshold    => 12,
        g_fall_through => false
    )
    port map (
        clock          => clock,
        reset          => reset,
        rd_en          => rd_en,
        wr_en          => wr_en,
        din            => din,
        dout           => dout,
        flush          => flush,
        full           => full,
        almost_full    => almost_full,
        empty          => empty,
        valid          => valid,
        count          => count
    );
    
    process
    begin
        din <= X"00";
        flush <= '0';
        wr_en <= '0';
        wait until reset = '0';
        for i in 1 to 20 loop
            wait until clock = '1';
            wait until clock = '1';
            while full = '1' loop
                wait until clock = '1';
            end loop;             
            wr_en <= '1';
            din <= std_logic_vector(to_unsigned(i, 8));
            wait until clock = '1';
            wr_en <= '0';
        end loop;
        wait until clock = '1';

        for i in 21 to 50 loop
            while full = '1' loop
                wait until clock = '1';
            end loop;             
            wr_en <= '1';
            din <= std_logic_vector(to_unsigned(i, 8));
            wait until clock = '1';
            wr_en <= '0';
        end loop;
        wait until empty = '1';
        for i in 51 to 60 loop
            wait until clock = '1';
            wait until clock = '1';
            wr_en <= '1';
            din <= std_logic_vector(to_unsigned(i, 8));
            wait until clock = '1';
            wr_en <= '0';
        end loop;
        wait;
    end process;

    process
    begin
        rd_en <= '0';
        wait until reset = '0';
        wait until full = '1';
        wait until clock = '1';
        wait until clock = '1';

        for i in 1 to 5 loop
            wait until clock = '1';
            wait until clock = '1';
            wait until clock = '1';
            rd_en <= '1';
            wait until clock = '1';
            rd_en <= '0';
        end loop;

        rd_en <= '1';
        wait;
    end process;
    
    process(clock)
        variable expect : natural := 0;
    begin
        if rising_edge(clock) then
            rd_en_d <= rd_en;
        elsif falling_edge(clock) then
            if rd_en_d = '1' and valid = '1' then
                expect := expect + 1;
                assert dout = std_logic_vector(to_unsigned(expect, 8)) report "Unexpected data" severity error;
            end if;
        end if;        
    end process;
    
end architecture;
