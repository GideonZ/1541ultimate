library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity clock_measure is
generic (
    g_divider		: natural range 1 to 64*1024*1024-1 := 50*65536
);
port (
    sys_clock   : in  std_logic;
    sys_reset   : in  std_logic;
    io_req      : in  t_io_req := c_io_req_init;
    io_resp     : out t_io_resp;

    ext_tick    : in  std_logic := '0';
    timer_out   : out std_logic;
    meas_clock  : in  std_logic
);
end entity;

architecture gideon of clock_measure is
    signal sys_measure_pulse    : std_logic;
    signal measure_pulse        : std_logic;
    signal meas_counter         : unsigned(23 downto 0);
    signal clock_freq           : std_logic_vector(23 downto 0);
    signal clock_freq_valid     : std_logic;
    signal valid                : std_logic;
    signal ext_tick_c           : std_logic;
    signal ext_tick_d1          : std_logic;
    signal ext_tick_d2          : std_logic;
    signal toggle               : std_logic;
begin
    timer_out <= toggle;

    process(sys_clock)
        variable v_div  : natural range 0 to g_divider-1;
    begin
        if rising_edge(sys_clock) then
            ext_tick_c  <= ext_tick;
            ext_tick_d1 <= ext_tick_c;
            ext_tick_d2 <= ext_tick_d1;

            sys_measure_pulse <= '0';
            if sys_reset = '1' then
                v_div := g_divider-1;
                toggle <= '0';
            elsif g_divider = 1 then
                sys_measure_pulse <= ext_tick_d2 xor ext_tick_d1;
            elsif v_div = 0 then
                sys_measure_pulse <= '1';
                toggle <= not toggle;
                v_div := g_divider-1;
            else
                v_div := v_div - 1;
            end if;
        end if;
    end process;

    i_meas_pulse: entity work.pulse_synchronizer
    port map (
        clock_in  => sys_clock,
        pulse_in  => sys_measure_pulse,
        clock_out => meas_clock,
        pulse_out => measure_pulse
    );

    process(meas_clock)
    begin
        if rising_edge(meas_clock) then
            if measure_pulse = '1' then
                meas_counter <= (0 => '1', others => '0');
            else
                meas_counter <= meas_counter + 1;
            end if;
        end if;
    end process;

    i_meas_synchronizer: entity work.synchronizer_gzw
    generic map (
        g_width => meas_counter'length,
        g_fast  => false
    )
    port map (
        tx_clock    => meas_clock,
        tx_push     => measure_pulse,
        tx_data     => std_logic_vector(meas_counter),
        tx_done     => open,
        rx_clock    => sys_clock,
        rx_new_data => clock_freq_valid,
        rx_data     => clock_freq
    );

    process(sys_clock)
        variable v_addr : natural range 0 to 3;
    begin
        if rising_edge(sys_clock) then
            io_resp <= c_io_resp_init;
            v_addr := to_integer(io_req.address(1 downto 0));
            if io_req.read = '1' then
                io_resp.ack <= '1';
                case v_addr is
                when 0 =>
                    io_resp.data <= clock_freq(7 downto 0);
                when 1 =>
                    io_resp.data <= clock_freq(15 downto 8);
                when 2 =>
                    io_resp.data <= clock_freq(23 downto 16);
                when 3 =>
                    io_resp.data(0) <= valid;
                    valid <= '0';
                end case;
            elsif io_req.write = '1' then
                io_resp.ack <= '1';
            end if;
            if clock_freq_valid = '1' then
                valid <= '1';
            end if;
            if sys_reset = '1' then
                valid <= '0';
            end if;
        end if;
    end process;
end architecture;
