library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

--use work.usb_pkg.all;

entity nano_minimal_io is
generic (
    g_support_suspend : boolean := false );
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
    
    -- Register access
    reg_read        : out std_logic := '0';
    reg_write       : out std_logic := '0';
    reg_ack         : in  std_logic;
    reg_address     : out std_logic_vector(5 downto 0) := (others => '0');  
    reg_wdata       : out std_logic_vector(7 downto 0) := X"00";  
    reg_rdata       : in  std_logic_vector(7 downto 0);
    status          : in  std_logic_vector(7 downto 0);
    
    -- Low level
    do_chirp        : out std_logic;
    chirp_data      : out std_logic;

    -- Functional Level
    connected       : out std_logic; -- '1' when a USB device is connected
    operational     : out std_logic; -- '1' when a USB device is successfully reset
    suspended       : out std_logic; -- '1' when the USB bus is in the suspended state
    sof_enable      : out std_logic; -- '1' when SOFs shall be generated
    speed           : out std_logic_vector(1 downto 0) ); -- speed indicator of current link

end entity;

architecture gideon of nano_minimal_io is
    signal stall_i   : std_logic := '0';
    signal ulpi_access : std_logic;
    signal filter_cnt   : unsigned(7 downto 0);
    signal filter_st1   : std_logic;
    signal filter_cnt2  : unsigned(8 downto 0);
    signal bus_low      : std_logic := '0';
    signal speed_i      : std_logic_vector(1 downto 0);
    signal reset_filter_st1     : std_logic;
    signal disconn              : std_logic;
    signal disconn_latched      : std_logic;
begin
    disconn          <= '1' when (status(5 downto 4) = "10") or (bus_low = '1' and speed_i(1)='0') else '0';
    speed            <= speed_i;

    process(clock)
        variable adlo   : unsigned(3 downto 0);
        variable adhi   : unsigned(7 downto 4);
    begin
        if rising_edge(clock) then
            adlo := io_addr(3 downto 0);
            adhi := io_addr(7 downto 4);
            
            -- filter for chirp detection
            if reset_filter_st1 = '1' then
                filter_cnt <= (others => '0');
                filter_st1 <= '0';
            elsif status(1) = '0' then
                filter_cnt <= (others => '0');
            else -- status(1) = '1'                
                filter_cnt <= filter_cnt + 1;
                if filter_cnt = 255 then
                    filter_st1 <= '1';
                end if;
            end if;

            -- filter for disconnect detection
            if status(1 downto 0) = "00" then
                filter_cnt2 <= filter_cnt2 + 1;
                if filter_cnt2 = 511 then
                    bus_low <= '1';
                end if;
            else
                filter_cnt2 <= (others => '0');
                bus_low <= '0';
            end if;
                
            -- register access defaults
            if reg_ack='1' then
                reg_write  <= '0';
                reg_read   <= '0';
                stall_i    <= '0';
            end if;

            reset_filter_st1 <= '0';
            
            if io_write='1' then
                reg_address <= std_logic_vector(io_addr(5 downto 0));
                case adhi is
                -- set registers
                when X"2" =>
                    case adlo is
                    when X"0" =>
                        do_chirp <= '1';
                    when X"1" =>
                        chirp_data <= '1';
                    when X"2" =>
                        connected <= '1';
                    when X"3" =>
                        operational <= '1';
                    when X"4" =>
                        suspended <= '1';
                    when X"6" =>
                        speed_i <= io_wdata(1 downto 0);
                    when X"7" =>
                        sof_enable <= '1';
                    when X"9" =>
                        reset_filter_st1 <= '1';
                    when others =>
                        null;
                    end case;
                -- clear registers
                when X"3" =>
                    case adlo is
                    when X"0" =>
                        do_chirp <= '0';
                    when X"1" =>
                        chirp_data <= '0';
                    when X"2" =>
                        connected <= '0';
                    when X"3" =>
                        operational <= '0';
                    when X"4" =>
                        suspended <= '0';
                    when X"7" =>
                        sof_enable <= '0';
                    when X"E" =>
                        disconn_latched <= '0';
                    when others =>
                        null;
                    end case;
                when X"C"|X"D"|X"E"|X"F" =>
                    reg_wdata <= io_wdata(7 downto 0);
                    reg_write <= '1';
                    stall_i <= '1';
                when others =>
                    null;
                end case;
            end if;

            if io_read = '1' then
                reg_address <= std_logic_vector(io_addr(5 downto 0));
                if io_addr(7 downto 6) = "10" then
                    reg_read <= '1';    
                    stall_i <= '1';
                end if;
            end if;

            if disconn='1' then
                disconn_latched <= '1';
            end if;
            
            if reset='1' then
                do_chirp <= '0';
                chirp_data <= '0';
                connected <= '0';
                operational <= '0';
                suspended <= '0';
                sof_enable <= '0';
                disconn_latched <= '0';
                filter_st1 <= '0';
                reg_read <= '0';
                reg_write <= '0';
                stall_i <= '0';
                speed_i <= "01";
            end if;
        end if;
    end process;

    ulpi_access <= io_addr(7);
    stall <= ((stall_i or io_read or io_write) and ulpi_access) and not reg_ack; -- stall right away, and continue right away also when the data is returned
    
    process( reg_rdata, io_addr, status, disconn_latched, filter_st1)
        variable adlo   : unsigned(3 downto 0);
        variable adhi   : unsigned(7 downto 4);
    begin
        io_rdata <= (others => '0');
        adlo := io_addr(3 downto 0);
        adhi := io_addr(7 downto 4);

        case adhi is
        when X"3" =>
            case adlo(3 downto 0) is
            when X"9" =>
                io_rdata(15) <= filter_st1;
            when X"E" =>
                io_rdata(15) <= disconn_latched;            
            when X"F" =>
                io_rdata(7 downto 0) <= status;
            when others =>
                null;
            end case;

        when X"8"|X"9"|X"A"|X"B" =>
            io_rdata <= X"00" & reg_rdata;
            
        when others =>
            null;
        end case;
    end process;

end architecture;
