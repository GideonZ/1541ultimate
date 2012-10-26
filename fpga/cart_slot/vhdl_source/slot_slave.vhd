library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.slot_bus_pkg.all;

entity slot_slave is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- Cartridge pins
    RSTn            : in  std_logic;
    IO1n            : in  std_logic;
    IO2n            : in  std_logic;
    ROMLn           : in  std_logic;
    ROMHn           : in  std_logic;
    BA              : in  std_logic;
    GAMEn           : in  std_logic;
    EXROMn          : in  std_logic;
    RWn             : in  std_logic;
    ADDRESS         : in  std_logic_vector(15 downto 0);
    DATA_in         : in  std_logic_vector(7 downto 0);
    DATA_out        : out std_logic_vector(7 downto 0) := (others => '0');
    DATA_tri        : out std_logic;

    -- interface with memory controller
    mem_req         : out std_logic; -- our memory request to serve slot
    mem_rwn         : out std_logic;
    mem_rack        : in  std_logic;
    mem_dack        : in  std_logic;
    mem_rdata       : in  std_logic_vector(7 downto 0);
    mem_wdata       : out std_logic_vector(7 downto 0);
    -- mem_addr comes from cartridge logic

    reset_out       : out std_logic;
    
    -- timing inputs
    phi2_tick       : in  std_logic;
    do_sample_addr  : in  std_logic;
    do_sample_io    : in  std_logic;
    do_io_event     : in  std_logic;

    -- interface with freezer (cartridge) logic
    allow_serve     : in  std_logic := '0'; -- from timing unit (modified version of serve_enable)
    serve_rom       : in  std_logic := '0'; -- ROML or ROMH
    serve_io1       : in  std_logic := '0'; -- IO1n
    serve_io2       : in  std_logic := '0'; -- IO2n
    allow_write     : in  std_logic := '0';
    do_reg_output   : in  std_logic := '0';

    epyx_timeout    : out std_logic; -- '0' => epyx is on, '1' epyx is off    
    cpu_write       : out std_logic; -- for freezer

    slot_req        : out t_slot_req;
    slot_resp       : in  t_slot_resp;

--    slot_addr       : out std_logic_vector(15 downto 0);
--    cpu_write       : out std_logic; -- state, sampled during CPU cycle
--    io_write        : out std_logic;
--    io_read         : out std_logic;
--    io_event_addr   : out std_logic_vector(15 downto 0) := (others => '0');
--    io_rdata        : in  std_logic_vector(7 downto 0);
--    io_wdata        : out std_logic_vector(7 downto 0);

    
    -- interface with hardware
    BUFFER_ENn      : out std_logic );

end slot_slave;    

architecture gideon of slot_slave is
    signal address_c    : std_logic_vector(15 downto 0) := (others => '0');
    signal data_c       : std_logic_vector(7 downto 0) := X"FF";
    signal io1n_c       : std_logic := '1';
    signal io2n_c       : std_logic := '1';
    signal rwn_c        : std_logic := '1';
    signal romhn_c      : std_logic := '1';
    signal romln_c      : std_logic := '1';
    signal ba_c         : std_logic := '0';
    signal dav          : std_logic := '0';
    signal addr_is_io   : boolean;
    signal mem_req_ff   : std_logic;
    signal servicable   : std_logic;
    signal io_out       : boolean := false;
    signal io_read_cond : std_logic;
    signal io_write_cond: std_logic;
    signal late_write_cond  : std_logic;
    signal ultimax      : std_logic;
    signal last_rwn     : std_logic;
    signal mem_wdata_i  : std_logic_vector(7 downto 0);
        
    attribute register_duplication : string;
    attribute register_duplication of rwn_c     : signal is "no";
    attribute register_duplication of io1n_c    : signal is "no";
    attribute register_duplication of io2n_c    : signal is "no";
    attribute register_duplication of romln_c   : signal is "no";
    attribute register_duplication of romhn_c   : signal is "no";
    attribute register_duplication of reset_out : signal is "no";

    type   t_state is (idle, mem_access, wait_end, reg_out);
                       
    attribute iob : string;
    attribute iob of data_c : signal is "true"; 

    signal state     : t_state;
    
--    attribute fsm_encoding : string;
--    attribute fsm_encoding of state : signal is "sequential";

    signal epyx_timer       : unsigned(6 downto 0) := (others => '0');
    signal epyx_reset       : std_logic := '0';
