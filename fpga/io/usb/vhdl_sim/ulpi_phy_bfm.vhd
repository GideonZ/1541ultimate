library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_phy_bfm is
generic (
    g_rx_interval   : integer := 100 );
port (
    clock       : in    std_logic;
    reset       : in    std_logic;
    
    ULPI_DATA   : inout std_logic_vector(7 downto 0);
    ULPI_DIR    : out   std_logic;
    ULPI_NXT    : out   std_logic;
    ULPI_STP    : in    std_logic );

end ulpi_phy_bfm;
    
architecture gideon of ulpi_phy_bfm is

    type t_state is (idle, sending, receiving, read_reg, read_reg2, read_reg3, write_reg, status_update);
    signal state            : t_state;
    signal pattern          : std_logic_vector(0 to 19);
    signal do_send          : std_logic;
    signal counter          : unsigned(7 downto 0) := X"01";
    signal status_in        : std_logic_vector(7 downto 0) := X"00";
    signal status_d         : std_logic_vector(7 downto 0) := X"00";
    signal ulpi_nxt_i       : std_logic;
    signal ulpi_dir_i       : std_logic;
    alias  ulpi_cmd         : std_logic_vector(1 downto 0) is ULPI_DATA(7 downto 6);

    constant c_transmit     : std_logic_vector(1 downto 0) := "01";
    constant c_write_reg    : std_logic_vector(1 downto 0) := "10";
    constant c_read_reg     : std_logic_vector(1 downto 0) := "11";
begin
    process(clock)
        variable byte_count  : integer := 0;
        variable rx_interval : integer := g_rx_interval;
        variable address     : std_logic_vector(5 downto 0);

        procedure set_reg(addr: std_logic_vector(5 downto 0);
                          data: std_logic_vector(7 downto 0) ) is
        begin
            if addr = "001010" then
                if data(5)='1' or data(6)='1' then-- poweron
                    report "Power On";
                    if status_in(3)='0' then
                        status_in(3 downto 2) <= transport "00",
                                                           "01" after 10 us,
                                                           "10" after 20 us,
                                                           "11" after 30 us;
                    end if;
                else -- power off
                    report "Power Off";
                    status_in(3 downto 2) <= transport "11",
                                                       "10" after 1 us,
                                                       "01" after 2 us,
                                                       "00" after 3 us;
                end if;
            end if;
            
            if addr = "000100" then
                case data(2 downto 0) is
                when "000" => -- host chirp
                    status_in(1 downto 0) <= transport "00", "10" after 10 us, "00" after 15 us;
                when "001"|"011" => -- powerup
                    status_in(1 downto 0) <= "11";
                when "010" => -- unknown
                    status_in(1 downto 0) <= "00";
                when "100" => -- peripheral chirp
                    status_in(1 downto 0) <= "10";
                when "101"|"111" => -- peripheral FS
                    status_in(1 downto 0) <= "01";
                when "110" => -- peripheral LS
                    status_in(1 downto 0) <= "10";
                when others =>
                    null;
                end case;
            end if;
        end procedure;


    begin
        if rising_edge(clock) then

            if rx_interval = 0 then
                do_send <= '0'; -- autonomous send disabled
                rx_interval := g_rx_interval;
            else
                rx_interval := rx_interval - 1;
            end if;

            ulpi_nxt_i <= '0';

            case state is
            when idle =>
                status_d <= status_in;
                ulpi_dir_i <= '0';
                ULPI_DATA <= (others => 'Z');
                if do_send = '1' then
                    do_send  <= '0';
                    ulpi_dir_i <= '1';
                    ulpi_nxt_i <= '1';
                    pattern  <= "01111101111011101101";
                    state <= sending;
                    byte_count := 20;
                elsif ulpi_dir_i = '0' then
                    if ulpi_cmd = c_transmit then
                        pattern  <= "11111111100111011010";
                        state <= receiving;
                    elsif ulpi_cmd = c_write_reg then
                        address := ULPI_DATA(5 downto 0);
                        byte_count := 2;
                        state <= write_reg;
                    elsif ulpi_cmd = c_read_reg then
                        state <= read_reg;
                    elsif status_in /= status_d then
                        ulpi_dir_i <= '1';
                        state <= status_update;
                    end if;
                end if;

            when status_update =>
                ULPI_DATA <= status_d;
                state <= idle;
                
            when sending =>
                pattern <= pattern(1 to 19) & '0';
                if pattern(0)='1' then
                    ULPI_DATA <= std_logic_vector(counter);
                    ulpi_nxt_i  <= '1';
                    counter <= counter + 1;
                else
                    ULPI_DATA <= status_in;
                    ulpi_nxt_i  <= '0';
                end if;
                byte_count := byte_count - 1;
                if byte_count = 0 then
                    state <= idle;
                end if;

            when receiving =>
                if ULPI_STP = '1' then
                    ulpi_nxt_i <= '0';
                    state <= idle;
                else
                    ulpi_nxt_i <= pattern(0);
                    pattern <= pattern(1 to 19) & '1';
                end if;                    
                
            when write_reg =>
                if byte_count = 0 then
                    ulpi_nxt_i <= '0';
                    set_reg(address, ULPI_DATA);
                else
                    ulpi_nxt_i <= '1';
                end if;
                byte_count := byte_count - 1;
                if ULPI_STP = '1' then
                    state <= idle;
                end if;
            
            when read_reg =>
                ulpi_nxt_i <= '1';
                state <= read_reg2;
            
            when read_reg2 =>
                ulpi_dir_i <= '1';
                state <= read_reg3;
            
            when read_reg3 =>
                ULPI_DATA <= X"AA";
                state <= idle;

            when others =>
                state <= idle;
            end case;

            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;
    
    ULPI_NXT <= ulpi_nxt_i;
    ULPI_DIR <= ulpi_dir_i;
    
end gideon;    
    