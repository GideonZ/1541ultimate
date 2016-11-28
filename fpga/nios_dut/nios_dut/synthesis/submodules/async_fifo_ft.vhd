-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2016, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : asynchronous fifo - Fall through mode
-------------------------------------------------------------------------------
-- Description: Asynchronous fifo for transfer of data between 2 clock domains
--              This is a simple fifo without "almost full" and count outputs.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library altera_mf;
use altera_mf.all;

entity async_fifo_ft is
    generic(
        g_data_width : integer := 36;
        g_depth_bits : integer := 9   -- depth = 2^depth_bits  (9 == 512 words)
    );
    port (
        -- write port signals (synchronized to write clock)
        wr_clock       : in  std_logic;
        wr_reset       : in  std_logic;
        wr_en          : in  std_logic;
        wr_din         : in  std_logic_vector(g_data_width-1 downto 0);
        wr_full        : out std_logic;

        -- read port signals (synchronized to read clock)
        rd_clock        : in  std_logic;
        rd_reset        : in  std_logic;
        rd_next         : in  std_logic;
        rd_dout         : out std_logic_vector(g_data_width-1 downto 0);
        rd_count        : out unsigned(g_depth_bits downto 0);
        rd_valid        : out std_logic );

    ---------------------------------------------------------------------------
    -- synthesis attributes to prevent duplication and balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_duplication                  : string;
    attribute register_duplication of async_fifo_ft : entity is "no";
    -- Altera attributes
    attribute dont_replicate                        : boolean;
    attribute dont_replicate of async_fifo_ft       : entity is true;
    
end entity;


architecture altera of async_fifo_ft is
    signal aclr     : std_logic;
    signal rdempty  : std_logic;
    signal rdusedw  : std_logic_vector(g_depth_bits downto 0);
    constant c_num_words : natural := 2 ** g_depth_bits;

    COMPONENT dcfifo
    GENERIC (
        add_usedw_msb_bit       : STRING;
        intended_device_family      : STRING;
        lpm_numwords        : NATURAL;
        lpm_showahead       : STRING;
        lpm_type        : STRING;
        lpm_width       : NATURAL;
        lpm_widthu      : NATURAL;
        overflow_checking       : STRING;
        rdsync_delaypipe        : NATURAL;
        read_aclr_synch     : STRING;
        underflow_checking      : STRING;
        use_eab     : STRING;
        write_aclr_synch        : STRING;
        wrsync_delaypipe        : NATURAL
    );
    PORT (
            aclr    : IN STD_LOGIC ;
            data    : IN STD_LOGIC_VECTOR (g_data_width-1 DOWNTO 0);
            rdclk   : IN STD_LOGIC ;
            rdreq   : IN STD_LOGIC ;
            wrclk   : IN STD_LOGIC ;
            wrreq   : IN STD_LOGIC ;
            q   : OUT STD_LOGIC_VECTOR (g_data_width-1 DOWNTO 0);
            rdempty : OUT STD_LOGIC ;
            rdusedw : OUT STD_LOGIC_VECTOR(g_depth_bits DOWNTO 0);
            wrfull  : OUT STD_LOGIC 
    );
    END COMPONENT;
begin
    aclr <= wr_reset or rd_reset;
    
    dcfifo_component : dcfifo
    generic map (
        add_usedw_msb_bit => "ON",
        intended_device_family => "Cyclone IV E",
        lpm_numwords => c_num_words,
        lpm_showahead => "ON",
        lpm_type => "dcfifo",
        lpm_width => g_data_width,
        lpm_widthu => g_depth_bits+1,
        overflow_checking => "ON",
        rdsync_delaypipe => 4,
        read_aclr_synch => "ON",
        underflow_checking => "ON",
        use_eab => "ON",
        write_aclr_synch => "ON",
        wrsync_delaypipe => 4
    )
    port map (
        aclr  => aclr,

        wrclk   => wr_clock,
        wrreq   => wr_en,
        data    => wr_din,
        wrfull  => wr_full,

        rdclk   => rd_clock,
        rdreq   => rd_next,
        q       => rd_dout,
        rdempty => rdempty,
        rdusedw => rdusedw
    );
    rd_valid <= not rdempty;
    rd_count <= unsigned(rdusedw);
end architecture;
