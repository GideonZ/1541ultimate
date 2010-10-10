library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- LUT/FF/S3S/BRAM: 242/130/136/1

library work;
use work.io_bus_pkg.all;

entity c2n_record is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    req             : in  t_io_req;
    resp            : out t_io_resp;
    
    phi2_tick       : in  std_logic;
    c64_stopped		: in  std_logic;
    
    pull_sense      : out std_logic;
    c2n_motor       : in  std_logic;
    c2n_sense       : in  std_logic;
    c2n_read        : in  std_logic;
    c2n_write       : in  std_logic );

end c2n_record;

architecture gideon of c2n_record is
    signal stream_en        : std_logic;
    signal mode             : std_logic_vector(1 downto 0);
    signal sel              : std_logic;
    signal read_s           : std_logic;
    signal read_c           : std_logic;
    signal read_d           : std_logic;
    signal read_event       : std_logic;
    signal enabled          : std_logic;
    signal counter          : unsigned(23 downto 0);
    signal diff             : unsigned(23 downto 0);
    signal remain           : unsigned(2 downto 0);
    signal error            : std_logic;
    signal status           : std_logic_vector(7 downto 0);
    signal fifo_din         : std_logic_vector(7 downto 0);
    signal fifo_dout        : std_logic_vector(7 downto 0);
    signal fifo_read        : std_logic;
    signal fifo_full        : std_logic;
    signal fifo_empty       : std_logic;
    signal fifo_almostfull  : std_logic;
    signal fifo_flush       : std_logic;
    signal fifo_write       : std_logic;
    signal toggle           : std_logic;
    signal cnt2             : integer range 0 to 63;
    type t_state is (idle, listen, encode, multi1, multi2, multi3);
    signal state            : t_state;
    signal state_enc        : std_logic_vector(1 downto 0);
    
    attribute register_duplication  : string;
    attribute register_duplication of stream_en : signal is "no";
    attribute register_duplication of read_c : signal is "no";
    
begin
    pull_sense <= sel and enabled;
    
    filt: entity work.spike_filter generic map (10) port map(clock, read_s, read_c);

    process(clock)
        variable v_diff : unsigned(10 downto 0);
    begin
        if rising_edge(clock) then

            if fifo_full='1' and enabled='1' then
                error <= '1';
            end if;

            -- signal capture
            stream_en <= c2n_sense and enabled and c2n_motor;

            read_s <= (c2n_read and not sel) or (c2n_write and sel);
            read_d <= read_c;
            case mode is
                when "00" =>
                    read_event <= read_c and not read_d; -- rising edge
                when "01" =>
                    read_event <= not read_c and read_d; -- falling edge
                when others =>
                    read_event <= read_c xor read_d; -- both edges
            end case;

            -- filter for false pulses
--            if counter(23 downto 4) = X"00000" then
--                read_event <= '0';
--            end if;

            -- bus handling
            resp <= c_io_resp_init;
            if req.write='1' then
                resp.ack <= '1'; -- ack for fifo write as well.
                if req.address(11)='0' then
                    enabled <= req.data(0);
                    if req.data(0)='0' and enabled='1' then
                        read_event <= '1';
                    end if;
                    
                    if req.data(1)='1' then
                        error <= '0';
                    end if;
                    fifo_flush <= req.data(2);
                    mode <= req.data(5 downto 4);
                    sel <= req.data(6);
                end if;
            elsif req.read='1' then
                resp.ack <= '1';
                if req.address(11)='0' then
                    resp.data <= status;
                else
                    resp.data <= fifo_dout;
                end if;
            end if;

            -- listening process
            if stream_en='1' then
                if phi2_tick='1' then
                    counter <= counter + 1;
                end if;
            else
                counter <= (others => '0');
            end if;

            fifo_write <= '0';

            case state is
            when idle =>
                if stream_en='1' then
                    state <= listen;
                end if;
                    
            when listen =>
                if read_event='1' then
                    diff       <= counter;
                    if phi2_tick='1' then
                        counter <= to_unsigned(1, counter'length);
                    else
                        counter <= to_unsigned(0, counter'length);
                    end if;
                    state      <= encode;

                elsif enabled='0' then
                    state <= idle;
                end if;
            
            when encode =>
                fifo_write <= '1';
                if diff > 2040 then
                    fifo_din <= X"00";
                    state <= multi1;
                else
                    v_diff := diff(10 downto 0) + remain;
                    if v_diff(10 downto 3) = X"00" then
                        fifo_din <= X"01";
                    else
                        fifo_din <= std_logic_vector(v_diff(10 downto 3));
                    end if;
                    remain <= v_diff(2 downto 0);
                    state <= listen;
                end if;

            when multi1 =>
                fifo_din <= std_logic_vector(diff(7 downto 0));
                fifo_write <= '1';
                state <= multi2;
                
            when multi2 =>
                fifo_din <= std_logic_vector(diff(15 downto 8));
                fifo_write <= '1';
                state <= multi3;
                
            when multi3 =>
                fifo_din <= std_logic_vector(diff(23 downto 16));
                fifo_write <= '1';
                state <= listen;
            
            when others =>
                null;

            end case;

            if reset='1' then
                fifo_din <= (others => '0');
                enabled  <= '0';
                counter  <= (others => '0');
                toggle   <= '0';
                error    <= '0';
                mode     <= "00";
                sel      <= '0';
                remain   <= "000";
            end if;
        end if;
    end process;
    
    fifo_read  <= '1' when req.read='1' and req.address(11)='1' else '0';

    fifo: entity work.sync_fifo
    generic map (
        g_depth        => 2048,        -- Actual depth. 
        g_data_width   => 8,
        g_threshold    => 512,
        g_storage      => "block",     
        g_fall_through => true )
    port map (
        clock       => clock,
        reset       => reset,

        rd_en       => fifo_read,
        wr_en       => fifo_write,

        din         => fifo_din,
        dout        => fifo_dout,

        flush       => fifo_flush,

        full        => fifo_full,
        almost_full => fifo_almostfull,
        empty       => fifo_empty,
        count       => open );

    status(0) <= enabled;
    status(1) <= error;
    status(2) <= fifo_full;
    status(3) <= fifo_almostfull;
    status(4) <= state_enc(0);
    status(5) <= state_enc(1);
    status(6) <= stream_en;
    status(7) <= not fifo_empty;

    with state select state_enc <=
        "00" when idle,
        "01" when multi1,
        "01" when multi2,
        "01" when multi3,
        "10" when listen,
        "11" when others;
        
end gideon;
