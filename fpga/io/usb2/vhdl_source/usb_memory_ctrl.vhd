library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.mem_bus_pkg.all;

-- This module performs the memory operations that are instructed
-- by the nano_cpu. This controller copies data to or from a
-- designated BRAM, and notifies the nano_cpu that the transfer
-- is complete.

entity usb_memory_ctrl is
generic (
    g_tag          : std_logic_vector(7 downto 0) := X"55" );

port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    -- cmd interface
    cmd_addr    : in  std_logic_vector(3 downto 0);
    cmd_valid   : in  std_logic;
    cmd_write   : in  std_logic;
    cmd_wdata   : in  std_logic_vector(15 downto 0);
    cmd_ack     : out std_logic;
    cmd_done    : out std_logic;
    cmd_ready   : out std_logic;

    -- BRAM interface
    ram_addr    : out std_logic_vector(10 downto 2);
    ram_en      : out std_logic;
    ram_we      : out std_logic_vector(3 downto 0);
    ram_wdata   : out std_logic_vector(31 downto 0);
    ram_rdata   : in  std_logic_vector(31 downto 0);
    
    -- memory interface
    mem_req     : out t_mem_req_32;
    mem_resp    : in  t_mem_resp_32 );

end entity;

architecture gideon of usb_memory_ctrl is
    type t_state is (idle, reading, writing, init);
    signal state            : t_state;
    signal mem_addr_r       : unsigned(25 downto 0) := (others => '0');
    signal mem_addr_i       : unsigned(25 downto 2) := (others => '0');
    signal ram_addr_i       : unsigned(8 downto 2) := (others => '0');
    signal mreq             : std_logic := '0';
    signal rwn              : std_logic := '1';
    signal addr_do_load     : std_logic := '0';
    signal new_addr         : std_logic := '0';
    signal addr_do_inc      : std_logic := '0';
    signal rem_do_load      : std_logic;
    signal rem_do_dec       : std_logic;
    signal remain_is_0      : std_logic;
    signal remain_is_1      : std_logic;
    signal buffer_idx       : std_logic_vector(10 downto 9) := "00";
    signal ram_we_i         : std_logic_vector(3 downto 0);
    signal ram_wnext        : std_logic;
    signal rdata_valid      : std_logic;
    signal first_req        : std_logic;
    signal last_req         : std_logic;
