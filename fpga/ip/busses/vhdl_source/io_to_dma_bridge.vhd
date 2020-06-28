library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.dma_bus_pkg.all;

entity io_to_dma_bridge is
generic (
    g_ignore_stop   : boolean := false );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    c64_stopped     : in  std_logic;

    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    
    dma_req         : out t_dma_req;
    dma_resp        : in  t_dma_resp );

end entity;

architecture rtl of io_to_dma_bridge is
    signal dma_req_i    : t_dma_req := c_dma_req_init;
begin
    p_bus: process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;

            if dma_resp.rack='1' then
                dma_req_i.request <= '0';
                if dma_req_i.read_writen='0' then -- was write
                    io_resp.ack <= '1';
                end if;
            end if;
            if dma_resp.dack='1' then
                io_resp.data <= dma_resp.data;
                io_resp.ack <= '1';
            end if;
            
            if io_req.write='1' or io_req.read = '1' then
                dma_req_i.address     <= io_req.address(15 downto 0);
                dma_req_i.read_writen <= io_req.read;
                dma_req_i.data        <= io_req.data;

                if c64_stopped='1' or g_ignore_stop then
                    dma_req_i.request <= '1';
                else
                    io_resp.ack <= '1';
                end if;
            end if;
                        
            if reset='1' then
                dma_req_i.request   <= '0';
            end if;
        end if;
    end process;
    dma_req <= dma_req_i;
end architecture;
