library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.endianness_pkg.all;

entity bus_analyzer_32 is
generic (
    g_big_endian   : boolean );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    addr        : in  std_logic_vector(15 downto 0);
    data        : in  std_logic_vector(7 downto 0);
    rstn        : in  std_logic;
    phi2        : in  std_logic;
    rwn         : in  std_logic;
    ba          : in  std_logic;
    dman        : in  std_logic;
    ROMLn       : in  std_logic;
    ROMHn       : in  std_logic;
    EXROMn      : in  std_logic;
    GAMEn       : in  std_logic;
    IO1n        : in  std_logic;
    IO2n        : in  std_logic;
    IRQn        : in  std_logic;
    NMIn        : in  std_logic;
    
    trigger     : in  std_logic;
    sync        : in  std_logic;
    drv_enable  : out std_logic;
    
    ---
    mem_req     : out t_mem_req_32;
    mem_resp    : in  t_mem_resp_32;

    debug_data  : out std_logic_vector(31 downto 0);
    debug_valid : out std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp );
    
end entity;

architecture gideon of bus_analyzer_32 is
    type t_state is (idle, writing, recording, wait_trigger, wait_sync);
    signal enable_log   : std_logic;
    signal ev_addr      : unsigned(24 downto 0);
    signal state        : t_state;
    signal vector_in    : std_logic_vector(31 downto 0);
    signal vector_c     : std_logic_vector(31 downto 0);
    signal vector_d1    : std_logic_vector(31 downto 0);
    signal vector_d2    : std_logic_vector(31 downto 0);
    signal vector_d3    : std_logic_vector(31 downto 0);
    signal ba_history   : std_logic_vector(2 downto 0) := "000";
    signal debug_data_i : std_logic_vector(31 downto 0) := (others => '0');
    signal debug_valid_i: std_logic := '0';

    signal mem_request  : std_logic;
    signal phi_c        : std_logic := '0';
    signal phi_d1       : std_logic := '0';
    signal phi_d2       : std_logic := '0';
    signal dman_c       : std_logic := '0';
    signal io           : std_logic;
    signal interrupt    : std_logic;
    signal rom          : std_logic;
    signal frame_tick   : std_logic := '0';
    signal counter      : integer range 0 to 312*63; -- PAL
    signal drv_enable_i : std_logic;
    
    signal cpu_cycle_enable : std_logic := '1';
    signal vic_cycle_enable : std_logic := '1';
