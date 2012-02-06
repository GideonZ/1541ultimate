-------------------------------------------------------------------------------
-- Title      : External Memory controller for SDRAM
-------------------------------------------------------------------------------
-- Description: This module converts a 16 bit burst into 32 bits for reads,
--              and vice versa for writes
-------------------------------------------------------------------------------
 
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.mem_bus_pkg.all;

entity mem_16to32 is
port (
    clock       : in  std_logic := '0';
    reset       : in  std_logic := '0';

    req_16      : out t_mem_burst_16_req;
    resp_16     : in  t_mem_burst_16_resp;
    
    req_32      : in  t_mem_burst_32_req;
    resp_32     : out t_mem_burst_32_resp );

end entity;

architecture gideon of mem_16to32 is
    signal rtoggle      : std_logic;
--    signal rbuf         : std_logic_vector(15 downto 0);

    signal wtoggle      : std_logic;
    signal pass_wdata   : std_logic;
    signal get_wdata    : std_logic;
    signal wdata_av     : std_logic;
    signal fifo_wdata   : std_logic_vector(31 downto 0);
    signal fifo_byte_en : std_logic_vector(3 downto 0);
begin
    req_16.request     <= req_32.request;
    req_16.request_tag <= req_32.request_tag;
    req_16.address     <= req_32.address;
    req_16.read_writen <= req_32.read_writen;
    
    resp_32.ready      <= resp_16.ready;

    process(clock)
    begin
        if rising_edge(clock) then
            -- handle reads
            resp_32.rdata_av <= '0';
            if resp_16.rdata_av='1' then
                rtoggle <= not rtoggle;
--                rbuf <= resp_16.data;
                if rtoggle='1' then
                    resp_32.data(31 downto 16) <= resp_16.data;
                    resp_32.data_tag <= resp_16.data_tag;
                    resp_32.rdata_av <= '1';
                else
                    resp_32.data(15 downto 0) <= resp_16.data;
                end if;
            end if;

            -- handle writes
            if pass_wdata='1' then
                wtoggle <= not wtoggle;
            end if;

            -- reset
            if reset='1' then
                wtoggle <= '0';
                rtoggle <= '0';
--                rbuf    <= (others => '0');
                resp_32.data <= (others => '0');
                resp_32.data_tag <= (others => '0');
            end if;
        end if;
    end process;    

    pass_wdata       <= wdata_av and not resp_16.wdata_full;
    req_16.data      <= fifo_wdata(15 downto 0) when wtoggle='0' else fifo_wdata(31 downto 16);
    req_16.byte_en   <= fifo_byte_en(1 downto 0) when wtoggle='0' else fifo_byte_en(3 downto 2);
    req_16.data_push <= pass_wdata;
    get_wdata        <= pass_wdata and wtoggle;
    
    i_write_fifo: entity work.SRL_fifo
    generic map (
        Width     => 36,
        Depth     => 15,
        Threshold => 6 )
    port map (
        clock       => clock,
        reset       => reset,
        GetElement  => get_wdata,
        PutElement  => req_32.data_push,
        FlushFifo   => '0',
        DataIn(35 downto 32)  => req_32.byte_en,
        DataIn(31 downto  0)  => req_32.data,
        DataOut(35 downto 32) => fifo_byte_en,
        DataOut(31 downto  0) => fifo_wdata,
        SpaceInFifo => open,
        AlmostFull  => resp_32.wdata_full,
        DataInFifo  => wdata_av );

end architecture;
