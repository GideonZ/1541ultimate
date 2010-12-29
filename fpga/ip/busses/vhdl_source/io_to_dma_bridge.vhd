library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.dma_bus_pkg.all;

entity io_to_dma_bridge is
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
    signal dma_rwn_i    : std_logic;
begin
    dma_req.address     <= io_req.address(15 downto 0);
    dma_req.read_writen <= dma_rwn_i;
    dma_req.data        <= io_req.data;
    
    p_bus: process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;

            if dma_resp.rack='1' then
                dma_req.request <= '0';
                if dma_rwn_i='0' then -- was write
                    io_resp.ack <= '1';
                end if;
            end if;
            if dma_resp.dack='1' then
                io_resp.data <= dma_resp.data;
                io_resp.ack <= '1';
            end if;
            
            if io_req.write='1' then
                if c64_stopped='1' then
                    dma_req.request <= '1';
                    dma_rwn_i <= '0';
                else
                    io_resp.ack <= '1';
                end if;
            elsif io_req.read='1' then
                if c64_stopped='1' then
                    dma_req.request <= '1';
                    dma_rwn_i <= '1';
                else
                    io_resp.ack <= '1';
                end if;
            end if;
                        
            if reset='1' then
                dma_req.request   <= '0';
                dma_rwn_i <= '1';
            end if;
        end if;
    end process;
end architecture;
