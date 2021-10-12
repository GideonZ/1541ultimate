library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_cmd_pkg.all;

entity usb_debug is
generic (
    g_enabled   : boolean := false );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    cmd_req     : in  t_usb_cmd_req;
    cmd_resp    : in  t_usb_cmd_resp;
    
    debug_data  : out std_logic_vector(31 downto 0) := (others => '0');
    debug_valid : out std_logic := '0' );
    
end entity;

architecture breakout of usb_debug is
    type t_state is (idle, wait_response);
    signal state    : t_state;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            debug_data <= (others => '0');
            debug_valid <= '0';
            
            if g_enabled then
                case state is
                when idle =>
                    if cmd_req.request = '1' then
                        state <= wait_response;
                        
                        debug_data(9 downto 0)   <= std_logic_vector(cmd_req.data_length);
                        debug_data(10)           <= cmd_req.no_data;
                        debug_data(11)           <= cmd_req.togglebit;
                        debug_data(13 downto 12) <= std_logic_vector(to_unsigned(t_usb_command'pos(cmd_req.command), 2));
                        debug_data(14)           <= cmd_req.do_split;
                        debug_data(15)           <= cmd_req.do_data;
                        debug_data(19 downto 16) <= std_logic_vector(cmd_req.device_addr(3 downto 0));
                        debug_data(21 downto 20) <= std_logic_vector(cmd_req.endp_addr(1 downto 0));
                        debug_data(23 downto 22) <= std_logic_vector(cmd_req.split_port_addr(1 downto 0));
                        debug_data(26 downto 24) <= std_logic_vector(cmd_req.split_hub_addr(2 downto 0));
                        debug_data(27)           <= cmd_req.split_sc;
                        debug_data(28)           <= cmd_req.split_sp;
                        debug_data(30 downto 29) <= cmd_req.split_et; 
                        debug_data(31)           <= '0'; -- command
                        debug_valid <= '1';
                    end if;
                
                when wait_response =>
                    if cmd_resp.done = '1' then
                        debug_data(9 downto 0)   <= std_logic_vector(cmd_resp.data_length);
                        debug_data(10)           <= cmd_resp.no_data;
                        debug_data(11)           <= cmd_resp.togglebit;
                        debug_data(14 downto 12) <= std_logic_vector(to_unsigned(t_usb_result'pos(cmd_resp.result), 3));
                        debug_data(18 downto 16) <= cmd_resp.error_code;
                        debug_data(31) <= '1'; -- response
    
                        debug_valid <= '1';
                        state <= idle;
                    end if;
                end case;
            end if;
                        
            if reset = '1' then
                state <= idle;
            end if;
        end if;
    end process;

end architecture;
