library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity real_time_clock is
generic (
    g_leap      : boolean := true;
    g_freq      : natural := 50_000_000 );
port (
    clock       : in  std_logic;    
    reset       : in  std_logic;
    
    req         : in  t_io_req;
    resp        : out t_io_resp );
end entity;

architecture gideon of real_time_clock is
    signal year         : unsigned(6 downto 0);
    signal month        : unsigned(3 downto 0);
    signal date         : unsigned(4 downto 0);
    signal day          : unsigned(2 downto 0);
    signal hour         : unsigned(4 downto 0);
    signal minute       : unsigned(5 downto 0);
    signal second       : unsigned(5 downto 0);
    signal hundredths   : unsigned(6 downto 0);
    signal lock         : std_logic;
    signal tick         : std_logic;
    signal div_tick     : std_logic;
    signal div_count    : integer range 0 to g_freq/100;
    signal fat_packed   : unsigned(31 downto 0);
    
    type t_int5_array is array(natural range <>) of integer range 0 to 31;
    constant c_month_length : t_int5_array(0 to 15) := (
        31, -- dummy
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, -- JAN - DEC
        30, 30, 31 ); -- dummys
    signal month_length : t_int5_array(0 to 15) := c_month_length;

    --/* 31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31) */
	--/* 15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) */
begin
    month_length(2) <= 29 when year(1 downto 0)="00" and g_leap else 28;
    fat_packed <= year & month & date & hour & minute & second(5 downto 1);
    
    process(clock)
        variable tick_save    : integer range 0 to 15 := 0;
    begin
        if rising_edge(clock) then
            tick <= '0';
            div_tick <= '0';
            if div_count = 0 then
                div_count <= (g_freq / 100) - 1;
                div_tick <= '1';
--                tick_save := tick_save+1;
            else
                div_count <= div_count - 1;
            end if;
            
            if lock='0' then
                if div_tick='1' then
                    tick <= '1';
                elsif tick_save /= 0 then
                    tick_save := tick_save - 1;
                    tick <= '1';
                end if;
            elsif div_tick='1' then
                tick_save := tick_save + 1;
            end if;
            
            if tick='1' then
                if hundredths = 99 then
                    hundredths <= to_unsigned( 0, hundredths'length);
                    if second = 59 then
                        second <= to_unsigned(0, second'length);
                        if minute = 59 then
                            minute <= to_unsigned(0, minute'length);
                            if hour = 23 then
                                hour <= to_unsigned(0, hour'length);
                                if day = 6 or day = 7 then
                                    day <= to_unsigned(0, day'length);
                                else
                                    day <= day + 1;
                                end if;
                                if date = month_length(to_integer(month)) then
                                    date <= to_unsigned(1, date'length);
                                    if month = 12 then
                                        month <= to_unsigned(1, month'length);
                                        year <= year + 1;
                                    else
                                        month <= month + 1;
                                    end if;
                                else
                                    date <= date + 1;
                                end if;
                            else
                                hour <= hour + 1;
                            end if;
                        else
                            minute <= minute + 1;
                        end if;
                    else
                        second <= second + 1;
                    end if;
                else
                    hundredths <= hundredths + 1;
                end if;
            end if;

            resp <= c_io_resp_init;
            if req.read='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    resp.data(year'range) <= std_logic_vector(year);
                when X"1" =>
                    resp.data(month'range) <= std_logic_vector(month);
                when X"2" =>
                    resp.data(date'range) <= std_logic_vector(date);
                when X"3" =>
                    resp.data(day'range) <= std_logic_vector(day);
                when X"4" =>
                    resp.data(hour'range) <= std_logic_vector(hour);
                when X"5" =>
                    resp.data(minute'range) <= std_logic_vector(minute);
                when X"6" =>
                    resp.data(second'range) <= std_logic_vector(second);
                when X"7" =>
                    resp.data(hundredths'range) <= std_logic_vector(hundredths);
                when X"8" =>
                    resp.data <= std_logic_vector(fat_packed(7 downto 0));
                when X"9" =>
                    resp.data <= std_logic_vector(fat_packed(15 downto 8));
                when X"A" =>
                    resp.data <= std_logic_vector(fat_packed(23 downto 16));
                when X"B" =>
                    resp.data <= std_logic_vector(fat_packed(31 downto 24));
                when others =>
                    null;
                end case;
            elsif req.write='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    year <= unsigned(req.data(year'range));
                when X"1" =>
                    month <= unsigned(req.data(month'range));
                when X"2" =>
                    date <= unsigned(req.data(date'range));
                when X"3" =>
                    day <= unsigned(req.data(day'range));
                when X"4" =>
                    hour <= unsigned(req.data(hour'range));
                when X"5" =>
                    minute <= unsigned(req.data(minute'range));
                when X"6" =>
                    second <= unsigned(req.data(second'range));
                when X"7" =>
                    hundredths <= unsigned(req.data(hundredths'range));
                when X"C" =>
                    lock <= req.data(0);
                when others =>
                    null;
                end case;
            end if;

            if reset='1' then
                year         <= to_unsigned(30, year'length);
                month        <= to_unsigned( 4, month'length);
                date         <= to_unsigned( 6, date'length);
                day          <= to_unsigned( 2, day'length);
                hour         <= to_unsigned(15, hour'length);
                minute       <= to_unsigned(59, minute'length);
                second       <= to_unsigned(23, second'length);
                hundredths   <= to_unsigned( 0, hundredths'length);

                lock         <= '0';
                div_count    <= 0;
            end if;
        end if;
    end process;
end architecture;
    
