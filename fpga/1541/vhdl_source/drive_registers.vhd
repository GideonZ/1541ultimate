library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.c1541_pkg.all;

entity drive_registers is
generic (
    g_clock_freq    : natural := 50_000_000;
    g_multi_mode    : boolean := false );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    tick_1kHz       : in  std_logic;
                        
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    
    iec_reset_o     : in  std_logic;
    use_c64_reset   : out std_logic;
    power           : out std_logic;
    drv_reset       : out std_logic;
    drive_address   : out std_logic_vector(1 downto 0);
    floppy_inserted : out std_logic;
    disk_change_n   : out std_logic;
    force_ready     : out std_logic;
    write_prot_n    : out std_logic;
    bank_is_ram     : out std_logic_vector(7 downto 1);
    stop_on_freeze  : out std_logic;
    drive_type      : out natural range 0 to 2;
    do_snd_insert   : out std_logic;
    do_snd_remove   : out std_logic;

    track           : in  unsigned(6 downto 0);
    side            : in  std_logic := '0';
    mode            : in  std_logic;
    motor_on        : in  std_logic );
end;

architecture rtl of drive_registers is
    signal power_i          : std_logic;
    signal drv_reset_i      : std_logic := '0';
    signal use_c64_reset_i  : std_logic;
    signal drive_address_i  : std_logic_vector(1 downto 0);
    signal sensor_i         : std_logic;
    signal bank_is_ram_i    : std_logic_vector(7 downto 1);
    signal inserted_i       : std_logic;
    signal disk_change_i    : std_logic;
    signal force_ready_i    : std_logic;
    signal stop_when_frozen : std_logic;
    signal drive_type_i     : std_logic_vector(1 downto 0);

    signal write_delay      : unsigned(10 downto 0); -- max 2047 = 2 sec
    signal write_busy       : std_logic;
    signal manual_write     : std_logic;
begin
    p_reg: process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            manual_write <= '0';
            do_snd_insert <= '0';
            do_snd_remove <= '0';
            
            if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_drvreg_power =>
                    power_i <= io_req.data(0);
                when c_drvreg_reset =>
                    drv_reset_i <= io_req.data(0);
                    use_c64_reset_i <= io_req.data(1);
                    stop_when_frozen <= io_req.data(2);
                when c_drvreg_address =>
                    drive_address_i <= io_req.data(1 downto 0);
                when c_drvreg_sensor =>
                    sensor_i <= io_req.data(0);
                when c_drvreg_inserted =>
                    inserted_i <= io_req.data(0);
                when c_drvreg_rammap =>
                    bank_is_ram_i <= io_req.data(7 downto 1);
                when c_drvreg_man_write =>
                    manual_write <= '1';
                when c_drvreg_diskchng =>
                    disk_change_i <= io_req.data(0);
                    force_ready_i <= io_req.data(1);
                when c_drvreg_drivetype =>
                    if g_multi_mode then
                        drive_type_i <= io_req.data(1 downto 0);
                    end if;
                when c_drvreg_sound =>
                    do_snd_insert <= io_req.data(0);
                    do_snd_remove <= io_req.data(1);
                when others =>
                    null;
                end case;
            end if; -- write
                
            if io_req.read='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_drvreg_power =>
                    io_resp.data(0) <= power_i;
                when c_drvreg_reset =>
                    io_resp.data(0) <= drv_reset_i;
                    io_resp.data(1) <= use_c64_reset_i;
                    io_resp.data(2) <= stop_when_frozen;
                when c_drvreg_address =>
                    io_resp.data(1 downto 0) <= drive_address_i;
                when c_drvreg_sensor =>
                    io_resp.data(0) <= sensor_i;
                when c_drvreg_inserted =>
                    io_resp.data(0) <= inserted_i;
                when c_drvreg_rammap =>
                    io_resp.data <= bank_is_ram_i & '0';
                when c_drvreg_diskchng =>
                    io_resp.data(0) <= disk_change_i;
                    io_resp.data(1) <= force_ready_i;
                when c_drvreg_drivetype =>
                    if g_multi_mode then
                        io_resp.data(1 downto 0) <= drive_type_i;
                    end if;
                when c_drvreg_track =>
                    io_resp.data(6 downto 0) <= std_logic_vector(track(6 downto 0));
                when c_drvreg_side =>
                    io_resp.data(0) <= side;
                when c_drvreg_status =>
                    io_resp.data(0) <= motor_on;
                    io_resp.data(1) <= not mode; -- mode is '0' when writing
                    io_resp.data(2) <= write_busy;
                when others =>
                    null;
                end case;
            end if; -- read
                                                    
            drv_reset <= drv_reset_i or iec_reset_o or reset;

            if reset='1' then
                power_i          <= '0';
                drv_reset_i      <= '1';
                drive_address_i  <= (others => '0');
                sensor_i         <= '0';
                bank_is_ram_i    <= (others => '0');
                inserted_i       <= '0';
                disk_change_i    <= '0';
                use_c64_reset_i  <= '1';
                stop_when_frozen <= '1';
                force_ready_i    <= '0';
                drive_type_i     <= "00";
            end if;    
        end if;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            if drv_reset_i = '1' then
                write_busy <= '0';
                write_delay <= (others => '0');
            elsif (mode = '0' and motor_on = '1') or manual_write = '1' then
                write_busy <= '1';
                write_delay <= (others => '1');
            elsif write_delay = 0 then
                write_busy <= '0';
            elsif tick_1kHz = '1' then
                write_delay <= write_delay - 1;
            end if;
        end if;
    end process;

    power           <= power_i;
    drive_address   <= drive_address_i;
    floppy_inserted <= inserted_i;
    disk_change_n   <= not disk_change_i;
    write_prot_n    <= sensor_i;
    bank_is_ram     <= bank_is_ram_i;
    use_c64_reset   <= use_c64_reset_i;
    stop_on_freeze  <= stop_when_frozen;
    force_ready     <= force_ready_i;
    drive_type      <= to_integer(unsigned(drive_type_i));

end architecture;
