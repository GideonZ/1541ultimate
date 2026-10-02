--------------------------------------------------------------------------------
-- tb_wd177x_lockstep -- with bit 2 of register 0 clear, the wd177x block has to
-- behave exactly as master's does, cycle for cycle.
--
-- Two instances run side by side on the same clock and the same random
-- stimulus: the block under test and wd177x_ref, master's file with the entity
-- renamed. The drive CPU side writes and reads all four registers, the
-- application side writes and reads all sixteen with bit 2 of register 0 kept
-- clear, and each instance has its own memory responder answering the same way.
-- Every output is compared on every cycle.
--
-- A mostly random stream rarely completes a DMA transfer, so a share of the
-- stimulus is a scripted transfer: a write or a read of one to four bytes
-- through the data register. The run reports how many writes and reads reached memory,
-- and fails if there were none.
--
-- g_negative_control gives the reference a write delay one tick shorter. Then
-- the run must find a difference, which shows the comparison can see one.
--
-- Run under nvc, after analysing mem_bus_pkg, io_bus_pkg, sync_fifo, stepper,
-- wd177x, wd177x_ref and this file with --std=2008 --relaxed:
--   nvc -e tb_wd177x_lockstep -r
--   nvc -e -gg_negative_control=true tb_wd177x_lockstep -r
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity tb_wd177x_lockstep is
    generic (
        g_negative_control : boolean := false;
        g_cycles           : natural := 400000 );
end entity;

architecture tb of tb_wd177x_lockstep is
    constant c_clock_period : time := 20 ns;

    function ref_delay return unsigned is
    begin
        if g_negative_control then
            return X"FE";
        end if;
        return X"FF";
    end function;

    signal clock      : std_logic := '0';
    signal reset      : std_logic := '1';
    signal tick_1kHz  : std_logic := '0';
    signal tick_4MHz  : std_logic := '0';
    signal clock_en   : std_logic := '1';
    signal addr       : unsigned(1 downto 0) := "00";
    signal wen        : std_logic := '0';
    signal ren        : std_logic := '0';
    signal wdata      : std_logic_vector(7 downto 0) := X"00";
    signal motor_en   : std_logic := '1';
    signal stepper_en : std_logic := '0';
    signal cur_track  : unsigned(6 downto 0) := "0000001";
    signal io_req     : t_io_req := c_io_req_init;

    -- outputs, one set per instance
    signal rdata_n, rdata_r       : std_logic_vector(7 downto 0);
    signal mem_req_n, mem_req_r   : t_mem_req;
    signal mem_resp_n, mem_resp_r : t_mem_resp := c_mem_resp_init;
    signal step_n, step_r         : std_logic_vector(1 downto 0);
    signal tout_n, tout_r         : std_logic;
    signal tin_n, tin_r           : std_logic;
    signal io_resp_n, io_resp_r   : t_io_resp;
    signal irq_n, irq_r           : std_logic;

    signal running    : boolean := true;
    signal mismatches : natural := 0;
    signal mem_writes : natural := 0;
    signal mem_reads  : natural := 0;
