library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_interface is
port (
    ulpi_clock  : in    std_logic;
    ulpi_reset  : in    std_logic;
    
    ULPI_DATA   : inout std_logic_vector(7 downto 0);
    ULPI_DIR    : in    std_logic;
    ULPI_NXT    : in    std_logic;
    ULPI_STP    : out   std_logic;

    sys_clock   : in    std_logic;
    sys_reset   : in    std_logic;
    
    -- register interface bus
    address     : in    std_logic_vector(7 downto 0);
    request     : in    std_logic;
    read_writen : in    std_logic;
    wdata       : in    std_logic_vector(7 downto 0);
    rdata       : out   std_logic_vector(7 downto 0);
    rack        : out   std_logic;
    dack        : out   std_logic );

end ulpi_interface;

architecture structural of ulpi_interface is

    signal status           : std_logic_vector(7 downto 0);
    signal tx_next          : std_logic;
    signal tx_data          : std_logic_vector(7 downto 0);
    signal tx_last          : std_logic;
    signal tx_valid         : std_logic;
    signal tx_start         : std_logic;

    signal host_data        : std_logic_vector(7 downto 0);
    signal host_last        : std_logic;
    signal host_valid       : std_logic;
    signal host_start       : std_logic;
    signal host_next        : std_logic;
    
    signal ctrl_data        : std_logic_vector(7 downto 0);
    signal ctrl_last        : std_logic;
    signal ctrl_valid       : std_logic;
    signal ctrl_start       : std_logic;

    signal ctrl_req         : std_logic;
    signal ctrl_ack         : std_logic := '0';

    signal tx_get           : std_logic;
    signal tx_empty         : std_logic;
    signal rx_data          : std_logic_vector(7 downto 0);
    signal rx_command       : std_logic;
    signal rx_register      : std_logic;
    signal rx_last          : std_logic;
    signal rx_valid         : std_logic;
    signal rx_store         : std_logic;
    signal rx_fifo_put      : std_logic;
    signal rx_pkt_fifodata  : std_logic_vector(11 downto 0);
    signal rx_pkt_full      : std_logic;
    signal rx_pkt_dropped   : std_logic;
    signal rx_pkt_type      : std_logic_vector(1 downto 0);
    signal rx_pkt_length    : std_logic_vector(9 downto 0);
    signal rx_pkt_put       : std_logic;
    signal tx_pkt_empty     : std_logic;
    signal tx_pkt_length    : std_logic_vector(10 downto 0);
    signal tx_pkt_token     : std_logic;
    signal tx_pkt_get       : std_logic;

    signal tx_user_next     : std_logic;
    signal tx_user_data     : std_logic_vector(7 downto 0);
    signal tx_user_valid    : std_logic;
    
    signal sys_rx_pkt_get   : std_logic;
    signal sys_rx_pkt_data  : std_logic_vector(11 downto 0);
    signal sys_rx_pkt_empty : std_logic;
    signal sys_rx_data_get  : std_logic;
    signal sys_rx_data_data : std_logic_vector(7 downto 0);
    signal sys_rx_data_empty: std_logic;
    
    signal tx_data_fifo     : std_logic_vector(7 downto 0);
    signal sys_tx_pkt_put   : std_logic;
    signal sys_tx_pkt_data  : std_logic_vector(11 downto 0);
    signal sys_tx_pkt_full  : std_logic;
    signal sys_tx_data_put  : std_logic;
    signal sys_tx_data_data : std_logic_vector(7 downto 0);
    signal sys_tx_data_full : std_logic;

    signal sof_enable       : std_logic;
    signal reset_done       : std_logic;
    signal reset_reg        : std_logic;
    signal reset_reg_c      : std_logic;
    signal reset_reg_d      : std_logic;
    signal do_reset         : std_logic;
    signal speed            : std_logic_vector(1 downto 0);
    signal host_busy        : std_logic;
