library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.icap_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity icap is
generic (
    g_fpga_type	: std_logic_vector(7 downto 0) := X"3A" );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp );

end icap;

architecture spartan_3a of icap is
    type t_state is (idle, pulse, hold);
    signal state        : t_state;
    signal icap_cen     : std_logic := '1';
    signal icap_data    : std_logic_vector(0 to 7);
    signal icap_clk     : std_logic := '0';

    function swap_bits(s : std_logic_vector) return std_logic_vector is
        variable in_vec     : std_logic_vector(s'length downto 1) := s;
        variable out_vec    : std_logic_vector(1 to s'length);
    begin
        for i in in_vec'range loop
            out_vec(i) := in_vec(i);
        end loop;
        return out_vec;     
    end swap_bits;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;

            case state is
            when idle =>
                if io_req.write='1' then
                    case io_req.address(3 downto 0) is
                    when c_icap_pulse =>
                        icap_data   <= swap_bits(io_req.data);
                        icap_cen    <= '0';
                        state       <= pulse;
                    when c_icap_write =>
                        icap_data   <= swap_bits(io_req.data);
                        icap_cen    <= '1';
                        state       <= pulse;
                    when others =>
                        io_resp.ack <= '1';
                    end case;
                elsif io_req.read='1' then
                    io_resp.ack  <= '1';
    
                    case io_req.address(3 downto 0) is
                    when c_icap_fpga_type =>
                        io_resp.data <= g_fpga_type;
                    when others =>
                        null;
                    end case;
                end if;
            when pulse =>
                icap_clk <= '1';
                state <= hold;
            when hold =>
                icap_clk <= '0';
                io_resp.ack <= '1';
                state <= idle;
            when others =>
                null;
            end case;

            if reset='1' then
                state       <= idle;
                icap_data   <= X"00";
                icap_cen    <= '1';
                icap_clk    <= '0';
            end if;
        end if;
    end process;

    i_icap: ICAP_SPARTAN3A
    port map (
        CLK     => icap_clk,
        CE      => icap_cen,
        WRITE   => icap_cen,
        I       => icap_data,
        O       => open,
        BUSY    => open );

end architecture;
