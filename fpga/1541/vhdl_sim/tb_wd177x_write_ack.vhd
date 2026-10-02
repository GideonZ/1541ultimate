--------------------------------------------------------------------------------
-- Checks how the wd177x block ends a write command: the fixed delay it has
-- always used, and the acknowledge the application can ask for instead.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity tb_wd177x_write_ack is
end entity;

architecture tb of tb_wd177x_write_ack is
    constant c_clock_period : time := 20 ns;

    signal clock      : std_logic := '0';
    signal reset      : std_logic := '1';
    signal tick_1kHz  : std_logic := '0';
    signal tick_4MHz  : std_logic := '0';
    signal addr       : unsigned(1 downto 0) := "00";
    signal wen        : std_logic := '0';
    signal ren        : std_logic := '0';
    signal wdata      : std_logic_vector(7 downto 0) := X"00";
    signal rdata      : std_logic_vector(7 downto 0);
    signal mem_req    : t_mem_req;
    signal mem_resp   : t_mem_resp := c_mem_resp_init;
    signal io_req     : t_io_req := c_io_req_init;
    signal io_resp    : t_io_resp;
    signal io_irq     : std_logic;
    signal stop       : boolean := false;
begin
    clock <= not clock after c_clock_period / 2 when not stop else '0';

    -- Coarse but adequate: one 4 MHz tick every 250 ns, one 1 kHz tick every ms.
    process
    begin
        while not stop loop
            wait for 250 ns - c_clock_period;
            tick_4MHz <= '1';
            wait for c_clock_period;
            tick_4MHz <= '0';
        end loop;
        wait;
    end process;

    process
    begin
        while not stop loop
            wait for 1 ms - c_clock_period;
            tick_1kHz <= '1';
            wait for c_clock_period;
            tick_1kHz <= '0';
        end loop;
        wait;
    end process;

    -- Memory that acknowledges everything at once.
    process(clock)
    begin
        if rising_edge(clock) then
            mem_resp <= c_mem_resp_init;
            if mem_req.request = '1' then
                mem_resp.rack     <= '1';
                mem_resp.rack_tag <= mem_req.tag;
                mem_resp.dack_tag <= mem_req.tag;
                mem_resp.data     <= X"4E";
            end if;
        end if;
    end process;

    i_mut: entity work.wd177x
    port map (
        clock        => clock,
        clock_en     => '1',
        reset        => reset,
        tick_1kHz    => tick_1kHz,
        tick_4MHz    => tick_4MHz,
        addr         => addr,
        wen          => wen,
        ren          => ren,
        wdata        => wdata,
        rdata        => rdata,
        mem_req      => mem_req,
        mem_resp     => mem_resp,
        motor_en     => '1',
        stepper_en   => '0',
        step         => open,
        cur_track    => "0000001",
        do_track_out => open,
        do_track_in  => open,
        io_req       => io_req,
        io_resp      => io_resp,
        io_irq       => io_irq );

    process
        -- The drive CPU side
        procedure cpu_write(a : unsigned(1 downto 0); d : std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            addr <= a; wdata <= d; wen <= '1';
            wait until rising_edge(clock);
            wen <= '0';
        end procedure;

        procedure cpu_status(variable s : out std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            addr <= "00"; ren <= '1';
            wait until rising_edge(clock);
            ren <= '0';
            s := rdata;
        end procedure;

        -- The application side
        procedure app_write(a : unsigned(3 downto 0); d : std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            io_req.address <= X"00000" & a;
            io_req.data <= d;
            io_req.write <= '1';
            wait until rising_edge(clock);
            io_req.write <= '0';
        end procedure;

        -- The application acknowledges a finished write the way the firmware
        -- does: through register 2, with lost data in bit 2 if it could not
        -- store it.
        procedure app_acknowledge(lost : boolean) is
        begin
            if lost then
                app_write(X"2", X"04");
            else
                app_write(X"2", X"00");
            end if;
        end procedure;

        procedure feed_two_bytes is
        begin
            app_write(X"8", X"00");      -- transfer address
            app_write(X"9", X"10");
            app_write(X"A", X"00");
            app_write(X"C", X"02");      -- two bytes
            app_write(X"D", X"00");
            app_write(X"7", X"02");      -- dma mode: write, to the application
            cpu_write("11", X"4E");
            wait for 2 us;
            cpu_write("11", X"4E");
        end procedure;

        variable st : std_logic_vector(7 downto 0);
    begin
        wait for 200 ns;
        reset <= '0';
        wait for 200 ns;

        ---------------------------------------------------------------------
        report "case 1: no acknowledge asked for, the block ends the command";
        cpu_write("00", X"F8");          -- write track
        cpu_status(st);
        assert st(0) = '1' report "case 1: busy should be set after the command" severity failure;
        feed_two_bytes;
        wait for 200 us;
        cpu_status(st);
        assert st(0) = '0' report "case 1: busy should be clear again" severity failure;

        ---------------------------------------------------------------------
        report "case 2: acknowledge asked for, the application ends the command";
        app_write(X"0", X"04");          -- bit 2: acknowledge write completions
        cpu_write("00", X"F8");
        feed_two_bytes;
        wait for 2 ms;
        cpu_status(st);
        assert st(0) = '1' report "case 2: busy should still be set, nothing acknowledged" severity failure;
        app_acknowledge(true);
        wait for 10 us;
        cpu_status(st);
        assert st(0) = '0' report "case 2: busy should be clear after the acknowledge" severity failure;
        assert st(2) = '1' report "case 2: lost data should have survived until the drive read it" severity failure;

        ---------------------------------------------------------------------
        report "case 3: acknowledge asked for but never given, the fallback ends it";
        app_write(X"4", X"04");          -- clear the lost data bit again
        cpu_write("00", X"F8");
        feed_two_bytes;
        wait for 100 ms;
        cpu_status(st);
        assert st(0) = '1' report "case 3: busy should still be set at 100 ms" severity failure;
        wait for 200 ms;
        cpu_status(st);
        assert st(0) = '0' report "case 3: the fallback should have ended the command" severity failure;

        ---------------------------------------------------------------------
        report "case 4: the fallback fired, a new command started, then the late acknowledge";
        -- Case 3 ended on the fallback. The drive CPU now issues its next
        -- command, and only then does the application's acknowledge for the
        -- write arrive. It belongs to the finished write and must not end the
        -- new command.
        cpu_write("00", X"88");          -- read sector
        cpu_status(st);
        assert st(0) = '1' report "case 4: busy should be set for the new command" severity failure;
        app_acknowledge(false);
        wait for 10 us;
        cpu_status(st);
        assert st(0) = '1' report "case 4: a late acknowledge ended the next command" severity failure;

        report "all cases passed";
        stop <= true;
        wait;
    end process;
end architecture;
