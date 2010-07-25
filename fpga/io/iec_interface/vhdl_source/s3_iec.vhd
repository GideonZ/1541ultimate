library ieee;
    use ieee.std_logic_1164.all;
    use ieee.std_logic_unsigned.all;

entity s3_iec is
port (
    clk_66          : in  std_logic;
    switch          : in  std_logic_vector(5 downto 0);
    --
    
    leds            : out   std_logic_vector(7 downto 0);
    disp_seg1       : out   std_logic_vector(7 downto 0);
    disp_seg2       : out   std_logic_vector(7 downto 0);
    txd             : out   std_logic;
    rxd             : in    std_logic;

    --
    iec_atn         : inout std_logic;
    iec_data        : inout std_logic;
    iec_clock       : inout std_logic;
    iec_reset       : in    std_logic );
        
end s3_iec;

architecture structural of s3_iec is

    signal reset_in         : std_logic;

    signal atn_o, atn_i     : std_logic;
    signal clk_o, clk_i     : std_logic;
    signal data_o, data_i   : std_logic;
    signal error            : std_logic_vector(1 downto 0);

    signal send_byte        : std_logic;
    signal send_data        : std_logic_vector(7 downto 0);
    signal send_last        : std_logic;
    signal send_busy        : std_logic;

    signal recv_dav         : std_logic;
    signal recv_data        : std_logic_vector(7 downto 0);
    signal recv_last        : std_logic;
    signal recv_attention   : std_logic;
    
    signal do_tx            : std_logic;
    signal tx_done          : std_logic;
    signal txchar           : std_logic_vector(7 downto 0);
    
    signal rx_ack           : std_logic;
    signal rxchar           : std_logic_vector(7 downto 0);

    signal test_vector      : std_logic_vector(6 downto 0);
    signal test_vector_d    : std_logic_vector(6 downto 0);
    signal test_trigger     : std_logic;


    type t_state is (start, idle, tx2, tx3);
    signal state : t_state;
begin
    reset_in    <= iec_reset xor switch(1);

    leds(0) <= error(0) or error(1);
    leds(1) <= reset_in;
    leds(2) <= not iec_atn;
    leds(3) <= iec_data;
    leds(4) <= iec_clock;
    leds(5) <= not atn_o;
    leds(6) <= clk_o;
    leds(7) <= data_o;

    iec_atn   <= '0' when atn_o='0'  else 'Z'; -- open drain
    iec_clock <= '0' when clk_o='0'  else 'Z'; -- open drain
    iec_data  <= '0' when data_o='0' else 'Z'; -- open drain

    atn_i  <= iec_atn;
    clk_i  <= iec_clock;
    data_i <= iec_data;

    disp_seg2(0) <= recv_attention;
    disp_seg2(1) <= recv_last;
    disp_seg2(2) <= test_trigger;
    disp_seg2(7 downto 3) <= (others => '0');
    
    iec: entity work.iec_interface 
    generic map (
        tick_div        => 333 )
    port map (
        clock           => clk_66,
        reset           => reset_in,
    
        iec_atn_i       => atn_i,
        iec_atn_o       => atn_o,
        iec_clk_i       => clk_i,
        iec_clk_o       => clk_o,
        iec_data_i      => data_i,
        iec_data_o      => data_o,
        
        state_out       => disp_seg1,
        talker          => switch(0),
        error           => error,
        
        send_byte       => send_byte,
        send_data       => send_data,
        send_last       => send_last,
        send_attention  => '0',
        send_busy       => send_busy,
        
        recv_dav        => recv_dav,
        recv_data       => recv_data,
        recv_last       => recv_last,
        recv_attention  => recv_attention );

    my_tx: entity work.tx 
    generic map (579)
    port map (
        clk     => clk_66,
        reset   => reset_in,
        
        dotx    => do_tx,
        txchar  => txchar,
    
        txd     => txd,
        done    => tx_done );

    my_rx: entity work.rx 
    generic map (579)
    port map (
        clk     => clk_66,
        reset   => reset_in,
    
        rxd     => rxd,
        
        rxchar  => rxchar,
        rx_ack  => rx_ack );

    send_byte <= rx_ack;
    send_data <= rxchar;
    send_last <= '0';

    test_trigger <= '1' when (test_vector /= test_vector_d) else '0';

    process(clk_66)
        function to_hex(i : std_logic_vector(3 downto 0)) return std_logic_vector is
        begin
            case i is
            when X"0"|X"1"|X"2"|X"3"|X"4"|X"5"|X"6"|X"7"|X"8"|X"9" =>
                return X"3" & i;
            when X"A" => return X"41";
            when X"B" => return X"42";
            when X"C" => return X"43";
            when X"D" => return X"44";
            when X"E" => return X"45";
            when X"F" => return X"46";
            when others => return X"3F";
            end case;
        end function;
    begin
        if rising_edge(clk_66) then
            do_tx <= '0';

            test_vector <= reset_in & atn_i & clk_i & data_i & atn_o & clk_o & data_o;
            test_vector_d <= test_vector;
            
            case state is
            when start =>
                txchar <= X"2D";
                do_tx  <= '1';
                state  <= idle;                

            when idle =>
                if recv_dav='1' then
                    txchar <= to_hex(recv_data(7 downto 4));
                    do_tx  <= '1';
                    state  <= tx2;
                end if;
            
            when tx2 =>
                if tx_done = '1' and do_tx='0' then
                    txchar <= to_hex(recv_data(3 downto 0));
                    do_tx  <= '1';
                    state  <= tx3;
                end if;
                
            when tx3 =>
                if tx_done = '1' and do_tx='0' then
                    txchar <= "001000" & recv_last & recv_attention; -- !=atn @=end #=end atn
                    do_tx <= '1';
                    state <= idle;
                end if;

            when others =>
                null;

            end case;
            
            if reset_in='1' then
                txchar <= X"00";
                do_tx  <= '0';
                state  <= start;
            end if;
        end if;
    end process;


end structural;

