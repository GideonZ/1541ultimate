library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_register_if is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
                    
    -- register interface bus
    address         : in  std_logic_vector(7 downto 0);
    request         : in  std_logic;
    read_writen     : in  std_logic;
    wdata           : in  std_logic_vector(7 downto 0);
    rdata           : out std_logic_vector(7 downto 0);
    rack            : out std_logic;
    dack            : out std_logic;

    -- "illegal" register bit output
    sof_enable      : out std_logic;
    reset_done      : in  std_logic;
    speed           : in  std_logic_vector(1 downto 0);
    reset_reg       : out std_logic;
    
    -- fifo interface
    rx_pkt_get      : out std_logic;
    rx_pkt_data     : in  std_logic_vector(11 downto 0);
    rx_pkt_empty    : in  std_logic;
    rx_data_get     : out std_logic;
    rx_data_data    : in  std_logic_vector(7 downto 0);
    rx_data_empty   : in  std_logic;

    tx_pkt_put      : out std_logic;
    tx_pkt_data     : out std_logic_vector(11 downto 0);
    tx_pkt_full     : in  std_logic;
    tx_data_put     : out std_logic;
    tx_data_data    : out std_logic_vector(7 downto 0);
    tx_data_full    : in  std_logic );

end ulpi_register_if;

architecture gideon of ulpi_register_if is
    type t_state is (idle, reg_read, reg_write, put_token);
    signal state        : t_state;
    signal reg_wdata    : std_logic_vector(7 downto 0) := X"55";
    signal device       : std_logic_vector(6 downto 0) := (others => '0');
    signal endpoint     : std_logic_vector(3 downto 0) := (others => '0');
begin
    process(clock)
    begin
        if rising_edge(clock) then
            rack  <= '0';
            dack  <= '0';
            rdata <= (others => '0');
            
            tx_data_put <= '0';
            tx_pkt_put  <= '0';
            rx_data_get <= '0';
            rx_pkt_get  <= '0';
            
            case state is
            when idle =>
                if request='1' then
                    rack <= '1';
                    if read_writen='0' then -- write
                        if address(7 downto 6) = "00" then -- register access
                            tx_data_data <= "10" & address(5 downto 0);
                            tx_data_put  <= '1';
                            reg_wdata <= wdata;
                            state <= reg_write;
                        else
                            case address(3 downto 0) is
                            when X"0" =>
                                tx_data_data <= wdata;
                                tx_data_put  <= '1';

                            when X"2" =>
                                tx_pkt_data(7 downto 0) <= wdata;
                                tx_pkt_put   <= '1';
                            
                            when X"3" =>
                                tx_pkt_data(10 downto 8) <= wdata(2 downto 0);

                            when X"4" =>
                                rx_data_get <= '1';
                                
                            when X"6" =>
                                rx_pkt_get <= '1';
                                
                            when X"8" =>
                                device <= wdata(6 downto 0);
                                
                            when X"9" =>
                                endpoint <= wdata(3 downto 0);

                            when X"A" =>
                                tx_pkt_data <= "10000000" & wdata(3 downto 0);
                                tx_pkt_put  <= '1';
                                state <= put_token;
                                
                            when X"B" =>
                                sof_enable <= wdata(0);
                                
                            when X"C" =>
                                reset_reg <= wdata(0);
                                
                            when others =>
                                null;
                            end case;
                        end if;
                    else -- read
                        if address(7 downto 6) = "00" then -- register access
                            tx_data_data <= "11" & address(5 downto 0);
                            tx_data_put  <= '1';
                            tx_pkt_data  <= X"001";
                            tx_pkt_put   <= '1';
                            --state <= reg_read;

                            rdata <= X"BE";
                            dack  <= '1';
                        else
                            dack <= '1';
                            case address(3 downto 0) is
                            when X"F" =>
                                rdata(0) <= not rx_data_empty;
                                rdata(1) <= not rx_pkt_empty;
                                rdata(2) <= tx_data_full;
                                rdata(3) <= tx_pkt_full;
--                                rdata(7 downto 4) <= X"E";
                                
                            when X"4" =>
                                rdata <= rx_data_data;
                            
                            when X"6" =>
                                rdata <= rx_pkt_data(7 downto 0);
                            
                            when X"7" =>
                                rdata <= "0000" & rx_pkt_data(11 downto 8);
                                
                            when X"C" =>
                                rdata <= reset_done & "00000" & speed;
                                
                            when others =>
                                null;
                            end case;
                        end if; -- register
                    end if; -- read
                end if; -- access
            
            when put_token =>
                if tx_pkt_full = '0' then
                    tx_pkt_data <= '0' & endpoint & device;
                    tx_pkt_put  <= '1';
                    state <= idle;
                end if;
                
            when reg_write =>
                if tx_data_full = '0' and tx_pkt_full = '0' then
                    tx_data_data <= reg_wdata;
                    tx_data_put  <= '1';
                    tx_pkt_data  <= X"002";
                    tx_pkt_put   <= '1';
                -- dack <= '1';
                    state <= idle;
                end if;
                
            when reg_read =>
                null;
                
            end case;

            if reset='1' then
                state <= idle;
            end if;
        end if;
    end process;
    
end gideon;
