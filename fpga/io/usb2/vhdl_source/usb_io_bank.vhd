library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity usb_io_bank is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    -- i/o interface
    io_addr     : in  unsigned(7 downto 0);
    io_write    : in  std_logic;
    io_read     : in  std_logic;
    io_wdata    : in  std_logic_vector(15 downto 0);
    io_rdata    : out std_logic_vector(15 downto 0);
    stall       : out std_logic;
    
    -- Memory controller and buffer
    mem_ready       : in  std_logic;
    transferred     : in  unsigned(10 downto 0);
    
    -- Register access
    reg_read        : out std_logic := '0';
    reg_write       : out std_logic := '0';
    reg_ack         : in  std_logic;
    reg_addr        : out std_logic_vector(5 downto 0) := (others => '0');  
    reg_wdata       : out std_logic_vector(7 downto 0) := X"00";  
    reg_rdata       : in  std_logic_vector(7 downto 0);
    status          : in  std_logic_vector(7 downto 0);
    
    -- I/O pins from RX
    rx_pid          : in  std_logic_vector(3 downto 0);
    rx_token        : in  std_logic_vector(10 downto 0);
    rx_valid_token  : in  std_logic;
    rx_valid_handsh : in  std_logic;
    rx_valid_packet : in  std_logic;
    rx_error        : in  std_logic;

    -- I/O pins to TX
    tx_pid          : out std_logic_vector(3 downto 0);
    tx_token        : out std_logic_vector(10 downto 0);
    tx_send_token   : out std_logic;
    tx_send_handsh  : out std_logic;
    tx_send_data    : out std_logic;
    tx_length       : out unsigned(10 downto 0);
    tx_no_data      : out std_logic;
    tx_chirp_enable : out std_logic;
    tx_chirp_level  : out std_logic;
    tx_chirp_end    : out std_logic;
    tx_chirp_start  : out std_logic;
    tx_ack          : in  std_logic );

end entity;

architecture gideon of usb_io_bank is
    signal pulse_in  : std_logic_vector(15 downto 0) := (others => '0');
    signal pulse_out : std_logic_vector(15 downto 0) := (others => '0');
    signal latched   : std_logic_vector(15 downto 0) := (others => '0');
    signal level_out : std_logic_vector(15 downto 0) := (others => '0');
    signal frame_div : integer range 0 to 65535;
    signal frame_cnt : unsigned(13 downto 0) := (others => '0');
    signal stall_i   : std_logic := '0';
    signal ulpi_access : std_logic;
    signal tx_chirp_start_i : std_logic;
    signal tx_chirp_end_i   : std_logic;
    signal filter_cnt   : unsigned(7 downto 0);
    signal filter_st1   : std_logic;
    signal reset_filter : std_logic;
