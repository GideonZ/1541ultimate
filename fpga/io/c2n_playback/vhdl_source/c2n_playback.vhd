library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity c2n_playback is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    phi2_tick       : in  std_logic;
    stream_en       : in  std_logic;
    cmd_write       : in  std_logic;
    fifo_write      : in  std_logic;
    wdata           : in  std_logic_vector(7 downto 0);
    
    status          : out std_logic_vector(7 downto 0);    
    c2n_sense       : out std_logic;
    c2n_out         : out std_logic );

end c2n_playback;

architecture gideon of c2n_playback is
    signal enabled          : std_logic;
    signal counter          : std_logic_vector(23 downto 0);
    signal error            : std_logic;
    signal fifo_dout        : std_logic_vector(7 downto 0);
    signal fifo_read        : std_logic;
    signal fifo_full        : std_logic;
    signal fifo_empty       : std_logic;
    signal fifo_almostfull  : std_logic;
    signal toggle           : std_logic;
    signal cnt2             : integer range 0 to 15;
    
    type t_state is (idle, multi1, multi2, multi3, count_down);
    signal state            : t_state;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            c2n_sense <= not fifo_empty;

            if fifo_empty='1' and enabled='1' then
                error <= '1';
            end if;

            if cnt2 = 0 then
                toggle <= '0';
            elsif phi2_tick='1' then
                cnt2 <= cnt2 - 1;
            end if;

            if cmd_write='1' then
                enabled <= wdata(0);
                if wdata(1)='1' then
                    error <= '0';
                end if;
            end if;

            case state is
            when idle =>
                if enabled='1' and fifo_empty='0' then
                    if fifo_dout=X"00" then
                        state <= multi1;
                    else
                        counter <= "0000000000000" & fifo_dout & "000";
                        state <= count_down;
                    end if;
                end if;
            
            when multi1 =>
                if fifo_empty='0' then
                    counter(7 downto 0) <= fifo_dout;
                    state <= multi2;
                end if;
                
            when multi2 =>
                if fifo_empty='0' then
                    counter(15 downto 8) <= fifo_dout;
                    state <= multi3;
                end if;

            when multi3 =>
                if fifo_empty='0' then
                    counter(23 downto 16) <= fifo_dout;
                    state <= count_down;
                end if;
                
            when count_down =>
                if phi2_tick='1' and stream_en='1' then
                    if counter = 1 then
                        toggle <= '1';
                        cnt2   <= 15;
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
            end if;
        end if;
    end process;
    
    fifo_read <= '0' when state = count_down else (enabled and not fifo_empty);

    fifo: entity work.sync_fifo
    generic map (
        g_depth        => 2048,        -- Actual depth. 
        g_data_width   => 8,
        g_threshold    => 1536,
        g_storage      => "blockram",     -- can also be "blockram" or "distributed"
        g_fall_through => true )
    port map (
        clock       => clock,
        reset       => reset,

        rd_en       => fifo_read,
        wr_en       => fifo_write,

        din         => wdata,
        dout        => fifo_dout,

        flush       => '0',

        full        => fifo_full,
        almost_full => fifo_almostfull,
        empty       => fifo_empty,
        count       => open );

    status(0) <= enabled;
    status(1) <= error;
    status(2) <= fifo_full;
    status(3) <= fifo_almostfull;
    status(4) <= '0';
    status(5) <= '0';
    status(6) <= '0';
    status(7) <= fifo_empty;

    c2n_out   <= not toggle;

end gideon;
