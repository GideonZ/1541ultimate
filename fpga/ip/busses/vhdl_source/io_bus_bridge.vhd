library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;

entity io_bus_bridge is
generic (
    g_addr_width : natural := 8 );
port (
    clock_a   : in  std_logic;
    reset_a   : in  std_logic;
    req_a     : in  t_io_req;
    resp_a    : out t_io_resp;
    
    clock_b   : in  std_logic;
    reset_b   : in  std_logic;
    req_b     : out t_io_req;
    resp_b    : in  t_io_resp );
end entity;

architecture rtl of io_bus_bridge is
    -- CLOCK_A
    signal to_wr_en     : std_logic;
    signal to_wr_din    : std_logic_vector(g_addr_width+8 downto 0);
    signal to_wr_full   : std_logic;
    
    signal from_rd_en   : std_logic;
    signal from_rd_dout : std_logic_vector(7 downto 0);
    signal from_rd_valid: std_logic;

    type t_state_a is (idle, pending_w, pending_r, wait_rdata);
    signal state_a      : t_state_a;

    -- CLOCK B
    signal from_wr_en   : std_logic;
    signal to_rd_en     : std_logic;
    signal to_rd_dout   : std_logic_vector(g_addr_width+8 downto 0);
    signal to_rd_valid  : std_logic;

    type t_state_b is (idle, do_write, do_read);
    signal state_b      : t_state_b;

begin
    -- generate ack
    process(clock_a)
    begin
        if rising_edge(clock_a) then
            resp_a.ack <= '0';
            resp_a.data <= X"00";
            
            case state_a is
            when idle =>
                if req_a.write='1' then
                    if to_wr_full = '1' then
                        state_a <= pending_w;
                    else
                        resp_a.ack <= '1';
                    end if;
                elsif req_a.read='1' then
                    if to_wr_full = '1' then
                        state_a <= pending_r;
                    else
                        state_a <= wait_rdata;
                    end if;
                end if;
            
            when pending_w =>
                if to_wr_full = '0' then
                    state_a <= idle;
                    resp_a.ack <= '1';
                end if;
            
            when pending_r =>
                if to_wr_full = '0' then
                    state_a <= wait_rdata;
                end if;

            when wait_rdata =>
                if from_rd_valid = '1' then
                    resp_a.ack <= '1';
                    resp_a.data <= from_rd_dout;
                    state_a <= idle;
                end if;                

            when others =>
                null;
            end case;

            if reset_a = '1' then
                state_a <= idle;
            end if;
        end if;
    end process;
    
    process (state_a, to_wr_full, from_rd_valid, req_a)
    begin
        to_wr_en <= '0';
        to_wr_din <= std_logic_vector(req_a.address(g_addr_width-1 downto 0)) & '0' & req_a.data;
        from_rd_en <= '0';

        case state_a is
        when idle =>
            if to_wr_full = '0' then
                to_wr_en <= req_a.write or req_a.read;
                to_wr_din(8) <= req_a.write;
            end if;
        when pending_w =>
            if to_wr_full = '0' then
                to_wr_en <= '1';
                to_wr_din(8) <= '1';
            end if;
        when pending_r =>
            if to_wr_full = '0' then
                to_wr_en <= '1';
                to_wr_din(8) <= '0';
            end if;
        when wait_rdata =>
            if from_rd_valid='1' then
                from_rd_en <= '1';
            end if;
        end case;           
    end process;

    i_heen: entity work.async_fifo_ft
    generic map (
        g_data_width    => g_addr_width + 9,
        g_depth_bits    => 3 )
    port map (
        wr_clock        => clock_a,
        wr_reset        => reset_a,
        wr_en           => to_wr_en,
        wr_din          => to_wr_din,
        wr_full         => to_wr_full,

        rd_clock        => clock_b,
        rd_reset        => reset_b,
        rd_next         => to_rd_en,
        rd_dout         => to_rd_dout,
        rd_valid        => to_rd_valid );
    
    i_weer: entity work.async_fifo_ft
    generic map (
        g_data_width    => 8,
        g_depth_bits    => 3 )
    port map (
        wr_clock        => clock_b,
        wr_reset        => reset_b,
        wr_en           => from_wr_en,
        wr_din          => resp_b.data,

        rd_clock        => clock_a,
        rd_reset        => reset_a,
        rd_next         => from_rd_en,
        rd_dout         => from_rd_dout,
        rd_valid        => from_rd_valid );

    process(clock_b)
    begin
        if rising_edge(clock_b) then
            req_b.read <= '0';
            req_b.write <= '0';

            case state_b is
            when idle =>
                if to_rd_valid='1' then
                    req_b.address(g_addr_width-1 downto 0) <= unsigned(to_rd_dout(g_addr_width+8 downto 9));
                    req_b.data <= to_rd_dout(7 downto 0);
                    if to_rd_dout(8)='1' then
                        req_b.write <= '1';
                        state_b <= do_write;
                    else
                        req_b.read <= '1';
                        state_b <= do_read;
                    end if;
                end if;
                
            when do_write =>
                if resp_b.ack = '1' then
                    state_b <= idle;
                end if;
            
            when do_read =>
                if resp_b.ack = '1' then
                    state_b <= idle;
                end if;
            end case;
                                
            if reset_b='1' then
                state_b <= idle;
                req_b <= c_io_req_init;
            end if;
        end if;
    end process;

    process(state_b, to_rd_valid, resp_b)
    begin
        to_rd_en <= '0';
        from_wr_en <= '0';
        
        case state_b is
        when idle =>
            if to_rd_valid = '1' then
                to_rd_en <= '1';
            end if;
        when do_read =>
            if resp_b.ack = '1' then
                from_wr_en <= '1';
            end if;
        when others =>
            null;
        end case;
    end process;
end rtl;
