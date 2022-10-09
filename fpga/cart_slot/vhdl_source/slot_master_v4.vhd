library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.dma_bus_pkg.all;

entity slot_master_v4 is
generic (
    g_start_in_stopped_state    : boolean := false );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;

    -- Cartridge pins
    DMAn            : out std_logic := '1';
    BA              : in  std_logic := '0';
    RWn_in          : in  std_logic;
    RWn_out         : out std_logic;
    RWn_tri         : out std_logic;
    
    ADDRESS_out     : out std_logic_vector(15 downto 0);
    ADDRESS_tri_h   : out std_logic;
    ADDRESS_tri_l   : out std_logic;
    
    DATA_in         : in  std_logic_vector(7 downto 0);
    DATA_out        : out std_logic_vector(7 downto 0) := (others => '1');
    DATA_tri        : out std_logic;

    -- timing inputs
    vic_cycle       : in  std_logic;
    phi2_recovered  : in  std_logic;
    phi2_tick       : in  std_logic;
    do_sample_addr  : in  std_logic;
    do_io_event     : in  std_logic;
    reu_dma_n       : in  std_logic := '1';
    cmd_if_freeze   : in  std_logic := '0';
        
    -- request from the memory controller to do a cycle on the cart bus
    dma_req         : in  t_dma_req;
    dma_resp        : out t_dma_resp;

    -- system control
    stop_cond       : in  std_logic_vector(1 downto 0);
    c64_stop        : in  std_logic; -- condition to start looking for a stop moment
    c64_stopped     : out std_logic ); -- indicates that stopping has succeeded

end slot_master_v4;    

architecture gideon of slot_master_v4 is
    signal dma_n_i      : std_logic := '1';
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    signal data_d       : t_byte_array(0 to 7);
    signal addr_out     : std_logic_vector(15 downto 0) := (others => '1');
    signal drive_ah     : std_logic;
    signal drive_al     : std_logic;
    signal dma_ack_i    : std_logic;
    signal rwn_out_i    : std_logic;
    signal phi2_dly     : std_logic_vector(6 downto 0) := (others => '0');
    signal reu_active   : std_logic;
    signal cmd_if_active: std_logic;
    constant match_ptrn : std_logic_vector(1 downto 0) := "01"; -- read from left to right
    signal rwn_hist     : std_logic_vector(3 downto 0);
    signal ba_count     : integer range 0 to 15;
    signal c64_stopped_i: std_logic;
    
    attribute keep : string;
    attribute keep of rwn_hist : signal is "true";

    type   t_state is (idle, stopped, do_dma );
    signal state     : t_state;
    
    signal dma_rack     : std_logic;

begin
    process(clock)
        variable stop : boolean;
    begin
        if rising_edge(clock) then
            data_d(0) <= DATA_in;
            data_d(1 to 7) <= data_d(0 to 6);
            dma_rack  <= '0';
            phi2_dly  <= phi2_dly(phi2_dly'high-1 downto 0) & phi2_recovered;
            
            -- consecutive write counter (match counter)
            if do_sample_addr='1' and phi2_recovered='1' then -- count CPU cycles only (sample might become true for VIC cycles)
                -- the following statement detects a bad line. VIC wants bus. We just wait in line until VIC is done,
                -- by pulling DMA low halfway the bad line. This is the safest method.
                if BA='0' then
                    if ba_count /= 15 then
                        ba_count <= ba_count + 1;
                    end if;
                else
                    ba_count <= 0;
                end if;

                -- the following statement detects a rw/n pattern, that indicates that it's safe to pull dma low
                rwn_hist <= rwn_hist(rwn_hist'high-1 downto 0) & RWn_in;
            end if;            

            case stop_cond is
                when "00"   => stop := (ba_count = 14);
                when "01"   => stop := (rwn_hist(match_ptrn'range) = match_ptrn);
                when others => stop := true;
            end case;

            case state is
            when idle =>
                dma_ack_i <= dma_req.request and dma_req.read_writen; -- do not serve DMA requests when machine is not stopped
                dma_rack  <= dma_req.request;
                
                if reu_dma_n='0' then
                    reu_active <= '1';
                    dma_n_i <= '0';
                    state <= stopped;
                elsif cmd_if_freeze='1' then
                    cmd_if_active <= '1';
                    dma_n_i <= '0';
                    state <= stopped;
                elsif c64_stop='1' and do_io_event='1' then
                    if stop then
                        dma_n_i <= '0';
                        state <= stopped;
                        c64_stopped_i <= '1';
                    end if;
                end if;
                
            when stopped =>
                dma_ack_i <= '0';
                rwn_out_i <= '1';
                addr_out(15 downto 14) <= "00"; -- always in a safe area
                -- is the dma request active and are we at the right time?
                if dma_req.request='1' and dma_ack_i='0' and phi2_tick='1' and BA='1' then
                    dma_rack  <= '1';
                    DATA_out  <= dma_req.data;
                    addr_out  <= std_logic_vector(dma_req.address);
                    rwn_out_i <= dma_req.read_writen;
                    state     <= do_dma;
                elsif reu_active='1' and reu_dma_n='1' and do_io_event='1' then
                    dma_n_i  <= '1';
                    reu_active <= '0';
                    state   <= idle;
                elsif cmd_if_active='1' and cmd_if_freeze='0' and do_io_event='1' then
                    dma_n_i  <= '1';
                    cmd_if_active <= '0';
                    state   <= idle;
                elsif c64_stopped_i = '1' and c64_stop = '0' and do_io_event='1' then
                    if stop then
                        c64_stopped_i <= '0';
                        dma_n_i  <= '1';
                        state   <= idle;
                    end if;
                end if;
          
            when do_dma =>
                if phi2_recovered='0' then -- end of CPU cycle
                --if do_io_event='1' then
                    dma_ack_i <= '1';
                    state <= stopped;
                end if;

            when others =>
                null;                    

            end case;

            if reset='1' then
                reu_active <= '0';
                cmd_if_active <= '0';
                if g_start_in_stopped_state then
                    state           <= stopped;
                    dma_n_i         <= '0';
                    c64_stopped_i   <= '1';
                else
                    state           <= idle;
                    dma_n_i         <= '1';
                    c64_stopped_i   <= '0';
                end if;
                rwn_out_i       <= '1';
                addr_out        <= (others => '1');
            end if;
        end if;
    end process;
    
    c64_stopped   <= c64_stopped_i;

    dma_resp.dack <= dma_ack_i and rwn_out_i; -- only data-acknowledge reads
    dma_resp.rack <= dma_rack;
    dma_resp.data <= data_d(2) when dma_ack_i='1' and rwn_out_i='1' else X"00";
    
    -- by shifting the phi2 and anding it with the original, we make the write enable narrower,
    -- starting only halfway through the cycle. We should not be too fast!
    DATA_tri      <= '1' when (dma_n_i='0' and phi2_recovered='1' and phi2_dly(phi2_dly'high)='1' and
                               vic_cycle='0' and rwn_out_i='0') else '0';
                               
    drive_ah <= drive_al when rising_edge(clock); -- one cycle later on (SSO)
    drive_al <= not dma_n_i and phi2_recovered and not vic_cycle;

    ADDRESS_out   <= addr_out;
    ADDRESS_tri_h <= drive_ah and drive_al;
    ADDRESS_tri_l <= drive_al;
    RWn_tri       <= drive_al;
    RWn_out       <= rwn_out_i;
    DMAn          <= dma_n_i;

end gideon;
