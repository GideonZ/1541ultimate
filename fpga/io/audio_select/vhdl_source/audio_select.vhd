library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- LUT/FF/S3S/BRAM: 242/130/136/1

library work;
use work.io_bus_pkg.all;

entity audio_select is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    req             : in  t_io_req;
    resp            : out t_io_resp;
    
    drive0          : in  std_logic;
    drive1          : in  std_logic;
    cas_read        : in  std_logic;
    cas_write       : in  std_logic;
    sid_left        : in  std_logic;
    sid_right       : in  std_logic;
    samp_left       : in  std_logic;
    samp_right      : in  std_logic;
    
    pwm_out         : out std_logic_vector(1 downto 0) );

end audio_select;

architecture gideon of audio_select is
    signal left_select  : std_logic_vector(2 downto 0);
    signal right_select : std_logic_vector(2 downto 0);
    signal in_vec       : std_logic_vector(0 to 7);
begin

    in_vec <= drive0 & drive1 & cas_read & cas_write & sid_left & sid_right & samp_left & samp_right;

    process(clock)
    begin
        if rising_edge(clock) then
            -- bus handling
            resp <= c_io_resp_init;
            if req.write='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    left_select <= req.data(2 downto 0);
                when X"1" =>
                    right_select <= req.data(2 downto 0);
                when others =>
                    null;
                end case;
            elsif req.read='1' then
                resp.ack <= '1';
                case req.address(3 downto 0) is
                when X"0" =>
                    resp.data(2 downto 0) <= left_select;
                when X"1" =>
                    resp.data(2 downto 0) <= right_select;
                when others =>
                    null;
                end case;
            end if;

            pwm_out(0) <= in_vec(to_integer(unsigned(left_select)));
            pwm_out(1) <= in_vec(to_integer(unsigned(right_select)));

            if reset='1' then
                left_select  <= "000";
                right_select <= "000";
            end if;
        end if;
    end process;
end gideon;
