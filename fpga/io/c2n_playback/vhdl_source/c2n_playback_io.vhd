library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity c2n_playback_io is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    req             : in  t_io_req;
    resp            : out t_io_resp;
    
    phi2_tick       : in  std_logic;
    c64_stopped		: in  std_logic;
    
    c2n_motor       : in  std_logic;
    c2n_sense       : out std_logic;
    c2n_out_r       : out std_logic;
    c2n_out_w       : out std_logic );

end c2n_playback_io;

architecture gideon of c2n_playback_io is
    signal enabled          : std_logic;
    signal counter          : unsigned(23 downto 0);
    signal error            : std_logic;
    signal status           : std_logic_vector(7 downto 0);
    signal fifo_dout        : std_logic_vector(7 downto 0);
    signal fifo_read        : std_logic;
    signal fifo_full        : std_logic;
    signal fifo_empty       : std_logic;
    signal fifo_almostfull  : std_logic;
    signal fifo_flush       : std_logic;
    signal fifo_write       : std_logic;
    signal toggle           : std_logic;
    signal cnt2             : integer range 0 to 63;
    signal stream_en        : std_logic;
    type t_state is (idle, multi1, multi2, multi3, count_down);
    signal state            : t_state;
    signal state_enc        : std_logic_vector(1 downto 0);
    signal mode             : std_logic;
    signal sel              : std_logic;
    signal c2n_out          : std_logic;
    
    attribute register_duplication  : string;
    attribute register_duplication of stream_en : signal is "no";
    
begin
    process(clock)
    begin
        if rising_edge(clock) then
            -- c2n pin out and sync
            c2n_sense <= enabled and not fifo_empty;
            stream_en <= enabled and c2n_motor;

            if fifo_empty='1' and enabled='1' then
                error <= '1';
            end if;

            -- create a pulse of 16 ticks
            if cnt2 = 0 then
                toggle <= '0';
            elsif phi2_tick='1' then
                cnt2 <= cnt2 - 1;
            end if;

            -- bus handling
            resp <= c_io_resp_init;
            if req.write='1' then
                resp.ack <= '1'; -- ack for fifo write as well.
                if req.address(11)='0' then
                    enabled <= req.data(0);
                    if req.data(1)='1' then
                        error <= '0';
                    end if;
                    fifo_flush <= req.data(2);
                    mode <= req.data(3);
                    sel  <= req.data(6);
                end if;
            elsif req.read='1' then
                resp.ack <= '1';
                resp.data <= status;
            end if;

            case state is
            when idle =>
                if enabled='1' and fifo_empty='0' then
                    if fifo_dout=X"00" then
                        if mode='1' then
                            state <= multi1;
                        else
                            counter <= to_unsigned(256*8, counter'length);
                            state <= count_down;
                        end if;                    
                    else
                        counter <= unsigned("0000000000000" & fifo_dout & "000");
                        state <= count_down;
                    end if;
                end if;
            
            when multi1 =>
                if fifo_empty='0' then
                    counter(7 downto 0) <= unsigned(fifo_dout);
                    state <= multi2;
                elsif enabled = '0' then
                    state <= idle;
                end if;
                
            when multi2 =>
                if fifo_empty='0' then
                    counter(15 downto 8) <= unsigned(fifo_dout);
                    state <= multi3;
                elsif enabled = '0' then
                    state <= idle;
                end if;

            when multi3 =>
                if fifo_empty='0' then
                    counter(23 downto 16) <= unsigned(fifo_dout);
                    state <= count_down;
                elsif enabled = '0' then
                    state <= idle;
                end if;
                
            when count_down =>
                if phi2_tick='1' and stream_en='1' and c64_stopped='0' then
                    if (counter = 1) or (counter = 0) then
                        toggle <= '1';
                        cnt2   <= 49;
                        state  <= idle;
                    else
                        counter <= counter - 1;
                    end if;
                elsif enabled = '0' then
                    state <= idle;
                end if;

            when others =>
                null;

            end case;

            if reset='1' then
                enabled <= '0';
                counter <= (others => '0');
                toggle  <= '0';
                error   <= '0';
                mode    <= '0';
            end if;
        end if;
    end process;
    
    fifo_write <= req.write and req.address(11); -- 0x800-0xFFF (2K) 
    fifo_read  <= '0' when state = count_down else (enabled and not fifo_empty);

    fifo: entity work.sync_fifo
    generic map (
        g_depth        => 2048,        -- Actual depth. 
        g_data_width   => 8,
        g_threshold    => 1536,
        g_storage      => "block",     
        g_fall_through => true )
    port map (
        clock       => clock,
        reset       => reset,

        rd_en       => fifo_read,
        wr_en       => fifo_write,

        din         => req.data,
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
    status(7) <= fifo_empty;

    c2n_out   <= not toggle;

    c2n_out_r <= c2n_out when sel='0' else '1';
    c2n_out_w <= c2n_out when sel='1' else '1';

    with state select state_enc <=
        "00" when idle,
        "01" when multi1,
        "01" when multi2,
        "01" when multi3,
        "10" when count_down,
        "11" when others;
        
end gideon;
