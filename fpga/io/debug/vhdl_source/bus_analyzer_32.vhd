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
    
    cart_led_n  : in  std_logic;
    ---
    mem_req     : out t_mem_req_32;
    mem_resp    : in  t_mem_resp_32;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp );
    
end entity;

architecture gideon of bus_analyzer_32 is
    type t_state is (idle, writing);
    signal enable_log   : std_logic;
    signal ev_addr      : unsigned(24 downto 0);
    signal state        : t_state;
    signal vector_in    : std_logic_vector(31 downto 0);
    signal vector_d1    : std_logic_vector(31 downto 0);
    signal vector_d2    : std_logic_vector(31 downto 0);
    signal vector_d3    : std_logic_vector(31 downto 0);
    signal vector_d4    : std_logic_vector(31 downto 0);
    signal mem_wdata    : std_logic_vector(31 downto 0);
    signal mem_request  : std_logic;
    signal phi_c        : std_logic := '0';
    signal phi_d1       : std_logic := '0';
    signal phi_d2       : std_logic := '0';
    signal dman_c       : std_logic := '0';
    signal io           : std_logic;
    signal interrupt    : std_logic;
    signal rom          : std_logic;
begin
    io <= io1n and io2n;
    rom <= romln and romhn;
    interrupt <= irqn and nmin;
    
    vector_in <= phi2 & gamen & exromn & ba & irqn & rom & nmin & rwn & data & addr;
    --vector_in <= phi2 & gamen & exromn & ba & interrupt & rom & io & rwn & data & addr;

    process(clock)
    begin
        if rising_edge(clock) then
            dman_c <= dman;
            phi_c <= phi2;
            phi_d1 <= phi_c;
            phi_d2 <= phi_d1;
                     
            vector_d1 <= vector_in;
            vector_d2 <= vector_d1;
            vector_d3 <= vector_d2;
            vector_d4 <= vector_d3;

            case state is
            when idle =>
                mem_wdata <= byte_swap(vector_d4, g_big_endian);
                if enable_log = '1' and dman_c = '1' and cart_led_n = '1' then -- only when the cartridge is off
                    if phi_d2 /= phi_d1 then
                        mem_request <= '1';
                        state <= writing;            
                    end if;
                end if;
                
            when writing =>
                if mem_resp.rack='1' and mem_resp.rack_tag=X"F0" then
                    ev_addr <= ev_addr + 4;
                    if ev_addr = 16#1FFFFF8# then
                        enable_log <= '0';
                    end if;
                    mem_request <= '0';
                    state <= idle;
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
                case io_req.address(2 downto 0) is
                when "111" =>
                    ev_addr <= (others => '0');
                    enable_log <= '1';
                when "110" =>
                    enable_log <= '0';
                when others =>
                    null;
                end case;                    
            end if;

            if reset='1' then
                state      <= idle;
                enable_log <= '0';
                mem_request <= '0';
                mem_wdata   <= (others => '0');
                ev_addr    <= (others => '0');
            end if;
        end if;
    end process;

    mem_req.data        <= mem_wdata;
    mem_req.request     <= mem_request;
    mem_req.tag         <= X"F0";
    mem_req.address     <= "1" & unsigned(ev_addr);
    mem_req.read_writen <= '0'; -- write only
    mem_req.byte_en     <= "1111";
end gideon;