begin
    slot_req.io_write      <= do_io_event and io_write_cond;
    slot_req.io_read       <= do_io_event and io_read_cond;
    slot_req.late_write    <= do_io_event and late_write_cond;
    slot_req.io_read_early <= '1' when (addr_is_io and rwn_c='1' and do_sample_addr='1') else '0';

    process(clock)
    begin
        if rising_edge(clock) then
            -- synchronization
            if mem_req_ff='0' then -- don't change while an access is taking place
                rwn_c     <= RWn;
                address_c <= ADDRESS;
            end if;
            reset_out <= reset or not RSTn;
            ba_c      <= BA;
            io1n_c    <= IO1n;
            io2n_c    <= IO2n;
            romln_c   <= ROMLn;
            romhn_c   <= ROMHn;
            data_c    <= DATA_in;
            ultimax   <= not GAMEn and EXROMn;
            
            if epyx_reset='1' then
                epyx_timer <= (others => '1');
                epyx_timeout <= '0';
            elsif phi2_tick='1' then
                if epyx_timer = "0000000" then
                    epyx_timeout <= '1';
                else
                    epyx_timer <= epyx_timer - 1;
                end if;
            end if;

            slot_req.bus_write <= '0';
            if do_sample_io='1' then
                cpu_write  <= not RWn;

                slot_req.bus_write  <= not RWn;
                slot_req.io_address <= unsigned(address_c);
                mem_wdata_i         <= data_c;

                late_write_cond <= not rwn_c;
                io_write_cond <= not rwn_c and (not io2n_c or not io1n_c);
                io_read_cond  <= rwn_c and (not io2n_c or not io1n_c);
                epyx_reset    <= not io2n_c or not io1n_c or not romln_c or not RSTn;
            end if;
            
            case state is
            when idle =>
                if do_sample_addr='1' then
                    -- register output
                    if slot_resp.reg_output='1' and addr_is_io and rwn_c='1' then -- read register
                        data_out <= slot_resp.data;
                        io_out   <= true;
                        dav      <= '1';
                        state    <= reg_out;

                    elsif allow_serve='1' and servicable='1' and rwn_c='1' then
                        -- memory read
                        io_out <= false;
                        if addr_is_io then
                            if ba_c='1' then -- only serve IO when BA='1' (Fix for Ethernet)
                                mem_req_ff <= '1';
                                state      <= mem_access;
                            end if;
                            if address_c(8)='0' and serve_io1='1' then
                                io_out <= (rwn_c='1');
                            elsif address_c(8)='1' and serve_io2='1' then
                                io_out <= (rwn_c='1');
                            end if;
                        else -- non-IO, always serve
                            mem_req_ff <= '1';
                            state      <= mem_access;
                        end if;
                    end if;
                elsif do_sample_io='1' and rwn_c='0' and allow_write='1' then
                    -- memory write
                    if address_c(14)='1' then -- IO range
                        if io2n_c='0' or io1n_c='0' then
                            mem_req_ff <= '1';
                            state      <= mem_access;
                        end if;
                    else
                        mem_req_ff <= '1';
                        state      <= mem_access;
                    end if;
                end if;
                            
            when mem_access =>
                if mem_rack='1' then
                    mem_req_ff <= '0'; -- clear request
                    if rwn_c='0' then  -- if write, we're done.
                        state <= idle;
                    else -- if read, then we need to wait for the data
                        state <= wait_end;
                    end if;
                end if;
				if mem_dack='1' then -- in case the data comes immediately
                    DATA_out <= mem_rdata;
                    dav      <= '1';
				end if;

            when wait_end =>
				if mem_dack='1' then -- the data is available, put it on the bus!
                    DATA_out <= mem_rdata;
                    dav      <= '1';
				end if;
                if phi2_tick='1' or do_io_event='1' then -- around the clock edges
                    state <= idle;
                    io_out <= false;
                    dav    <= '0';
                end if;

            when reg_out =>
                data_out <= slot_resp.data;

                if phi2_tick='1' or do_io_event='1' then -- around the clock edges
                    state <= idle;
                    io_out <= false;
                    dav    <= '0';
                end if;
                
            when others =>
                null;                    

            end case;

            if (io_out and (io1n_c='0' or io2n_c='0')) or
              ((romln_c='0' or romhn_c='0') and (rwn_c='1')) then
                DATA_tri <= mem_dack or dav;
            else
                DATA_tri <= '0';
            end if;

            if reset='1' then
                last_rwn        <= '1';
                dav             <= '0';
                state           <= idle;
                mem_req_ff      <= '0';
                io_out          <= false;
                io_read_cond    <= '0';
                io_write_cond   <= '0';
                late_write_cond <= '0';
                slot_req.io_address <= (others => '0');
                cpu_write       <= '0';
                epyx_reset      <= '1';
            end if;
        end if;
    end process;
    
    -- combinatoric
    addr_is_io <= (address_c(15 downto 9)="1101111"); -- DE/DF

    process(rwn_c, address_c, addr_is_io, romln_c, romhn_c, serve_rom, serve_io1, serve_io2, ultimax)
    begin
        servicable <= '0';
        if rwn_c='1' then
            if addr_is_io and (serve_io1='1' or serve_io2='1') then
                servicable <= '1';
            end if;
--            if (romln_c='0' or romhn_c='0') and (serve_rom='1') then -- our decode is faster!
            if address_c(15 downto 14)="10" and (serve_rom='1') then -- 8000-BFFF
                servicable <= '1';
            end if;
            if address_c(15 downto 13)="111" and (serve_rom='1') and (ultimax='1') then
                servicable <= '1';
            end if;
        end if;
    end process;

    mem_req    <= mem_req_ff;
    mem_rwn    <= rwn_c;
    mem_wdata  <= mem_wdata_i;
    
    BUFFER_ENn <= '0';
                                
    slot_req.data        <= mem_wdata_i;
    slot_req.bus_address <= unsigned(address_c(15 downto 0));

end gideon;
