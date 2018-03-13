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
    signal mdio_out : std_logic;
    signal i2c_scl_out : std_logic;
    signal i2c_sda_out : std_logic;
    signal speaker_en_i : std_logic;
    signal speaker_vol_i : std_logic_vector(3 downto 0);
    signal hub_reset_i  : std_logic;
    signal ulpi_reset_i : std_logic;
begin
    process(clock, reset)
        variable local  : unsigned(3 downto 0);
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
            local := io_req.address(3 downto 0);
            io_resp <= c_io_resp_init;
            
            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local is
                when X"0" | X"1" | X"8" =>
                    io_resp.data(0) <= i2c_scl_i;
                when X"2" | X"3" | X"9" =>
                    io_resp.data(0) <= i2c_sda_i;
                when X"4" | X"5" | X"A" =>
                    io_resp.data(0) <= mdc_out;
                when X"6" | X"7" | X"B" =>
                    io_resp.data(0) <= mdio_i;
                when X"C" =>
                    io_resp.data(0) <= speaker_en_i;
                    io_resp.data(7 downto 3) <= board_rev;
                when X"D" =>
                    io_resp.data(0) <= hub_reset_i;
                when X"E" =>
                    io_resp.data(0) <= eth_irq_i;
                    io_resp.data(7 downto 4) <= iec_i;
                when X"F" =>
                    io_resp.data(0) <= ulpi_reset_i;
                when others =>
                    null;
                end case;
            end if;
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case local is
                when X"0" =>
                    i2c_scl_o <= '0';
                when X"1" =>
                    i2c_scl_o <= '1';
                when X"2" =>
                    i2c_sda_o <= '0';
                when X"3" =>
                    i2c_sda_o <= '1';
                when X"4" =>
                    mdc_out <= '0';
                when X"5" =>
                    mdc_out <= '1';
                when X"6" =>
                    mdio_o <= '0';
                when X"7" =>
                    mdio_o <= '1';
                when X"8" =>
                    i2c_scl_o <= io_req.data(0);
                when X"9" =>
                    i2c_sda_o <= io_req.data(0);
                when X"A" =>
                    mdc_out <= io_req.data(0);
                when X"B" =>
                    mdio_o <= io_req.data(0);
                when X"C" =>
                    speaker_en_i <= io_req.data(0);
                    speaker_vol_i <= io_req.data(4 downto 1);
                when X"D" =>
                    hub_reset_i <= io_req.data(0);
                when X"E" =>
                    iec_o <= io_req.data(7 downto 4);
                when X"F" =>
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