begin
    io <= io1n and io2n;
    rom <= romln and romhn;
    interrupt <= irqn and nmin;
    
    vector_in <= phi2 & gamen & exromn & ba & irqn & rom & nmin & rwn & data & addr;
    --vector_in <= phi2 & gamen & exromn & ba & irqn & rom & nmin & rwn & data & addr;
    --vector_in <= phi2 & gamen & exromn & ba & interrupt & rom & io & rwn & data & addr;

    debug_data  <= debug_data_i;
    debug_valid <= debug_valid_i;

    process(clock)
    begin
        if rising_edge(clock) then
            dman_c <= dman;
            phi_c <= phi2;
            phi_d1 <= phi_c;
            phi_d2 <= phi_d1;
                     
            vector_c <= vector_in;
            vector_d1 <= vector_c;
            vector_d2 <= vector_d1;
            vector_d3 <= vector_d2;

            -- BA  1 1 1 0 0 0 0 0 0 0 1 1 1
            -- BA0 1 1 1 1 0 0 0 0 0 0 0 1 1
            -- BA1 1 1 1 1 1 0 0 0 0 0 0 0 1
            -- BA2 1 1 1 1 1 1 0 0 0 0 0 0 0
            -- CPU 1 1 1 1 1 1 0 0 0 0 1 1 1 
            -- 
            debug_valid_i <= '0';
            if phi_d2 /= phi_d1 then
                debug_data_i <= vector_d3;
                if phi_d2 = '1' then
                    ba_history <= ba_history(1 downto 0) & vector_d3(28); -- BA position!
                end if;
                
                if phi_d2 = '1' then
                    if (vector_d3(28) = '1' or ba_history /= "000" or drv_enable_i = '1') and cpu_cycle_enable = '1' then
                        debug_valid_i <= '1';
                    elsif vic_cycle_enable = '1' then
                        debug_valid_i <= '1';
                    end if;
                elsif vic_cycle_enable = '1' then
                    debug_valid_i <= '1';
                end if;
            end if;
                        
            if sync = '1' then
                counter <= 312 * 63 - 1;
            elsif phi_d1 = '1' and phi_d2 = '0' then
                if counter = 0 then
                    counter <= 312 * 63 - 1;
                    frame_tick <= '1';
                else
                    counter <= counter - 1;
                    frame_tick <= '0';
                end if;
            end if;

            case state is
            when idle =>
                if enable_log = '1' then
                    state <= wait_trigger;
                end if;
            
            when wait_trigger =>
                if enable_log = '0' then
                    state <= idle;
                elsif trigger = '1' then
                    state <= wait_sync;
                end if;
                
            when wait_sync =>
                if enable_log = '0' then
                    state <= idle;
                elsif frame_tick = '1' then
                    state <= recording;
                end if;

            when recording =>
                if debug_valid_i = '1' then
                    mem_request <= '1';
                    state <= writing;            
                end if;
                
            when writing =>
                if mem_resp.rack='1' and mem_resp.rack_tag=X"F0" then
                    ev_addr <= ev_addr + 4;
                    if ev_addr = 16#1FFFFF4# then
                        enable_log <= '0';
                    end if;
                    mem_request <= '0';
                    if enable_log = '0' then
                        state <= idle;
                    else
                        state <= recording;
                    end if; 
                end if;

            when others =>
                null;
            end case;

            io_resp <= c_io_resp_init;

            if io_req.read='1' then
                io_resp.ack <= '1';
                if g_big_endian then
                    case io_req.address(2 downto 0) is
                    when "011" =>
                        io_resp.data <= std_logic_vector(ev_addr(7 downto 0));
                    when "010" =>
                        io_resp.data <= std_logic_vector(ev_addr(15 downto 8));
                    when "001" =>
                        io_resp.data <= std_logic_vector(ev_addr(23 downto 16));
                    when "000" =>
                        io_resp.data <= "0000001" & ev_addr(24);
                    when others =>
                        null;
                    end case;
                else
                    case io_req.address(2 downto 0) is
                    when "000" =>
                        io_resp.data <= std_logic_vector(ev_addr(7 downto 0));
                    when "001" =>
                        io_resp.data <= std_logic_vector(ev_addr(15 downto 8));
                    when "010" =>
                        io_resp.data <= std_logic_vector(ev_addr(23 downto 16));
                    when "011" =>
                        io_resp.data <= "0000001" & ev_addr(24);
                    when others =>
                        null;
                    end case;
                end if;                
            elsif io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when X"6" =>
                    enable_log <= '0';
                when X"7" =>
                    ev_addr <= (others => '0');
                    enable_log <= '1';
                when X"8" =>
                    cpu_cycle_enable <= io_req.data(0);
                    vic_cycle_enable <= io_req.data(1);
                    drv_enable_i     <= io_req.data(2);
                when others =>
                    null;
                end case;                    
            end if;

            if reset='1' then
                state      <= idle;
                enable_log <= '0';
                mem_request <= '0';
                ev_addr    <= (others => '0');
                drv_enable_i <= '0';
            end if;
        end if;
    end process;

    drv_enable          <= drv_enable_i;
    
    mem_req.data        <= byte_swap(debug_data_i, g_big_endian);
    mem_req.request     <= mem_request;
    mem_req.tag         <= X"F0";
    mem_req.address     <= "1" & unsigned(ev_addr);
    mem_req.read_writen <= '0'; -- write only
    mem_req.byte_en     <= "1111";
end gideon;
