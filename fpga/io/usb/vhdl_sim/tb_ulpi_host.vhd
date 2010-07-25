library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.usb_pkg.all;
use work.tl_vector_pkg.all;
use work.tl_string_util_pkg.all;
use work.tl_flat_memory_model_pkg.all;

entity tb_ulpi_host is
end ;

architecture tb of tb_ulpi_host is
    signal clock           : std_logic := '0';
    signal reset           : std_logic;
    signal descr_addr      : std_logic_vector(8 downto 0);
    signal descr_rdata     : std_logic_vector(31 downto 0);
    signal descr_wdata     : std_logic_vector(31 downto 0);
    signal descr_en        : std_logic;
    signal descr_we        : std_logic;
    signal buf_addr        : std_logic_vector(11 downto 0);
    signal buf_rdata       : std_logic_vector(7 downto 0);
    signal buf_wdata       : std_logic_vector(7 downto 0);
    signal buf_en          : std_logic;
    signal buf_we          : std_logic;
    signal tx_busy         : std_logic;
    signal tx_ack          : std_logic;
    signal send_token      : std_logic;
    signal send_handsh     : std_logic;
    signal tx_pid          : std_logic_vector(3 downto 0);
    signal tx_token        : std_logic_vector(10 downto 0);
    signal send_data       : std_logic;
    signal no_data         : std_logic;
    signal user_data       : std_logic_vector(7 downto 0);
    signal user_last       : std_logic;
    signal user_valid      : std_logic;
    signal user_next       : std_logic;
    signal rx_pid          : std_logic_vector(3 downto 0) := X"0";
    signal rx_token        : std_logic_vector(10 downto 0) := (others => '0');
    signal valid_token     : std_logic := '0';
    signal valid_handsh    : std_logic := '0';
    signal valid_packet    : std_logic := '0';
    signal data_valid      : std_logic := '0';
    signal data_start      : std_logic := '0';
    signal data_out        : std_logic_vector(7 downto 0) := X"12";
    signal rx_error        : std_logic := '0';
