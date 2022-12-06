--------------------------------------------------------------------------------
-- Entity: tape_speed_control_tb
-- Date:2016-04-17  
-- Author: Gideon     
--
-- Description: Testbench
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_bfm_pkg.all;
use work.io_bus_pkg.all;

entity tape_loop_tb is

end entity;

architecture arch of tape_loop_tb is
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '0';
    signal phi2_tick: std_logic;
    signal play_req : t_io_req;
    signal play_resp: t_io_resp;
    signal rec_req  : t_io_req;
    signal rec_resp : t_io_resp;
    signal c2n_data : std_logic;
    signal irq      : std_logic;
    signal clock_stop : boolean := false;
begin
    clock <= not clock after 50 ns when not clock_stop;
    reset <= '1', '0' after 250 ns;
    
    i_play: entity work.c2n_playback_io
    generic map (
        g_clock_freq  => 10_000_000
    )
    port map(
        clock         => clock,
        reset         => reset,
        req           => play_req,
        resp          => play_resp,
        generated_tick=> phi2_tick,
        c64_stopped   => '0',
        c2n_motor_in  => '1',
        c2n_motor_out => open,
        c2n_sense_in  => '0',
        c2n_sense_out => open,
        c2n_out_en_w  => open,
        c2n_out_en_r  => open,
        c2n_out_r     => c2n_data,
        c2n_out_w     => open
    );
    
    i_record: entity work.c2n_record
    port map (
        clock         => clock,
        reset         => reset,
        req           => rec_req,
        resp          => rec_resp,
        irq           => irq,
        phi2_tick     => phi2_tick,
        c64_stopped   => '0',
        pull_sense    => open,
        c2n_motor_in  => '1',
        c2n_motor_out => open,
        c2n_sense     => '1',
        c2n_read      => c2n_data,
        c2n_write     => '0'
    );
    
    i_playio: entity work.io_bus_bfm
    generic map (
        g_name => "play"
    )
    port map (
        clock  => clock,
        req    => play_req,
        resp   => play_resp
    );
    
    i_recio: entity work.io_bus_bfm
    generic map (
        g_name => "rec"
    )
    port map (
        clock  => clock,
        req    => rec_req,
        resp   => rec_resp
    );

    -- Bit 0: enable
    -- Bit 1: clear error
    -- Bit 2: flush fifo
    -- Bit 3: mode (0 = tap0, 1 = tap1, with zero extension )
    -- Bit 4: sense_out
    -- Bit 5: speed sel
    -- Bit 7..6: select (00 = none, 01 = neg read, 10 = pos write, 11 = none)

    p_play: process
        variable v_io : p_io_bus_bfm_object;       
    begin
        bind_io_bus_bfm("play", v_io);
        wait for 1 us;
        io_write(v_io, X"0000", X"49");
        wait for 2 us;
        io_write(v_io, X"0800", X"28");
        io_write(v_io, X"0800", X"28");
        io_write(v_io, X"0800", X"28");
        io_write(v_io, X"0800", X"28");
        io_write(v_io, X"0800", X"29");
        io_write(v_io, X"0800", X"28");
        io_write(v_io, X"0800", X"03");
        io_write(v_io, X"0800", X"02");
        wait;
    end process;

    p_rec: process
        variable v_io : p_io_bus_bfm_object;       
        variable received : natural := 0;
        variable data : std_logic_vector(7 downto 0);
    begin
        bind_io_bus_bfm("rec", v_io);
        wait for 1 us;
        io_write(v_io, X"00", X"11"); -- enable falling edge recording
        wait for 2 us;
        while true loop
            io_read(v_io, X"00", data);
            if data(7)='1' then
                io_read(v_io, X"800", data);
                report integer'image(to_integer(unsigned(data)));
                received := received + 1;
                if received = 50 then
                    wait for 10 us;
                    io_write(v_io, X"00", X"00"); -- disable
                    wait;
                end if;
            end if;
        end loop;
        wait;
    end process;
end architecture;
