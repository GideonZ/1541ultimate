library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;

entity i2c_master is
    generic (
        g_divisor       : natural := 50_000_000 / 400_000
    );
    port (
        clock               : in  std_logic;
        reset               : in  std_logic;

        io_req              : in  t_io_req;
        io_resp             : out t_io_resp;

        scl_o               : out std_logic;
        scl_i               : in  std_logic;
        sda_o               : out std_logic;
        sda_i               : in  std_logic
    );
end entity;

architecture behavioral of i2c_master is
    constant c_filter_length    : natural := 7;
    
    type t_state is (idle, startcond, tx_byte, rx_byte, check_ack, ack_rx, stop, repeated_start);
    signal data_out         : std_logic_vector(7 downto 0);
    signal state            : t_state;
    signal scl_o_i          : std_logic;
    signal sda_o_i          : std_logic;
    signal stretch          : std_logic;
    signal error            : std_logic;
    signal started          : std_logic;
    signal do_ack           : std_logic;
    signal bit_count        : integer range 0 to 7;
    signal tick             : std_logic;
    signal divisor          : integer range 0 to g_divisor;
       
    signal scl_count        : integer range 0 to c_filter_length-1;
    signal sda_count        : integer range 0 to c_filter_length-1;
    signal scl_in           : std_logic;
    signal scl_in_d         : std_logic;
    signal sda_in           : std_logic;
    signal sda_in_d         : std_logic;
    signal sda_c            : std_logic;
    signal scl_c            : std_logic;
    
    constant c_scl_start    : std_logic_vector(3 downto 0) := "1110";
    constant c_sda_start    : std_logic_vector(3 downto 0) := "1100";

    constant c_scl_rstart   : std_logic_vector(3 downto 0) := "0110";
    constant c_sda_rstart   : std_logic_vector(3 downto 0) := "1100";

    constant c_scl_stop     : std_logic_vector(3 downto 0) := "0111";
    constant c_sda_stop     : std_logic_vector(3 downto 0) := "0011";

    constant c_scl_data     : std_logic_vector(3 downto 0) := "0110";

    signal soft_reset       : std_logic;
    signal missed_write     : std_logic;
    signal do_seq           : std_logic;
    signal seq_busy         : std_logic;
    signal seq_done         : std_logic;
    signal cycle_cnt        : natural range 0 to 3;
    signal sda_queue        : std_logic_vector(3 downto 0);
    signal scl_queue        : std_logic_vector(3 downto 0);
    signal sampled          : std_logic;
