--------------------------------------------------------------------------------
-- Entity: avalon_usb
-- Date:2016-11-03  
-- Author: Gideon     
--
-- Description: AvalonMM wrapper around usb peripheral for use in QSYS
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity avalon_usb is
port (
    sys_clock           : in  std_logic;
    sys_reset           : in  std_logic;
    
    -- 8 bits Avalon bus interface
    avs_read            : in  std_logic;
    avs_write           : in  std_logic;
    avs_address         : in  std_logic_vector(12 downto 0);
    avs_writedata       : in  std_logic_vector(7 downto 0);
    avs_ready           : out std_logic;
    avs_readdata        : out std_logic_vector(7 downto 0);
    avs_readdatavalid   : out std_logic;
    avs_irq             : out std_logic;

    -- Memory master port for DMA
    avm_read            : out std_logic;
    avm_write           : out std_logic;
    avm_address         : out std_logic_vector(25 downto 0);
    avm_writedata       : out std_logic_vector(31 downto 0);
    avm_byteenable      : out std_logic_vector(3 downto 0);
    avm_ready           : in  std_logic;
    avm_readdata        : in  std_logic_vector(31 downto 0);
    avm_readdatavalid   : in  std_logic;

    -- Conduit with ULPI signals
    ulpi_clock          : in    std_logic;
    ulpi_reset          : in    std_logic;
    ulpi_nxt            : in    std_logic;
    ulpi_dir            : in    std_logic;
    ulpi_stp            : out   std_logic;
    ulpi_data           : inout std_logic_vector(7 downto 0));

end entity;

architecture arch of avalon_usb is
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    signal mem_req      : t_mem_req_32;
    signal mem_resp     : t_mem_resp_32;
    
begin
    i_bridge: entity work.avalon_to_io_bridge
    port map (
        clock             => sys_clock,
        reset             => sys_reset,

        avs_read          => avs_read,
        avs_write         => avs_write,
        avs_address(19 downto 13) => "0000000",
        avs_address(12 downto  0) => avs_address,
        avs_writedata     => avs_writedata,
        avs_ready         => avs_ready,
        avs_readdata      => avs_readdata,
        avs_readdatavalid => avs_readdatavalid,
        avs_irq           => open,

        io_read           => io_req.read,
        io_write          => io_req.write,
        unsigned(io_address) => io_req.address,
        io_wdata          => io_req.data,
        io_rdata          => io_resp.data,
        io_ack            => io_resp.ack,
        io_irq            => '0'
    );
    
    i_wrapped: entity work.usb_host_nano
    generic map(
        g_big_endian => false,
        g_tag        => X"01",
        g_simulation => false
    )
    port map (
        clock        => ulpi_clock,
        reset        => ulpi_reset,
        ulpi_nxt     => ulpi_nxt,
        ulpi_dir     => ulpi_dir,
        ulpi_stp     => ulpi_stp,
        ulpi_data    => ulpi_data,

        sys_clock    => sys_clock,
        sys_reset    => sys_reset,
        sys_mem_req  => mem_req,
        sys_mem_resp => mem_resp,
        sys_io_req   => io_req,
        sys_io_resp  => io_resp,
        sys_irq      => avs_irq
    );
    
    avm_read       <= mem_req.read_writen and mem_req.request;
    avm_write      <= not mem_req.read_writen and mem_req.request;
    avm_address    <= std_logic_vector(mem_req.address);
    avm_byteenable <= mem_req.byte_en;
    avm_writedata  <= mem_req.data;
    
    mem_resp.data     <= avm_readdata;
    mem_resp.dack_tag <= X"01" when avm_readdatavalid='1' else X"00";
    mem_resp.rack_tag <= X"01" when avm_ready='1' else X"00";
    mem_resp.rack     <= avm_ready; 
end arch;