begin
    pulse_in(0) <= rx_error;
    pulse_in(1) <= rx_valid_token;
    pulse_in(2) <= rx_valid_handsh;
    pulse_in(3) <= rx_valid_packet;
    pulse_in(4) <= rx_valid_packet or rx_valid_handsh or rx_valid_token or rx_error;
    pulse_in(7) <= tx_ack; -- tx ack resets lower half of output pulses
        
    tx_no_data     <= level_out(0);
    tx_send_token  <= pulse_out(0);
    tx_send_handsh <= pulse_out(1);
    tx_send_data   <= pulse_out(2);
    tx_chirp_level   <= level_out(5);
    tx_chirp_start   <= tx_chirp_start_i;
    tx_chirp_end     <= tx_chirp_end_i;

    tx_chirp_start_i <= pulse_out(8);
    tx_chirp_end_i   <= pulse_out(9);
    reset_filter     <= pulse_out(14);
    pulse_in(14)     <= filter_st1;
    pulse_in(13)     <= '1' when (status(5 downto 4) = "10") else '0';
        
    process(clock)
        variable adlo   : unsigned(3 downto 0);
        variable adhi   : unsigned(7 downto 4);
    begin
        if rising_edge(clock) then
            adlo := io_addr(3 downto 0);
            adhi := io_addr(7 downto 4);
            
            if tx_ack = '1' then
                pulse_out(7 downto 0) <= (others => '0');
            end if;
            pulse_out(15 downto 8) <= X"00";
            
            pulse_in(15) <= '0';
            if frame_div = 0 then
                frame_div <= 7499; -- microframes
                pulse_in(15) <= '1';                
                frame_cnt <= frame_cnt + 1;
            else
                frame_div <= frame_div - 1;
            end if;

            if tx_chirp_start_i = '1' then
                tx_chirp_enable <= '1';
            elsif tx_chirp_end_i = '1' then
                tx_chirp_enable <= '0';
            end if;

            filter_st1 <= '0';
            if reset_filter = '1' then
                filter_cnt <= (others => '0');
            elsif status(1) = '0' then
                filter_cnt <= (others => '0');
            else                
                filter_cnt <= filter_cnt + 1;
                if filter_cnt = 255 and latched(14)='0' then
                    filter_st1 <= '1';
                end if;
            end if;
                
            if reg_ack='1' then
                reg_write  <= '0';
                reg_read   <= '0';
                stall_i    <= '0';
            end if;

            if io_write='1' then
                reg_addr <= std_logic_vector(io_addr(5 downto 0));
                case adhi is
                when X"0" =>
                    case adlo(3 downto 0) is
                    when X"0" =>
                        tx_pid <= io_wdata(tx_pid'range);
                    when X"1" =>
                        tx_token <= io_wdata(tx_token'range);
                    when X"2" =>
                        tx_length <= unsigned(io_wdata(tx_length'range));
                    when others =>
                        null;
                    end case;
                when X"1" =>
                    pulse_out(to_integer(adlo)) <= '1';
                when X"2" =>
                    latched(to_integer(adlo)) <= '0';
                when X"4" =>
                    level_out(to_integer(adlo)) <= '0';
                when X"5" =>
                    level_out(to_integer(adlo)) <= '1';
                when X"C"|X"D"|X"E"|X"F" =>
                    reg_wdata <= io_wdata(7 downto 0);
                    reg_write <= '1';
                    stall_i <= '1';
                when others =>
                    null;
                end case;
            end if;

            if io_read = '1' then
                reg_addr <= std_logic_vector(io_addr(5 downto 0));
                if io_addr(7 downto 6) = "10" then
                    reg_read <= '1';    
                    stall_i <= '1';
                end if;
            end if;

            for i in latched'range loop
                if pulse_in(i)='1' then
                    latched(i) <= '1';
                end if;
            end loop;
                
            if reset='1' then
                tx_pid <= (others => '0');
                tx_token <= (others => '0');
                tx_length <= (others => '0');
                latched <= (others => '0');
                level_out <= (others => '0');
                reg_read <= '0';
                reg_write <= '0';
                stall_i <= '0';
                tx_chirp_enable <= '0';
            end if;
        end if;
    end process;

    ulpi_access <= io_addr(7);
    stall <= ((stall_i or io_read or io_write) and ulpi_access) and not reg_ack; -- stall right away, and continue right away also when the data is returned
    
    process(latched, level_out, rx_pid, rx_token, reg_rdata, io_addr)
        variable adlo   : unsigned(3 downto 0);
        variable adhi   : unsigned(7 downto 4);
    begin
        io_rdata <= (others => '0');
        adlo := io_addr(3 downto 0);
        adhi := io_addr(7 downto 4);

        case adhi is
        when X"2" =>
            io_rdata(15) <= latched(to_integer(adlo));
        when X"3" =>
            case adlo(3 downto 0) is
            when X"0" =>
                io_rdata <= X"000" & rx_pid;
            when X"1" =>
                io_rdata <= "00000" & rx_token;
            when X"2" =>
                io_rdata <= X"00" & status;
            when X"3" =>
                io_rdata <= "00000" & std_logic_vector(transferred);
            when others =>
                null;
            end case;
        when X"6" =>
            case adlo(3 downto 0) is
            when X"0" =>
                io_rdata <= "00000" & std_logic_vector(frame_cnt(13 downto 3));
            when others =>
                null;
            end case;
        when X"7" =>
            io_rdata(15) <= mem_ready;
        when X"8"|X"9"|X"A"|X"B" =>
            io_rdata <= X"00" & reg_rdata;
            
        when others =>
            null;
        end case;
    end process;

end architecture;