begin
    -- generate processing tick
    process(clock)
    begin
        if rising_edge(clock) then
            tick <= '0';
            if stretch = '1' then
                divisor <= g_divisor;
            elsif divisor = 0 then
                divisor <= g_divisor;
                tick <= '1';
            else
                divisor <= divisor - 1;
            end if;
        end if;
    end process;

    stretch <= '1' when (state = idle) else
               '1' when (scl_o_i = '1' and scl_c = '0') else
               '0';

    p_input: process(clock)
    begin
        if rising_edge(clock) then
            -- dual flipflop for synchronization
            sda_in <= to_x01(sda_i);
            scl_in <= to_x01(scl_i);
            sda_in_d <= sda_in;
            scl_in_d <= scl_in;

            -- filtering SCL
            if scl_in_d = '0' then
                if scl_count = 0 then
                    scl_c <= '0';
                else
                    scl_count <= scl_count - 1;
                end if;
            else
                if scl_count = c_filter_length-1 then
                    scl_c <= '1';
                else
                    scl_count <= scl_count + 1;
                end if;
            end if;

            -- filtering SDA
            if sda_in_d = '0' then
                if sda_count = 0 then
                    sda_c <= '0';
                else
                    sda_count <= sda_count - 1;
                end if;
            else
                if sda_count = c_filter_length-1 then
                    sda_c <= '1';
                else
                    sda_count <= sda_count + 1;
                end if;
            end if;

            if reset = '1' then
                sda_in <= '1';
                scl_in <= '1';
                sda_in_d <= '1';
                scl_in_d <= '1';
                sda_count <= 4;
                scl_count <= 4;
                sda_c <= '1';
                scl_c <= '1';
            end if;            
        end if;
    end process;
    
    -- Sequence generator
    p_sequence: process(clock)
    begin
        if rising_edge(clock) then
            seq_done <= '0';
            if seq_busy = '0' then
                cycle_cnt <= 3;
                if do_seq = '1' then
                    seq_busy <= '1';
                end if;
            else -- busy
                scl_o_i <= scl_queue(cycle_cnt);
                sda_o_i <= sda_queue(cycle_cnt);
                if tick = '1' then
                    if cycle_cnt = 0 then
                        seq_busy <= '0';
                        seq_done <= '1';
                    else
                        cycle_cnt <= cycle_cnt - 1;
                    end if;
                    if cycle_cnt = 2 then
                        sampled <= sda_c;
                    end if;
                end if;
            end if;

            if reset = '1' or soft_reset = '1' then
                seq_busy <= '0';
                sampled <= '0';
                scl_o_i <= '1';
                sda_o_i <= '1';
            end if;
        end if;
    end process;
    
    p_logic: process(clock)
        variable v_addr : unsigned(3 downto 0);
    begin
        if rising_edge(clock) then
            v_addr := c_io_req_init.address(3 downto 0);
            do_seq <= '0';
            soft_reset <= '0';
            io_resp <= c_io_resp_init;

            case state is
            when idle =>
                do_ack <= '0';
                if io_req.write = '1' then
                    io_resp.ack <= '1';
                    case v_addr is
                    when X"0" =>
                        bit_count <= 7; 
                        data_out  <= io_req.data;
                        do_ack <= '0';
                        if started = '1' then
                            state <= tx_byte;
                        else
                            scl_queue <= c_scl_start;
                            sda_queue <= c_sda_start;
                            do_seq <= '1';
                            state <= startcond;
                        end if;

                    when X"2" =>
                        state <= repeated_start;
                    
                    when X"3" =>
                        state <= stop;

                    when X"4" =>
                        state <= rx_byte;
                        do_ack <= '0';
                    
                    when X"5" =>
                        state <= rx_byte;
                        do_ack <= '1';

                    when X"7" =>
                        soft_reset <= io_req.data(0);

                    when others =>
                        null;
                    end case;
                end if;

            when startcond =>
                if seq_done = '1' then
                    started <= '1';
                    state <= tx_byte;
                end if;
                
            when tx_byte =>
                if seq_done = '1' then
                    if bit_count = 0 then
                        state <= check_ack;
                    else
                        bit_count <= bit_count - 1;
                    end if;
                elsif seq_busy = '0' and do_seq = '0' then
                    scl_queue <= c_scl_data;
                    sda_queue <= (others => data_out(7));
                    data_out <= data_out(6 downto 0) & '1';
                    do_seq <= '1';
                end if;

            when check_ack =>
                if seq_done = '1' then
                    if sampled = '1' then -- not acknowledged
                        error <= '1';
                        state <= stop;
                    else
                        state <= idle;
                    end if;
                elsif seq_busy = '0' and do_seq = '0' then
                    do_seq <= '1';
                    scl_queue <= c_scl_data;
                    sda_queue <= "1111";
                end if;
                
            when rx_byte =>
                if seq_busy = '0' and do_seq = '0' then
                    scl_queue <= c_scl_data;
                    sda_queue <= "1111";
                    data_out(7 downto 1) <= data_out(6 downto 0);
                    do_seq <= '1';
                end if;
                if seq_done = '1' then
                    data_out(0) <= sampled;
                    if bit_count = 0 then
                        state <= ack_rx;
                        scl_queue <= c_scl_data;
                        sda_queue <= (others => not do_ack);
                        do_seq <= '1';
                    else
                        bit_count <= bit_count - 1;
                    end if;
                end if;                    

            when ack_rx =>
                if seq_done = '1' then
                    state <= idle;
                end if;
                
            when stop =>
                if seq_done = '1' then
                    started <= '0';
                    state <= idle;
                elsif seq_busy = '0' and do_seq = '0' then
                    do_seq <= '1';
                    scl_queue <= c_scl_stop;
                    sda_queue <= c_sda_stop;
                end if;
            
            when repeated_start =>
                if seq_done = '1' then
                    state <= idle;
                elsif seq_busy = '0' and do_seq = '0' then
                    do_seq <= '1';
                    scl_queue <= c_scl_rstart;
                    sda_queue <= c_sda_rstart;
                end if;

            end case;

            if state /= idle then
                if io_req.write = '1' then
                    io_resp.ack <= '1';
                    missed_write <= '1';
                end if;
            end if;

            if io_req.read = '1' then
                io_resp.ack <= '1';
                case v_addr is
                when X"0" | X"4" | X"5" =>
                    io_resp.data <= data_out;
                when X"1" =>
                    io_resp.data(0) <= started;
                    io_resp.data(2) <= error;
                    io_resp.data(3) <= missed_write;
                    error <= '0';
                    missed_write <= '0';
                    if state = idle then
                        io_resp.data(7) <= '0';
                    else
                        io_resp.data(7) <= '1'; -- busy!
                    end if;
                when others =>
                    null;
                end case;
            end if;                

            if reset = '1' or soft_reset = '1' then
                do_ack <= '0';
                missed_write <= '0';
                started <= '0';
                state <= idle;
            end if;
        end if;
    end process;

    scl_o <= scl_o_i;
    sda_o <= sda_o_i;
end architecture;

