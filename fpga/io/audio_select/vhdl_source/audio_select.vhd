library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity audio_select is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    req             : in  t_io_req;
    resp            : out t_io_resp;
    
    select_left     : out std_logic_vector(3 downto 0);
    select_right    : out std_logic_vector(3 downto 0) );

end audio_select;

architecture gideon of audio_select is
    signal left_select  : std_logic_vector(3 downto 0);
    signal right_select : std_logic_vector(3 downto 0);
begin
    select_left <= left_select;
    select_right <= right_select;

    process(clock)
    begin
        if rising_edge(clock) then
            -- bus handling
            resp <= c_io_resp_init;
            if req.write='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    left_select <= req.data(3 downto 0);
                when X"1" =>
                    right_select <= req.data(3 downto 0);
                when others =>
                    null;
                end case;
            elsif req.read='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    resp.data(3 downto 0) <= left_select;
                when X"1" =>
                    resp.data(3 downto 0) <= right_select;
                when others =>
                    null;
                end case;
            end if;

            if reset='1' then
                left_select  <= "0000";
                right_select <= "0000";
            end if;
        end if;
    end process;
end gideon;