begin
    i_bus: entity work.ulpi_bus
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        ULPI_DATA   => ULPI_DATA,
        ULPI_DIR    => ULPI_DIR,
        ULPI_NXT    => ULPI_NXT,
        ULPI_STP    => ULPI_STP,
        
        status      => status,
        
        -- fifo interface
        tx_data     => tx_data,
        tx_last     => tx_last,
        tx_valid    => tx_valid,
        tx_start    => tx_start,
        tx_next     => tx_next,
                                  
        rx_data     => rx_data,
        rx_register => rx_register,
        rx_last     => rx_last,
        rx_store    => rx_store,
        rx_valid    => rx_valid );

    i_rx: entity work.ulpi_rx
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        rx_data     => rx_data,
        rx_register => rx_register,
        rx_last     => rx_last,
        rx_valid    => rx_valid,
        rx_store    => rx_store,
        rx_fifo_put => rx_fifo_put,
    
        pkt_full    => rx_pkt_full,
        pkt_type    => rx_pkt_fifodata(11 downto 10),
        pkt_length  => rx_pkt_fifodata(9 downto 0),
        pkt_put     => rx_pkt_put,
        
        error       => rx_pkt_dropped );

    i_reset: entity work.ulpi_bus_ctrl
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        do_reset    => do_reset,
    
        tx_request  => ctrl_req, -- sets selector
        tx_ack      => ctrl_ack, -- we have the ulpi bus
        
        reset_done  => reset_done,
        speed       => speed,
        
        -- status
        status      => status,
    
        tx_data     => ctrl_data,
        tx_last     => ctrl_last,
        tx_start    => ctrl_start,
        tx_valid    => ctrl_valid,
        tx_next     => tx_next );

    -- arbitration (SIMPLE!!)
    process(ulpi_clock)
    begin
        if rising_edge(ulpi_clock) then
            reset_reg_c <= reset_reg;
            reset_reg_d <= reset_reg_c;
            do_reset    <= reset_reg_c and not reset_reg_d; -- rising edge

            if ctrl_req='1' and host_busy='0' then
                ctrl_ack <= '1';
            elsif ctrl_req='0' then
                ctrl_ack <= '0';
            end if;
            
            if ulpi_reset='1' then
                ctrl_ack <= '0';
            end if;
        end if;
    end process;

    -- mux
    tx_data   <= ctrl_data  when ctrl_ack='1' else host_data;
    tx_last   <= ctrl_last  when ctrl_ack='1' else host_last;
    tx_valid  <= ctrl_valid when ctrl_ack='1' else host_valid;
    tx_start  <= ctrl_start when ctrl_ack='1' else host_start;
    host_next <= '0' when ctrl_ack='1' else tx_next;
    
    i_tx: entity work.ulpi_host
    port map (
        clock       => ulpi_clock,
        reset       => ulpi_reset,
        
        sof_enable  => sof_enable,

        host_busy   => host_busy,
        tx_last     => host_last,
        tx_start    => host_start,
        tx_data     => host_data,
        tx_valid    => host_valid,
        tx_next     => tx_next,
        
        tx_user_next    => tx_user_next,
        tx_user_data    => tx_user_data,
        tx_user_valid   => tx_user_valid,
        
        pkt_empty   => tx_pkt_empty,
        pkt_token   => tx_pkt_token,
        pkt_length  => tx_pkt_length,
        pkt_get     => tx_pkt_get );