begin
    mem_req.tag(7)          <= last_req;
    mem_req.tag(6)          <= first_req;
    mem_req.tag(5 downto 0) <= g_tag(5 downto 0);
    mem_req.request     <= mreq;
    mem_req.address     <= mem_addr_i & mem_addr_r(1 downto 0);
    mem_req.read_writen <= rwn;
    mem_req.data        <= ram_rdata;
    mem_req.byte_en     <= "1111";
    
    -- pop from fifo when we process the access
    cmd_ack <= '1' when (state = idle) and (cmd_valid='1') else '0';

    process(buffer_idx, state, mreq, mem_resp, ram_addr_i, ram_we_i)
    begin
        ram_addr  <= buffer_idx & std_logic_vector(ram_addr_i);
        ram_en <= '0';
        
        -- for writing to memory, we enable the BRAM only when we are going to set
        -- the request, such that the data and the request comes at the same time
        case state is
        when writing =>
            if (mem_resp.rack='1' and mem_resp.rack_tag(5 downto 0) = g_tag(5 downto 0)) or (mreq = '0') then
                ram_en <= '1';
            end if;
        when others =>
            null;
        end case;

        rem_do_dec <= '0';
        if mem_resp.rack='1' and mem_resp.rack_tag(5 downto 0) = g_tag(5 downto 0) then
            rem_do_dec <= '1';
        end if;
        
        -- for reading from memory, it doesn't matter in which state we are:
        if ram_we_i /= "0000" then
            ram_en <= '1';
        end if;
    end process;
    ram_we <= ram_we_i;

    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                rwn <= '1';
                if cmd_valid='1' then
                    if cmd_write='1' then
                        cmd_done <= '0';
                        case cmd_addr is
                        when X"0" =>
                            mem_addr_r(15 downto 0) <= unsigned(cmd_wdata(15 downto 0));
                            new_addr <= '1';
                        when X"1" =>
                            mem_addr_r(25 downto 16) <= unsigned(cmd_wdata(9 downto 0));
                            new_addr <= '1';
                        when X"2" =>
                            rwn <= '0';
                            state <= init;
                        when X"3" => 
                            state <= init;
                        when X"4" =>
                            buffer_idx <= cmd_wdata(15 downto 14);
                        when others =>
                            null;
                        end case;
                    end if;
                end if; 

            when init =>
                new_addr <= '0';
                ram_addr_i <= (others => '0');
                if rwn='1' then
                    state <= reading;
                else
                    state <= writing;
                end if;
            
            when reading =>
                rwn <= '1';
                if (mem_resp.rack='1' and mem_resp.rack_tag(5 downto 0) = g_tag(5 downto 0)) or (mreq = '0') then
                    if remain_is_0 = '1' then
                        state <= idle;
                        cmd_done <= '1';
                        mreq <= '0';
                    else
                        first_req <= not mreq;
                        last_req <= remain_is_1;
                        mreq <= '1';
                    end if;
                end if;
    
            when writing =>
                rwn <= '0';
                if (mem_resp.rack='1' and mem_resp.rack_tag(5 downto 0) = g_tag(5 downto 0)) or (mreq = '0') then
                    ram_addr_i <= ram_addr_i + 1;
                    if remain_is_1 = '1' and mreq = '1' then
                        state <= idle;
                        cmd_done <= '1';
                        mreq <= '0';
                    else
                        first_req <= not mreq;
                        last_req <= '0'; 
                        mreq <= '1';
                    end if;
                end if;

            when others =>
                null;
            end case;

            if ram_wnext = '1' then
                ram_addr_i <= ram_addr_i + 1;
            end if;

            if reset='1' then
                state  <= idle;
                mreq   <= '0';
                cmd_done <= '0';
                new_addr <= '0';
                first_req <= '0';
            end if;
        end if;
    end process;
    
    cmd_ready <= '1' when (state = idle) else '0';

    addr_do_load  <= new_addr when (state = init) else '0';
    addr_do_inc   <= '1' when (mem_resp.rack='1' and mem_resp.rack_tag(5 downto 0) = g_tag(5 downto 0)) else '0';
    
    i_addr: entity work.mem_addr_counter
    port map (
        clock       => clock,
        load_value  => mem_addr_r(25 downto 2),
        do_load     => addr_do_load,
        do_inc      => addr_do_inc,
        address     => mem_addr_i );

    rem_do_load   <= '1' when cmd_valid='1' and cmd_write='1' and cmd_addr(3 downto 1)="001" else '0';

    i_rem: entity work.mem_remain_counter
    port map (
        clock       => clock,
        load_value  => unsigned(cmd_wdata(9 downto 2)),
        do_load     => rem_do_load,
        do_dec      => rem_do_dec,
        remain      => open,
        remain_is_1 => remain_is_1,
        remain_is_0 => remain_is_0 );

    rdata_valid <= '1' when mem_resp.dack_tag(5 downto 0) = g_tag(5 downto 0) else '0';
    
    i_align: entity work.align_read_to_bram
    port map (
        clock       => clock,
        reset       => reset,
        rdata       => mem_resp.data,
        rdata_valid => rdata_valid,
        first_word  => mem_resp.dack_tag(6),
        last_word   => mem_resp.dack_tag(7),
        offset      => mem_addr_r(1 downto 0),
        wdata       => ram_wdata,
        wmask       => ram_we_i,
        wnext       => ram_wnext );
    
end architecture;
