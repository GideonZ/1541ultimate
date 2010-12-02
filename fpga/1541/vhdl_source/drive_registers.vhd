library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.c1541_pkg.all;

entity drive_registers is
generic (
    g_audio_base    : unsigned(27 downto 0) := X"0030000";
    g_ram_base      : unsigned(27 downto 0) := X"0060000" );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
                    
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    
    iec_reset_o     : in  std_logic;
    use_c64_reset   : out std_logic;
    power           : out std_logic;
    drv_reset       : out std_logic;
    drive_address   : out std_logic_vector(1 downto 0);
    floppy_inserted : out std_logic;
    write_prot_n    : out std_logic;
    bank_is_ram     : out std_logic_vector(7 downto 0);
    dirty_led_n     : out std_logic;

    param_write     : out std_logic;
    param_ram_en    : out std_logic;
    param_addr      : out std_logic_vector(10 downto 0);
    param_wdata     : out std_logic_vector(7 downto 0);
    param_rdata     : in  std_logic_vector(7 downto 0);

    track           : in  std_logic_vector(6 downto 0);
    mode            : in  std_logic;
    motor_on        : in  std_logic );
end;

architecture rtl of drive_registers is
    signal dirty_bits : std_logic_vector(63 downto 0) := (others => '0');
    signal any_dirty  : std_logic;
    signal irq_en     : std_logic;
    signal wr, wd     : std_logic;
    signal wa         : integer range 0 to 63 := 0;
    signal param_ack  : std_logic;

    signal power_i          : std_logic;
    signal drv_reset_i      : std_logic;
    signal use_c64_reset_i  : std_logic;
    signal drive_address_i  : std_logic_vector(1 downto 0);
    signal sensor_i         : std_logic;
    signal bank_is_ram_i    : std_logic_vector(7 downto 0);
    signal inserted_i       : std_logic;
begin
    p_reg: process(clock)
    begin
        if rising_edge(clock) then

            io_resp <= c_io_resp_init;
            param_ack <= '0';
            wr <= '0';
            wa <= to_integer(unsigned(track(6 downto 1)));
            if mode = '0' and motor_on='1' and inserted_i='1' then
                wr <= '1';
                wd <= '1';
                any_dirty <= '1';
            end if;

            if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(12 downto 11) is
                when "00" => -- registers
                    case io_req.address(3 downto 0) is
                    when c_drvreg_power =>
                        power_i <= io_req.data(0);
                    when c_drvreg_reset =>
                        drv_reset_i <= io_req.data(0);
                        use_c64_reset_i <= io_req.data(1);
                    when c_drvreg_address =>
                        drive_address_i <= io_req.data(1 downto 0);
                    when c_drvreg_sensor =>
                        sensor_i <= io_req.data(0);
                    when c_drvreg_inserted =>
                        inserted_i <= io_req.data(0);
                    when c_drvreg_rammap =>
                        bank_is_ram_i <= io_req.data;
                    when c_drvreg_anydirty =>
                        any_dirty <= '0';
                    when c_drvreg_dirtyirq =>
                        irq_en <= io_req.data(0);
                    when others =>
                        null;
                    end case;
                when "01" => -- dirty block
                    wr <= '1';
                    wa <= to_integer(io_req.address(5 downto 0));
                    wd <= '0';
                when "10" => -- param block
                    null;
                when others => -- unused
                    null;
                end case;
            end if; -- write
                
            if io_req.read='1' then
                case io_req.address(12 downto 11) is
                when "00" => -- registers                
                    io_resp.ack <= '1';
                    case io_req.address(3 downto 0) is
                    when c_drvreg_power =>
                        io_resp.data(0) <= power_i;
                    when c_drvreg_reset =>
                        io_resp.data(0) <= drv_reset_i;
                        io_resp.data(1) <= use_c64_reset_i;
                    when c_drvreg_address =>
                        io_resp.data(1 downto 0) <= drive_address_i;
                    when c_drvreg_sensor =>
                        io_resp.data(0) <= sensor_i;
                    when c_drvreg_inserted =>
                        io_resp.data(0) <= inserted_i;
                    when c_drvreg_rammap =>
                        io_resp.data <= bank_is_ram_i;
                    when c_drvreg_anydirty =>
                        io_resp.data(0) <= any_dirty;
                    when c_drvreg_dirtyirq =>
                        io_resp.data(0) <= irq_en;
                    when c_drvreg_track =>
                        io_resp.data(6 downto 0) <= track(6 downto 0);
                    when c_drvreg_status =>
                        io_resp.data(0) <= motor_on;
                        io_resp.data(1) <= not mode; -- mode is '0' when writing
                    when c_drvreg_memmap =>
                        io_resp.data <= std_logic_vector(g_ram_base(23 downto 16));
                    when c_drvreg_audiomap =>
                        io_resp.data <= std_logic_vector(g_audio_base(23 downto 16));
                    when others =>
                        null;
                    end case;
                when "01" => -- dirty block
                    io_resp.ack <= '1';
                    io_resp.data(0) <= dirty_bits(to_integer(io_req.address(5 downto 0)));
                when "10" => -- param block
                    param_ack <= '1';
                when others =>
                    io_resp.ack <= '1';
                end case;
            end if; -- read
                                                    
            -- param response
            if param_ack='1' then
                io_resp.ack <= '1';
                io_resp.data <= param_rdata;
            end if;
            
            -- write into memory array
            if wr='1' then
                dirty_bits(wa) <= wd;
            end if;

            drv_reset <= drv_reset_i or iec_reset_o;

            if reset='1' then
                power_i          <= '0';
                drv_reset_i      <= '1';
                drive_address_i  <= (others => '0');
                sensor_i         <= '0';
                bank_is_ram_i    <= (others => '0');
                inserted_i       <= '0';
                use_c64_reset_i  <= '1';
                any_dirty <= '0';
                irq_en    <= '0';
                wd        <= '0';
            end if;    
        end if;
    end process;

    param_write     <= '1' when io_req.write='1' and io_req.address(12 downto 11)="10" else '0';
    param_ram_en    <= '1' when (io_req.write='1' or io_req.read='1') and io_req.address(12 downto 11)="10" else '0';
    param_addr      <= std_logic_vector(io_req.address(10 downto 0));
    param_wdata     <= io_req.data;

    power           <= power_i;
    drive_address   <= drive_address_i;
    floppy_inserted <= inserted_i;
    write_prot_n    <= sensor_i;
    bank_is_ram     <= bank_is_ram_i;
    dirty_led_n     <= not any_dirty;
    use_c64_reset   <= use_c64_reset_i;
end rtl;