-- crossing the clock boundary --
    i_rx_pkt_fifo: entity work.async_fifo
    generic map (
        g_data_width => 12,
        g_depth_bits => 4,
        g_count_bits => 4,
        g_threshold  => 10,
        g_storage    => "distributed" )
    port map (
        -- write port signals (synchronized to write clock)
        wr_clock       => ulpi_clock,
        wr_reset       => ulpi_reset,
        wr_en          => rx_pkt_put,
        wr_din         => rx_pkt_fifodata,
        wr_flush       => '0',
        wr_count       => open,
        wr_full        => rx_pkt_full,
        wr_almost_full => open,
        wr_error       => open,
        wr_inhibit     => open,

        -- read port signals (synchronized to read clock)
        rd_clock        => sys_clock,
        rd_reset        => sys_reset,
        rd_en           => sys_rx_pkt_get,
        rd_dout         => sys_rx_pkt_data,
        rd_count        => open,
        rd_empty        => sys_rx_pkt_empty,
        rd_almost_empty => open,
        rd_error        => open );

    i_rx_data_fifo: entity work.async_fifo
    generic map (
        g_data_width => 8,
        g_depth_bits => 11,
        g_count_bits => 4,
        g_threshold  => 10,
        g_storage    => "blockram" )
    port map (
        -- write port signals (synchronized to write clock)
        wr_clock       => ulpi_clock,
        wr_reset       => ulpi_reset,
        wr_en          => rx_fifo_put,
        wr_din         => rx_data,
        wr_flush       => '0',
        wr_count       => open,
        wr_full        => open, -- ## TODO!!
        wr_almost_full => open,
        wr_error       => open,
        wr_inhibit     => open,

        -- read port signals (synchronized to read clock)
        rd_clock        => sys_clock,
        rd_reset        => sys_reset,
        rd_en           => sys_rx_data_get,
        rd_dout         => sys_rx_data_data,
        rd_count        => open,
        rd_empty        => sys_rx_data_empty,
        rd_almost_empty => open,
        rd_error        => open );

    i_tx_pkt_fifo: entity work.async_fifo
    generic map (
        g_data_width => 12,
        g_depth_bits => 4,
        g_count_bits => 4,
        g_threshold  => 10,
        g_storage    => "distributed" )
    port map (
        -- write port signals (synchronized to write clock)
        wr_clock       => sys_clock,
        wr_reset       => sys_reset,
        wr_en          => sys_tx_pkt_put,
        wr_din         => sys_tx_pkt_data,
        wr_flush       => '0',
        wr_count       => open,
        wr_full        => sys_tx_pkt_full,
        wr_almost_full => open,
        wr_error       => open,
        wr_inhibit     => open,

        -- read port signals (synchronized to read clock)
        rd_clock        => ulpi_clock,
        rd_reset        => ulpi_reset,
        rd_en           => tx_pkt_get,
        rd_dout(11 )         => tx_pkt_token,
        rd_dout(10 downto 0) => tx_pkt_length,
        rd_count        => open,
        rd_empty        => tx_pkt_empty,
        rd_almost_empty => open,
        rd_error        => open );

    i_tx_data_fifo: entity work.async_fifo
    generic map (
        g_data_width => 8,
        g_depth_bits => 11,
        g_count_bits => 4,
        g_threshold  => 10,
        g_storage    => "blockram" )
    port map (
        -- write port signals (synchronized to write clock)
        wr_clock       => sys_clock,
        wr_reset       => sys_reset,
        wr_en          => sys_tx_data_put,
        wr_din         => sys_tx_data_data,
        wr_flush       => '0',
        wr_count       => open,
        wr_full        => sys_tx_data_full,
        wr_almost_full => open,
        wr_error       => open,
        wr_inhibit     => open,

        -- read port signals (synchronized to read clock)
        rd_clock        => ulpi_clock,
        rd_reset        => ulpi_reset,
        rd_en           => tx_get,
        rd_dout         => tx_data_fifo,
        rd_count        => open,
        rd_empty        => tx_empty,
        rd_almost_empty => open,
        rd_error        => open );

    i_ft: entity work.fall_through_add_on
    generic map (
        g_data_width => 8 ) 
    port map (
        clock      => ulpi_clock,
        reset      => ulpi_reset,
        -- fifo side
        rd_dout    => tx_data_fifo,
        rd_empty   => tx_empty,
        rd_en      => tx_get,
        -- consumer side
        data_valid => tx_user_valid,
        data_out   => tx_user_data,
        data_next  => tx_user_next );

-- we're now in the system clock domain --

    i_reg_if: entity work.ulpi_register_if
    port map (
        clock           => sys_clock,
        reset           => sys_reset,
                        
        -- register interface bus
        address         => address,
        request         => request,
        read_writen     => read_writen,
        wdata           => wdata,
        rdata           => rdata,
        rack            => rack,
        dack            => dack,
    
        sof_enable      => sof_enable,
        reset_reg       => reset_reg,
        speed           => speed, -- unsynced!
        reset_done      => reset_done, -- unsynced
        
        -- fifo interface
        rx_pkt_get      => sys_rx_pkt_get,
        rx_pkt_data     => sys_rx_pkt_data,
        rx_pkt_empty    => sys_rx_pkt_empty,
        rx_data_get     => sys_rx_data_get,
        rx_data_data    => sys_rx_data_data,
        rx_data_empty   => sys_rx_data_empty,

        tx_pkt_put      => sys_tx_pkt_put,
        tx_pkt_data     => sys_tx_pkt_data,
        tx_pkt_full     => sys_tx_pkt_full,
        tx_data_put     => sys_tx_data_put,
        tx_data_data    => sys_tx_data_data,
        tx_data_full    => sys_tx_data_full );
    
end structural;
    