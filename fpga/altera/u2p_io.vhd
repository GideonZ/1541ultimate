library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

use work.io_bus_pkg.all;

entity u2p_io is
port (
	clock       : in  std_logic;
	reset       : in  std_logic;

    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

    mdc         : out std_logic;
    mdio_i      : in  std_logic;
    mdio_o      : out std_logic;
    
    i2c_scl_i   : in  std_logic;
    i2c_sda_i   : in  std_logic;
    i2c_scl_o   : out std_logic;
    i2c_sda_o   : out std_logic;

    iec_i       : in  std_logic_vector(3 downto 0) := X"F";
    iec_o       : out std_logic_vector(3 downto 0);

    value1      : in  unsigned(7 downto 0) := X"00";
    value2      : in  unsigned(7 downto 0) := X"00";
    value3      : in  unsigned(7 downto 0) := X"00";

    board_rev   : in  std_logic_vector(4 downto 0);    
    eth_irq_i   : in  std_logic;
    hub_reset_n : out std_logic;
    speaker_en  : out std_logic;
    ulpi_reset  : out std_logic;
    speaker_vol : out std_logic_vector(3 downto 0);
    buffer_en   : out std_logic  );
end entity;

architecture rtl of u2p_io is
    signal mdc_out  : std_logic;
    signal speaker_en_i : std_logic;
    signal speaker_vol_i : std_logic_vector(3 downto 0) := X"0";
    signal hub_reset_i  : std_logic;
    signal ulpi_reset_i : std_logic;
begin
    process(clock, reset)
        variable local  : unsigned(7 downto 0) := X"00";
    begin
        if reset = '1' then -- switched to asynchronous reset
            i2c_scl_o <= '1';
            i2c_sda_o <= '1';
            mdc_out <= '1';
            mdio_o <= '1';
            speaker_en_i <= '0';
            hub_reset_i <= '0';
            ulpi_reset_i <= '0';
            buffer_en <= '0';
            iec_o <= (others => '1');
        elsif rising_edge(clock) then
            local(4 downto 0) := io_req.address(4 downto 0);
            io_resp <= c_io_resp_init;
            
            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local is
                when X"00" | X"01" | X"08" =>
                    io_resp.data(0) <= i2c_scl_i;
                when X"02" | X"03" | X"09" =>
                    io_resp.data(0) <= i2c_sda_i;
                when X"04" | X"05" | X"0A" =>
                    io_resp.data(0) <= mdc_out;
                when X"06" | X"07" | X"0B" =>
                    io_resp.data(0) <= mdio_i;
                when X"0C" =>
                    io_resp.data(0) <= speaker_en_i;
                    io_resp.data(7 downto 3) <= board_rev;
                when X"0D" =>
                    io_resp.data(0) <= hub_reset_i;
                when X"0E" =>
                    io_resp.data(0) <= eth_irq_i;
                    io_resp.data(7 downto 4) <= iec_i;
                when X"0F" =>
                    io_resp.data(0) <= ulpi_reset_i;
                when X"10" =>
                    io_resp.data <= std_logic_vector(value1);
                when X"11" =>
                    io_resp.data <= std_logic_vector(value2);
                when X"12" =>
                    io_resp.data <= std_logic_vector(value3);
                when others =>
                    null;
                end case;
            end if;
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case local is
                when X"00" =>
                    i2c_scl_o <= '0';
                when X"01" =>
                    i2c_scl_o <= '1';
                when X"02" =>
                    i2c_sda_o <= '0';
                when X"03" =>
                    i2c_sda_o <= '1';
                when X"04" =>
                    mdc_out <= '0';
                when X"05" =>
                    mdc_out <= '1';
                when X"06" =>
                    mdio_o <= '0';
                when X"07" =>
                    mdio_o <= '1';
                when X"08" =>
                    i2c_scl_o <= io_req.data(0);
                when X"09" =>
                    i2c_sda_o <= io_req.data(0);
                when X"0A" =>
                    mdc_out <= io_req.data(0);
                when X"0B" =>
                    mdio_o <= io_req.data(0);
                when X"0C" =>
                    speaker_en_i <= io_req.data(0);
                    speaker_vol_i <= io_req.data(4 downto 1);
                when X"0D" =>
                    hub_reset_i <= io_req.data(0);
                when X"0E" =>
                    iec_o <= io_req.data(7 downto 4);
                when X"0F" =>
                    ulpi_reset_i <= io_req.data(0);
                    if io_req.data(7) = '1' then
                        buffer_en <= '1';
                    elsif io_req.data(6) = '1' then
                        buffer_en <= '0';
                    end if;
                when others =>
                    null;
                end case;
            end if;
        end if;
    end process;

    mdc     <= mdc_out;
    speaker_en <= speaker_en_i;
    speaker_vol <= speaker_vol_i;
    hub_reset_n <= not (hub_reset_i or reset);
    ulpi_reset  <= ulpi_reset_i or reset;
    
end architecture;
