library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.cart_slot_pkg.all;

entity cart_slot_registers is
generic (
    g_cartreset_init: std_logic := '0';
    g_boot_stop     : boolean := false;
    g_kernal_repl   : boolean := true;
    g_ram_expansion : boolean := true );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    
    control         : out t_cart_control;
    status          : in  t_cart_status );

end entity;

architecture rtl of cart_slot_registers is
    signal control_i    : t_cart_control;
begin
    
    control  <= control_i;
    
    p_bus: process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            control_i.cartridge_kill <= '0'; 
            control_i.cartridge_force <= '0';
            
            if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_cart_c64_mode =>
                    if io_req.data(2)='1' then
                        control_i.c64_reset <= '1';
                    elsif io_req.data(3)='1' then
                        control_i.c64_reset <= '0';
                    else
                        control_i.c64_ultimax <= io_req.data(1);
                        control_i.c64_nmi     <= io_req.data(4);
                    end if;
                when c_cart_c64_stop =>
                    control_i.c64_stop  <= io_req.data(0);
                when c_cart_c64_stop_mode =>
                    control_i.c64_stop_mode <= io_req.data(1 downto 0);
                when c_cart_cartridge_type =>
                    control_i.cartridge_type <= io_req.data(4 downto 0);
                    control_i.cartridge_variant <= io_req.data(7 downto 5);
                when c_cart_cartridge_kill =>
                    control_i.cartridge_kill <= io_req.data(0);
                    control_i.cartridge_force <= io_req.data(1);
                when c_cart_kernal_enable =>
                    if g_kernal_repl then
                        control_i.kernal_enable <= io_req.data(0);
                        control_i.kernal_16k <= io_req.data(1);
                    end if;
                when c_cart_reu_enable =>
                    control_i.reu_enable <= io_req.data(0);
                when c_cart_reu_size =>
                    control_i.reu_size <= io_req.data(2 downto 0);
                when c_cart_serve_control =>
                    control_i.serve_while_stopped <= io_req.data(0);
                when c_cart_timing =>
                    control_i.timing_addr_valid <= unsigned(io_req.data(2 downto 0)); 
                when c_cart_phi2_recover =>
                    control_i.phi2_edge_recover <= io_req.data(0);
                when c_cart_swap_buttons =>
                	control_i.swap_buttons <= io_req.data(0);
                when c_cart_sampler_enable =>
                    control_i.sampler_enable <= io_req.data(0);
                when others =>
                    null;
                end case;
            elsif io_req.read='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_cart_c64_mode =>
                    io_resp.data(1) <= control_i.c64_ultimax;
                    io_resp.data(2) <= control_i.c64_reset;
                    io_resp.data(4) <= control_i.c64_nmi;
                when c_cart_c64_stop =>
                    io_resp.data(0) <= control_i.c64_stop;
                    io_resp.data(1) <= status.c64_stopped;
                when c_cart_c64_stop_mode =>
                    io_resp.data(1 downto 0) <= control_i.c64_stop_mode;
                when c_cart_c64_clock_detect =>
                    io_resp.data(0) <= status.clock_detect;
                    io_resp.data(1) <= status.c64_vcc;
                    io_resp.data(2) <= status.exrom;
                    io_resp.data(3) <= status.game;
                    io_resp.data(4) <= status.reset_in;
                    io_resp.data(5) <= status.nmi;
                when c_cart_cartridge_type =>
                    io_resp.data(4 downto 0) <= control_i.cartridge_type;
                    io_resp.data(7 downto 5) <= control_i.cartridge_variant;
                when c_cart_cartridge_active =>
                    io_resp.data(0) <= status.cart_active;
                when c_cart_kernal_enable =>
                    io_resp.data(0) <= control_i.kernal_enable;
                    io_resp.data(1) <= control_i.kernal_16k;
                when c_cart_reu_enable =>
                    io_resp.data(0) <= control_i.reu_enable;
                when c_cart_reu_size =>
                    io_resp.data(2 downto 0) <= control_i.reu_size;
                when c_cart_serve_control =>
                    io_resp.data(0) <= control_i.serve_while_stopped;
                when c_cart_sampler_enable =>
                    io_resp.data(0) <= control_i.sampler_enable;
                when c_cart_timing =>
                    io_resp.data(2 downto 0) <= std_logic_vector(control_i.timing_addr_valid); 
                when c_cart_phi2_recover =>
                    io_resp.data(0) <= control_i.phi2_edge_recover;
                when c_cart_swap_buttons =>
                	io_resp.data(0) <= control_i.swap_buttons;
                when others =>
                    null;
                end case;
            end if;
                        
            if reset='1' then
                control_i <= c_cart_control_init;
                control_i.c64_reset <= g_cartreset_init;
                if g_boot_stop then
                    control_i.c64_stop <= '1';
                    control_i.c64_stop_mode <= "10";
                end if;
            end if;
        end if;
    end process;
end architecture;