begin

    i_mut: entity work.ulpi_host
    port map (
        clock       => clock,
        reset       => reset,
    
        -- Descriptor RAM interface
        descr_addr  => descr_addr,
        descr_rdata => descr_rdata,
        descr_wdata => descr_wdata,
        descr_en    => descr_en,
        descr_we    => descr_we,
    
        -- Buffer RAM interface
        buf_addr    => buf_addr,
        buf_rdata   => buf_rdata,
        buf_wdata   => buf_wdata,
        buf_en      => buf_en,
        buf_we      => buf_we,
    
        -- Transmit Path Interface
        tx_busy     => tx_busy,
        tx_ack      => tx_ack,
        
        -- Interface to send tokens and handshakes
        send_token  => send_token,
        send_handsh => send_handsh,
        tx_pid      => tx_pid,
        tx_token    => tx_token,
        
        -- Interface to send data packets
        send_data   => send_data,
        no_data     => no_data,
        user_data   => user_data,
        user_last   => user_last,
        user_valid  => user_valid,
        user_next   => user_next,
                
        do_reset    => open,
        power_en    => open,
        reset_done  => '1',
        speed       => "10",
        
        reset_pkt   => '0',
        reset_data  => X"00",
        reset_last  => '0',
        reset_valid => '0',

        -- Receive Path Interface
        rx_pid          => rx_pid,
        rx_token        => rx_token,
        valid_token     => valid_token,
        valid_handsh    => valid_handsh,
    
        valid_packet    => valid_packet,
        data_valid      => data_valid,
        data_start      => data_start,
        data_out        => data_out,
    
        rx_error        => rx_error );

    clock <= not clock after 10 ns;
    reset <= '1', '0' after 100 ns;

    i_descr_ram: entity work.bram_model_32sp
    generic map("descriptors", 9) 
    port map (
		CLK  => clock,
		SSR  => reset,
		EN   => descr_en,
		WE   => descr_we,
        ADDR => descr_addr,
		DI   => descr_wdata,
		DO   => descr_rdata );

    i_buf_ram: entity work.bram_model_8sp
    generic map("buffer", 12) 
    port map (
		CLK  => clock,
		SSR  => reset,
		EN   => buf_en,
		WE   => buf_we,
        ADDR => buf_addr,
		DI   => buf_wdata,
		DO   => buf_rdata );

    b_tx_bfm: block
        signal tx_delay : integer range 0 to 31 := 0;
    begin
        process(clock)
        begin
            if rising_edge(clock) then
                tx_ack <= '0';
                user_next <= '0';
                if tx_delay = 28 then -- transmit packet
                    user_next <= '1';
                    if user_last='1' and user_valid='1' then
                        tx_delay <= 0; -- done;
                        user_next <= '0';
                    end if;
                elsif tx_delay = 0 then
                    if send_token='1' then
                        tx_delay <= 6;
                        tx_ack <= '1';
                    elsif send_handsh='1' then
                        tx_delay <= 4;
                        tx_ack <= '1';
                    elsif send_data='1' then
                        tx_ack <= '1';
                        if no_data='1' then
                            tx_delay <= 5;
                        else                      
                            tx_delay <= 31;
                        end if;
                    end if;
                else
                    tx_delay <= tx_delay - 1;
                end if;
            end if;
        end process;
        tx_busy <= '0' when tx_delay = 0 else '1';
    end block;

    p_test: process
        variable desc   : h_mem_object;
        variable buf    : h_mem_object;

        procedure packet(pkt : t_std_logic_8_vector) is
        begin
            for i in pkt'range loop
                wait until clock='1';
                data_out <= pkt(i);
                data_valid <= '1';
                if i = pkt'left then
                    data_start <= '1';
                else
                    data_start <= '0';
                end if;
            end loop;
            wait until clock='1';
            data_valid <= '0';
            data_start <= '0';
            wait until clock='1';
            wait until clock='1';
            wait until clock='1';
        end procedure packet;

    begin
        bind_mem_model("descriptors", desc);
        bind_mem_model("buffer", buf);
        wait until reset='0';
            
        write_memory_32(desc, X"0000_0100", t_transaction_to_data((
            transaction_type => control,
            state            => busy, -- activate
            pipe_pointer     => "00000",
            transfer_length  => to_unsigned(8, 11),
            buffer_address   => to_unsigned(100, 12) )));

        write_memory_32(desc, X"0000_0104", t_transaction_to_data((
            transaction_type => bulk,
            state            => busy, -- activate
            pipe_pointer     => "00001",
            transfer_length  => to_unsigned(60, 11),
            buffer_address   => to_unsigned(256, 12) )));

        write_memory_32(desc, X"0000_0000", t_pipe_to_data((
            state               => initialized,
            direction           => dir_out,
            device_address      => (others => '0'),
            device_endpoint     => (others => '0'),
            max_transfer        => to_unsigned(64, 11),
            data_toggle         => '0' ) ));

        write_memory_32(desc, X"0000_0004", t_pipe_to_data((
            state               => initialized,
            direction           => dir_out,
            device_address      => (others => '0'),
            device_endpoint     => (others => '0'),
            max_transfer        => to_unsigned(8, 11),
            data_toggle         => '0' ) ));

        for i in 0 to 7 loop
            write_memory_8(buf, std_logic_vector(to_unsigned(100+i,32)),
                                std_logic_vector(to_unsigned(33+i,8)));
        end loop;

        wait until tx_busy='0'; -- first sof token
        wait until tx_busy='0'; -- setup token
        wait until tx_busy='0'; -- setup data
        wait until tx_busy='0'; -- retried setup token
        wait until tx_busy='0'; -- retried setup data
        wait until clock='1';    
        wait until clock='1';    
        wait until clock='1';    
        wait until clock='1';    
        wait until clock='1';    
        valid_handsh <= '1';
        rx_pid       <= c_pid_ack;
        wait until clock='1';    
        valid_handsh <= '0';


        -- control out
        for i in 0 to 7 loop
            wait until tx_busy='0'; -- out token
            wait until tx_busy='0'; -- out data
            wait until clock='1';    
            wait until clock='1';    
            wait until clock='1';    
            valid_handsh <= '1';
            rx_pid       <= c_pid_ack;
            wait until clock='1';    
            valid_handsh <= '0';
        end loop;

        wait until tx_busy='0'; -- in token
        assert tx_pid = c_pid_in
            report "Expected in token! (pid = " & hstr(tx_pid) & ")"
            severity error;
        wait until clock='1';    
        wait until clock='1';    
        wait until clock='1';    
        valid_packet <= '1';
        wait until clock='1';    
        valid_packet <= '0';
        
--        -- control in..
--        wait until send_token='1';
--        wait until tx_busy='0'; -- in token done
--        wait until clock='1';    
--        wait until clock='1';    
--        wait until clock='1';    
--        packet((X"01", X"02", X"03", X"04", X"05", X"06"));
--        valid_packet <= '1';
--        wait until clock='1';    
--        valid_packet <= '0';
        wait;           
    end process;
        
end tb;
