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
    ram_we      : out std_logic;
    ram_wdata   : out std_logic_vector(31 downto 0);
    ram_rdata   : in  std_logic_vector(31 downto 0);
    
    -- memory interface
    mem_req     : out t_mem_req_32;
    mem_resp    : in  t_mem_resp_32 );

end entity;

architecture gideon of usb_memory_ctrl is
    type t_state is (idle, reading, writing, init);
    signal state            : t_state;
    signal mem_addr_r       : unsigned(25 downto 2) := (others => '0');
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
    signal buffer_idx       : std_logic_vector(10 downto 9) := "00";
begin
    mem_req.tag         <= g_tag;
    mem_req.request     <= mreq;
    mem_req.address     <= mem_addr_i & "00";
    mem_req.read_writen <= rwn;
    mem_req.data        <= ram_rdata;
    mem_req.byte_en     <= "1111";
    
    -- pop from fifo when we process the access
    cmd_ack <= '1' when (state = idle) and (cmd_valid='1') else '0';

    process(buffer_idx, state, mreq, mem_resp, ram_addr_i)
    begin
        ram_addr  <= buffer_idx & std_logic_vector(ram_addr_i);
        ram_wdata <= mem_resp.data;
        ram_we <= '0';
        ram_en <= '0';

        -- for writing to memory, we enable the BRAM only when we are going to set
        -- the request, such that the data and the request comes at the same time
        case state is
        when writing =>
            if (mem_resp.rack='1' and mem_resp.rack_tag = g_tag) or (mreq = '0') then
                ram_en <= '1';
            end if;
        when others =>
            null;
        end case;
        
        -- for reading from memory, it doesn't matter in which state we are:
        if mem_resp.dack_tag = g_tag then
            ram_we <= '1';
            ram_en <= '1';
        end if;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            rem_do_dec <= '0';
            case state is
            when idle =>
                rwn <= '1';
                if cmd_valid='1' then
                    if cmd_write='1' then
                        cmd_done <= '0';
                        case cmd_addr is
                        when X"0" =>
                            mem_addr_r(15 downto 2) <= unsigned(cmd_wdata(15 downto 2));
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
                if (mem_resp.rack='1' and mem_resp.rack_tag = g_tag) or (mreq = '0') then
                    if remain_is_0 = '1' then
                        state <= idle;
                        cmd_done <= '1';
                        mreq <= '0';
                    else
                        mreq <= '1';
                        rem_do_dec <= '1';
                    end if;
                end if;
    
            when writing =>
                rwn <= '0';
                if (mem_resp.rack='1' and mem_resp.rack_tag = g_tag) or (mreq = '0') then
                    ram_addr_i <= ram_addr_i + 1;
                    if remain_is_0 = '1' then
                        state <= idle;
                        cmd_done <= '1';
                        mreq <= '0';
                    else
                        mreq <= '1';
                        rem_do_dec <= '1';
                    end if;
                end if;

            when others =>
                null;
            end case;

            if mem_resp.dack_tag=g_tag then
                ram_addr_i <= ram_addr_i + 1;
            end if;

            if reset='1' then
                state  <= idle;
                mreq   <= '0';
                cmd_done <= '0';
                new_addr <= '0';
            end if;
        end if;
    end process;
    
    cmd_ready <= '1' when (state = idle) else '0';

    addr_do_load  <= new_addr when (state = init) else '0';
    addr_do_inc   <= '1' when (mem_resp.rack='1' and mem_resp.rack_tag = g_tag) else '0';
    
    i_addr: entity work.mem_addr_counter
    port map (
        clock       => clock,
        load_value  => mem_addr_r,
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
        remain_is_0 => remain_is_0 );

end architecture;
