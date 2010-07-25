library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity gcr_decoder is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    req         : in  t_io_req;
    resp        : out t_io_resp );
end gcr_decoder;

architecture regmap of gcr_decoder is
    signal shift_reg    : std_logic_vector(0 to 39);
    signal decoded      : std_logic_vector(0 to 31);
    signal errors       : std_logic_vector(0 to 7);
begin
    process(clock)
    begin
        if rising_edge(clock) then
            resp <= c_io_resp_init;
            if req.write='1' then
                resp.ack <= '1';
                shift_reg <= shift_reg(8 to 39) & req.data;
            elsif req.read='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    resp.data <= decoded(0 to 7);
                when X"1" =>
                    resp.data <= decoded(8 to 15);
                when X"2" =>
                    resp.data <= decoded(16 to 23);
                when X"3" =>
                    resp.data <= decoded(24 to 31);
                when X"7" =>
                    resp.data <= decoded(0 to 7);
                when X"6" =>
                    resp.data <= decoded(8 to 15);
                when X"5" =>
                    resp.data <= decoded(16 to 23);
                when X"4" =>
                    resp.data <= decoded(24 to 31);
                when others =>
                    resp.data <= errors;
                end case;
            end if;
            if reset='1' then
                shift_reg <= X"5555555555";
            end if;
        end if;
    end process;

    r_decoders: for i in 0 to 7 generate
        i_gcr2bin: entity work.gcr2bin
        port map (
            d_in   => shift_reg(5*i to 4+5*i),
            d_out  => decoded(4*i to 3+4*i),
            error  => errors(i) );
    end generate;
end;
