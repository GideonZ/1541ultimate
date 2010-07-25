library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity gcr_encoder is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    req         : in  t_io_req;
    resp        : out t_io_resp );
end gcr_encoder;

architecture regmap of gcr_encoder is
    signal shift_reg    : std_logic_vector(0 to 31);
    signal encoded      : std_logic_vector(0 to 39);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            resp <= c_io_resp_init;
            if req.write='1' then
                resp.ack <= '1';
                shift_reg <= shift_reg(8 to 31) & req.data;
            elsif req.read='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    resp.data <= encoded(0 to 7);
                when X"1" =>
                    resp.data <= encoded(8 to 15);
                when X"2" =>
                    resp.data <= encoded(16 to 23);
                when X"3" =>
                    resp.data <= encoded(24 to 31);
                when others =>
                    resp.data <= encoded(32 to 39);
                end case;
            end if;
            if reset='1' then
                shift_reg <= X"00000000";
            end if;
        end if;
    end process;

    r_encoders: for i in 0 to 7 generate
        i_bin2gcr: entity work.bin2gcr
        port map (
            d_in   => shift_reg(4*i to 3+4*i),
            d_out  => encoded(5*i to 4+5*i) );
    end generate;
end;