begin
    clock <= not clock after c_clock_period / 2 when running else '0';

    -- One responder per instance, both answering the same way: acknowledge at
    -- once, and return data that depends on the address.
    process(clock)
        procedure respond(signal req : in t_mem_req; signal resp : out t_mem_resp) is
        begin
            resp <= c_mem_resp_init;
            if req.request = '1' then
                resp.rack     <= '1';
                resp.rack_tag <= req.tag;
                resp.dack_tag <= req.tag;
                resp.data     <= std_logic_vector(req.address(7 downto 0) xor X"A5");
            end if;
        end procedure;
    begin
        if rising_edge(clock) then
            respond(mem_req_n, mem_resp_n);
            respond(mem_req_r, mem_resp_r);
        end if;
    end process;

    i_new: entity work.wd177x
    port map (
        clock => clock, clock_en => clock_en, reset => reset,
        tick_1kHz => tick_1kHz, tick_4MHz => tick_4MHz,
        addr => addr, wen => wen, ren => ren, wdata => wdata, rdata => rdata_n,
        mem_req => mem_req_n, mem_resp => mem_resp_n,
        motor_en => motor_en, stepper_en => stepper_en, step => step_n,
        cur_track => cur_track, do_track_out => tout_n, do_track_in => tin_n,
        io_req => io_req, io_resp => io_resp_n, io_irq => irq_n );

    i_ref: entity work.wd177x_ref
    generic map ( g_write_delay => ref_delay )
    port map (
        clock => clock, clock_en => clock_en, reset => reset,
        tick_1kHz => tick_1kHz, tick_4MHz => tick_4MHz,
        addr => addr, wen => wen, ren => ren, wdata => wdata, rdata => rdata_r,
        mem_req => mem_req_r, mem_resp => mem_resp_r,
        motor_en => motor_en, stepper_en => stepper_en, step => step_r,
        cur_track => cur_track, do_track_out => tout_r, do_track_in => tin_r,
        io_req => io_req, io_resp => io_resp_r, io_irq => irq_r );

    -- Compare every output on every cycle, and count what reached memory.
    process(clock)
    begin
        if falling_edge(clock) and reset = '0' then
            if rdata_n /= rdata_r or mem_req_n /= mem_req_r or step_n /= step_r or
               tout_n /= tout_r or tin_n /= tin_r or io_resp_n /= io_resp_r or
               irq_n /= irq_r then
                if mismatches = 0 then
                    report "first difference at " & time'image(now) severity note;
                end if;
                mismatches <= mismatches + 1;
            end if;
            if mem_req_n.request = '1' then
                if mem_req_n.read_writen = '0' then
                    mem_writes <= mem_writes + 1;
                else
                    mem_reads <= mem_reads + 1;
                end if;
            end if;
        end if;
    end process;

    process
        variable seed1, seed2 : positive := 42;
        variable r : real;

        impure function rnd(n : positive) return natural is
        begin
            uniform(seed1, seed2, r);
            return integer(trunc(r * real(n)));
        end function;

        impure function rnd_byte return std_logic_vector is
        begin
            return std_logic_vector(to_unsigned(rnd(256), 8));
        end function;

        procedure idle_cycle is
        begin
            wait until rising_edge(clock);
            wen <= '0'; ren <= '0';
            io_req.write <= '0'; io_req.read <= '0';
            -- Ticks come at random, far more often than in a real machine, so
            -- the counters that use them run through their whole range.
            tick_4MHz <= '1' when rnd(8) = 0 else '0';
            tick_1kHz <= '1' when rnd(512) = 0 else '0';
        end procedure;

        procedure cpu_write(a : natural; d : std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            addr <= to_unsigned(a, 2); wdata <= d; wen <= '1'; ren <= '0';
            io_req.write <= '0'; io_req.read <= '0';
            tick_4MHz <= '0'; tick_1kHz <= '0';
        end procedure;

        procedure cpu_read(a : natural) is
        begin
            wait until rising_edge(clock);
            addr <= to_unsigned(a, 2); ren <= '1'; wen <= '0';
            io_req.write <= '0'; io_req.read <= '0';
            tick_4MHz <= '0'; tick_1kHz <= '0';
        end procedure;

        procedure app_write(a : natural; d : std_logic_vector(7 downto 0)) is
            variable v : std_logic_vector(7 downto 0) := d;
        begin
            if a = 0 then
                v(2) := '0';   -- bit 2 stays clear: the behaviour master has
            end if;
            wait until rising_edge(clock);
            io_req.address <= to_unsigned(a, 24);
            io_req.data <= v; io_req.write <= '1'; io_req.read <= '0';
            wen <= '0'; ren <= '0';
            tick_4MHz <= '0'; tick_1kHz <= '0';
        end procedure;

        procedure app_read(a : natural) is
        begin
            wait until rising_edge(clock);
            io_req.address <= to_unsigned(a, 24);
            io_req.read <= '1'; io_req.write <= '0';
            wen <= '0'; ren <= '0';
            tick_4MHz <= '0'; tick_1kHz <= '0';
        end procedure;

        -- A write command the way the drive and the firmware carry one out:
        -- command, transfer set up, data bytes, status polled until busy drops,
        -- with random cycles in between.
        procedure scripted_write is
            variable n : natural;
        begin
            cpu_write(0, X"A0" or (rnd_byte and X"1F"));     -- write sector
            n := 1 + rnd(4);
            app_write(16#C#, std_logic_vector(to_unsigned(n, 8)));
            app_write(16#D#, X"00");
            app_write(16#8#, rnd_byte);
            app_write(16#9#, rnd_byte);
            app_write(16#A#, X"00");
            app_write(16#7#, X"02");
            for i in 1 to n loop
                for j in 0 to rnd(40) loop
                    idle_cycle;
                end loop;
                cpu_write(3, rnd_byte);
            end loop;
            for i in 0 to 2000 loop
                if rnd(4) = 0 then
                    cpu_read(0);
                elsif rnd(16) = 0 then
                    app_read(rnd(16));
                else
                    idle_cycle;
                end if;
            end loop;
            app_write(16#6#, X"00");                          -- pop the command
            app_write(16#4#, X"FF");                          -- clear the status
        end procedure;

        -- A read command the same way: the application points the transfer at
        -- memory, and the drive CPU takes the bytes from the data register.
        procedure scripted_read is
            variable n : natural;
        begin
            cpu_write(0, X"80" or (rnd_byte and X"1F"));     -- read sector
            n := 1 + rnd(4);
            app_write(16#C#, std_logic_vector(to_unsigned(n, 8)));
            app_write(16#D#, X"00");
            app_write(16#8#, rnd_byte);
            app_write(16#9#, rnd_byte);
            app_write(16#A#, X"00");
            app_write(16#7#, X"01");
            for i in 0 to 2000 loop
                if rnd(8) = 0 then
                    cpu_read(3);
                elsif rnd(4) = 0 then
                    cpu_read(0);
                elsif rnd(16) = 0 then
                    app_read(rnd(16));
                else
                    idle_cycle;
                end if;
            end loop;
            app_write(16#6#, X"00");
            app_write(16#4#, X"FF");
        end procedure;

        variable cycles : natural := 0;
    begin
        for i in 1 to 10 loop
            idle_cycle;
        end loop;
        reset <= '0';

        while cycles < g_cycles loop
            case rnd(16) is
                when 0 =>
                    if rnd(2) = 0 then
                        scripted_write;
                    else
                        scripted_read;
                    end if;
                    cycles := cycles + 2200;
                when 1 | 2 =>
                    cpu_write(rnd(4), rnd_byte);
                when 3 | 4 | 5 =>
                    cpu_read(rnd(4));
                when 6 | 7 =>
                    app_write(rnd(16), rnd_byte);
                when 8 | 9 =>
                    app_read(rnd(16));
                when 10 =>
                    -- the inputs from the drive mechanics change now and then
                    motor_en <= '1' when rnd(4) /= 0 else '0';
                    stepper_en <= '1' when rnd(2) = 0 else '0';
                    cur_track <= to_unsigned(rnd(84), 7);
                    idle_cycle;
                when others =>
                    idle_cycle;
            end case;
            cycles := cycles + 1;
        end loop;
        idle_cycle;
        idle_cycle;

        report "memory writes " & integer'image(mem_writes) &
               ", memory reads " & integer'image(mem_reads) &
               ", differences " & integer'image(mismatches);
        assert mem_writes > 0 and mem_reads > 0
            report "the stimulus never reached memory" severity failure;
        if g_negative_control then
            assert mismatches > 0
                report "negative control: a reference with a different write delay went unnoticed"
                severity failure;
            report "negative control: the difference was found, as it must be";
        else
            assert mismatches = 0
                report "the block differs from master with bit 2 clear" severity failure;
            report "lock step: no difference from master";
        end if;
        running <= false;
        wait;
    end process;
end architecture;
