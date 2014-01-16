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
    signal counter          : integer := 0;
    signal status_in        : std_logic_vector(7 downto 0) := X"00";
    signal status_d         : std_logic_vector(7 downto 0) := X"00";
    signal ulpi_nxt_i       : std_logic;
    signal ulpi_nxt_d       : std_logic;
    signal ulpi_dir_i       : std_logic;
    alias  ulpi_cmd         : std_logic_vector(1 downto 0) is ULPI_DATA(7 downto 6);

    type   t_data8          is array(natural range <>) of std_logic_vector(7 downto 0);
    signal crc_sync     : std_logic;
    signal data_crc     : std_logic_vector(15 downto 0);

    signal irq_status   : std_logic_vector(7 downto 0) := X"00";
    signal func_select  : std_logic_vector(7 downto 0) := X"00";
    signal intf_control : std_logic_vector(7 downto 0) := X"00";
    signal otg_control  : std_logic_vector(7 downto 0) := X"00";
    signal scratch      : std_logic_vector(7 downto 0) := X"00";
    signal power        : std_logic := '0';
    signal power_level  : std_logic_vector(1 downto 0) := "00";
    signal pid          : std_logic_vector(3 downto 0) := X"F";
    
    constant c_transmit     : std_logic_vector(1 downto 0) := "01";
    constant c_write_reg    : std_logic_vector(1 downto 0) := "10";
    constant c_read_reg     : std_logic_vector(1 downto 0) := "11";

    function to_std(a : boolean) return std_logic is
    begin
        if (a) then
            return '1';
        else
            return '0';
        end if;
    end function;
begin

    crc_sync <= '1' when (state = idle) else '0';
    
    irq_status(3) <= to_std(status_in(3 downto 2) = "01");
    irq_status(2) <= status_in(3);
    irq_status(1) <= to_std(status_in(3 downto 2) = "11");

    status_in(3 downto 2) <= power_level;

    process(power)
    begin
        if power'event then
            if power = '1' then
                power_level <= transport "00",
                                       "01" after 10 us,
                                       "10" after 20 us,
                                       "11" after 30 us;
            elsif power = '0' then
                power_level <= transport "11",
                                       "10" after 3 us,
                                       "01" after 6 us,
                                       "00" after 10 us;
            else
                power_level <= "00";
            end if;
        end if;
    end process;


    process(clock)
        variable byte_count  : integer := 0;
        variable rx_interval : integer := g_rx_interval;
        variable address     : std_logic_vector(5 downto 0);

        procedure set_reg(addr: std_logic_vector(5 downto 0);
                          data: std_logic_vector(7 downto 0) ) is
            variable a      : integer;
        begin
            a := to_integer(unsigned(addr));
            case a is
            when 4 =>
                func_select <= data;            
                if data(4 downto 0) = "10000" then -- host chirp
                    status_in(1 downto 0) <= transport "00", "10" after 10 us, "00" after 15 us;
                elsif data(4 downto 3) ="00" then -- normal operation
                    status_in(1 downto 0) <= data(1 downto 0); -- for the time being
                end if;
            when 7 =>
                intf_control <= data;
            when 8 =>
                intf_control <= intf_control or data;
            when 9 =>
                intf_control <= intf_control and not data;
            when 10 =>
                otg_control <= data;
                if data(5)='1' or data(6)='1' then-- poweron
                    report "Power On";
                    power <= '1';
                else -- power off
                    report "Power Off";
                    power <= '0';            
                end if;
            when 22 =>
                scratch <= data;
            when 23 =>
                scratch <= scratch or data;
            when 24 =>
                scratch <= scratch and not data;
            when others =>
                null;
            end case;            
        end procedure;

        variable receive_buffer  : t_data8(0 to 127);
        variable transmit_buffer : t_data8(0 to 127);
        variable receive_length  : integer := 0;
        variable transmit_length : integer := 0;

        procedure determine_response is
        begin
            if receive_buffer(0) = X"43" then -- data0 packet
                transmit_buffer(0) := X"D2";
                transmit_length := 1;
                do_send <= '1';
            end if;
        end procedure;

    
        impure function read_ulpi_register(addr: std_logic_vector(5 downto 0)) return std_logic_vector is
            variable ret    : std_logic_vector(7 downto 0);
            variable a      : integer;
        begin
            a := to_integer(unsigned(addr));
            ret := X"00";            
            case a is
            when 19 | 20 =>
                ret := irq_status;
            when 0 =>
                ret := X"24";
            when 1|2 =>
                ret := X"04";
            when 4|5|6 =>
                ret := func_select;
            when 7|8|9 =>
                ret := intf_control;
            when 10|11|12 =>
                ret := otg_control;
            when 22|23|24 =>
                ret := scratch;
            when others =>
                null;
            end case;
            return ret;
        end function;

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
                    counter <= 0;
                    byte_count := transmit_length;
                elsif ulpi_dir_i = '0' then
                    pid <= ULPI_DATA(3 downto 0);
                    if ulpi_cmd = c_transmit then
                        pattern  <= "11111111100111011010";
                        state <= receiving;
                        receive_length := 0;
                    elsif ulpi_cmd = c_write_reg then
                        address := ULPI_DATA(5 downto 0);
                        byte_count := 2;
                        state <= write_reg;
                    elsif ulpi_cmd = c_read_reg then
                        address := ULPI_DATA(5 downto 0);
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
                    ULPI_DATA <= transmit_buffer(counter);
                    ulpi_nxt_i  <= '1';
                    counter <= counter + 1;
                    byte_count := byte_count - 1;
                    if byte_count = 0 then
                        state <= idle;
                    end if;
                else
                    ULPI_DATA <= status_in;
                    ulpi_nxt_i  <= '0';
                end if;

            when receiving =>
                if ULPI_STP = '1' then
                    ulpi_nxt_i <= '0';
                    state <= idle;
                    report "Received packet with length = " & integer'image(receive_length);
                    determine_response;
                else
                    if (ulpi_nxt_i = '1') and (pid /= X"0") then
                        receive_buffer(receive_length) := ULPI_DATA;
                        receive_length := receive_length + 1;
                    end if;
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
                ULPI_DATA <= read_ulpi_register(address);
                state <= idle;

            when others =>
                state <= idle;
            end case;

            ulpi_nxt_d <= ulpi_nxt_i;
            
            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;
    
    ULPI_NXT <= ulpi_nxt_i;
    ULPI_DIR <= ulpi_dir_i;

    i_data_crc: entity work.data_crc
    port map (
        clock       => clock,
        sync        => crc_sync,
        valid       => ulpi_nxt_d,
        data_in     => ULPI_DATA,
        
        crc         => data_crc );
    
end gideon;    
    