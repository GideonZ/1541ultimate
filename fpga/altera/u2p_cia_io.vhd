library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;

use work.io_bus_pkg.all;

entity u2p_cia_io is
port (
	clock       : in  std_logic;
	reset       : in  std_logic;

    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

    addr        : out std_logic_vector(3 downto 0);
    cs_n        : out std_logic;
    cs2         : out std_logic;
    reset_n     : out std_logic;
    rw_n        : out std_logic;
    phi2        : out std_logic;
    irq_n       : in  std_logic;
    
    db_to_cia   : out std_logic_vector(7 downto 0);
    db_from_cia : in  std_logic_vector(7 downto 0);
    db_drive    : out std_logic;

    pa_to_cia   : out std_logic_vector(7 downto 0);
    pa_from_cia : in  std_logic_vector(7 downto 0);
    pa_drive    : out std_logic_vector(7 downto 0);
    
    pb_to_cia   : out std_logic_vector(7 downto 0);
    pb_from_cia : in  std_logic_vector(7 downto 0);
    pb_drive    : out std_logic_vector(7 downto 0);
    
    hs_to_cia   : out std_logic_vector(4 downto 0);
    hs_from_cia : in  std_logic_vector(4 downto 0);
    hs_drive    : out std_logic_vector(4 downto 0) );

end entity;

architecture rtl of u2p_cia_io is
    signal reg_addr        : std_logic_vector(3 downto 0);
    signal reg_cs_n        : std_logic;
    signal reg_cs2         : std_logic;
    signal reg_reset_n     : std_logic;
    signal reg_rw_n        : std_logic;
    signal reg_phi2        : std_logic;
    signal reg_hs_to_cia   : std_logic_vector(4 downto 0);
begin
    process(clock, reset)
        variable local  : unsigned(3 downto 0);
    begin
        if reset = '1' then -- switched to asynchronous reset
            reg_addr    <= X"0";
            reg_cs_n    <= '1';
            reg_cs2     <= '0';
            reg_reset_n <= '1';
            reg_rw_n    <= '1';
            reg_phi2    <= '0';

            db_to_cia   <= (others => '0');
            pa_to_cia   <= (others => '0');
            pa_drive    <= (others => '0');
            pb_to_cia   <= (others => '0');
            pb_drive    <= (others => '0');
            reg_hs_to_cia   <= (others => '0');
            hs_drive    <= (others => '0');

        elsif rising_edge(clock) then
            local := io_req.address(3 downto 0);
            io_resp <= c_io_resp_init;
            
            if io_req.read = '1' then
                io_resp.ack <= '1';
                case local is
                when X"0" =>
                    io_resp.data(3 downto 0) <= reg_addr;
                when X"1" =>
                    io_resp.data <= db_from_cia;
                when X"2" | X"3" =>
                    io_resp.data(0) <= reg_cs_n;
                    io_resp.data(1) <= reg_cs2;
                    io_resp.data(2) <= reg_reset_n;
                    io_resp.data(3) <= reg_rw_n;
                    io_resp.data(4) <= reg_phi2;
                when X"4" | X"5" =>
                    io_resp.data <= pa_from_cia;
                when X"6" | X"7" =>
                    io_resp.data <= pb_from_cia;
                when X"8" | X"9" =>
                    io_resp.data(4 downto 0) <= hs_from_cia;
                    io_resp.data(5) <= irq_n;
                when others =>
                    null;
                end case;
            end if;
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case local is
                when X"0" =>
                    reg_addr <= io_req.data(3 downto 0);
                when X"1" =>
                    db_to_cia <= io_req.data;
                when X"2" =>
                    reg_cs_n    <= reg_cs_n    or io_req.data(0);
                    reg_cs2     <= reg_cs2     or io_req.data(1);
                    reg_reset_n <= reg_reset_n or io_req.data(2);
                    reg_rw_n    <= reg_rw_n    or io_req.data(3);
                    reg_phi2    <= reg_phi2    or io_req.data(4);
                when X"3" =>
                    reg_cs_n    <= reg_cs_n    and not io_req.data(0);
                    reg_cs2     <= reg_cs2     and not io_req.data(1);
                    reg_reset_n <= reg_reset_n and not io_req.data(2);
                    reg_rw_n    <= reg_rw_n    and not io_req.data(3);
                    reg_phi2    <= reg_phi2    and not io_req.data(4);
                when X"4" =>
                    pa_to_cia   <= io_req.data;
                when X"5" =>
                    pa_drive    <= io_req.data;
                when X"6" =>
                    pb_to_cia   <= io_req.data;
                when X"7" =>
                    pb_drive    <= io_req.data;
                when X"8" =>
                    reg_hs_to_cia  <= reg_hs_to_cia or io_req.data(4 downto 0);
                when X"9" =>
                    reg_hs_to_cia  <= reg_hs_to_cia and not io_req.data(4 downto 0);
                when X"A" =>
                    hs_drive    <= io_req.data(4 downto 0);
                    
                when others =>
                    null;
                end case;
            end if;
        end if;
    end process;
    
    addr    <= reg_addr;
    cs_n    <= reg_cs_n;
    cs2     <= reg_cs2;
    reset_n <= reg_reset_n;
    rw_n    <= reg_rw_n;
    phi2    <= reg_phi2;
    hs_to_cia <= reg_hs_to_cia;
    
    db_drive <= '1' when reg_rw_n = '0' and reg_cs_n = '0' and reg_cs2 = '1' else '0';
end architecture;
