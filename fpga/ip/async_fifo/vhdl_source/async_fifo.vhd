-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006, Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : asynchronous fifo with write_flush
-------------------------------------------------------------------------------
-- Description: Asynchronous fifo for transfer of data between 2 clock domains
--              This variant has a write_flush input. A '1' on the write flush
--              will transfer the current wr_ptr_wr to the read domain as the
--              new read pointer, effectively flushing the buffer. The read
--              domain reports the completion of the flushing, and after that
--              report the wr_inhibit flag is deasserted.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.gray_code_pkg.all;

entity async_fifo is
    generic(
        g_data_width : integer := 36;
        g_depth_bits : integer := 9;  -- depth = 2^depth_bits  (9 == 512 words)
        g_count_bits : integer := 9;
        g_threshold  : integer := 3;
        g_storage    : string  := "auto"  -- can also be "blockram" or
                                          -- "distributed"
        );
    port (
        -- write port signals (synchronized to write clock)
        wr_clock       : in  std_logic;
        wr_reset       : in  std_logic;
        wr_en          : in  std_logic;
        wr_din         : in  std_logic_vector(g_data_width-1 downto 0);
        wr_flush       : in  std_logic := '0';
        wr_count       : out std_logic_vector(g_count_bits-1 downto 0);
        wr_full        : out std_logic;
        wr_almost_full : out std_logic;
        wr_error       : out std_logic;
        wr_inhibit     : out std_logic;

        -- read port signals (synchronized to read clock)
        rd_clock        : in  std_logic;
        rd_reset        : in  std_logic;
        rd_en           : in  std_logic;
        rd_dout         : out std_logic_vector(g_data_width-1 downto 0);
        rd_count        : out std_logic_vector(g_count_bits-1 downto 0);
        rd_empty        : out std_logic;
        rd_almost_empty : out std_logic;
        rd_error        : out std_logic
        );

    ---------------------------------------------------------------------------
    -- synthesis attributes to prevent duplication and balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_duplication               : string;
    attribute register_duplication of async_fifo : entity is "no";
    -- Altera attributes
    attribute dont_replicate                     : boolean;
    attribute dont_replicate of async_fifo       : entity is true;
    
end async_fifo;

architecture rtl of async_fifo is

    ---------------------------------------------------------------------------
    -- constants
    ---------------------------------------------------------------------------
    constant c_depth      : integer := 2 ** g_depth_bits;
    constant c_count_high : integer := g_depth_bits-1;
    constant c_count_low  : integer := g_depth_bits-g_count_bits;

    ---------------------------------------------------------------------------
    -- storage memory for the data
    ---------------------------------------------------------------------------
    type t_mem is array (0 to c_depth-1)
        of std_logic_vector(g_data_width-1 downto 0);
    signal mem : t_mem;


    ---------------------------------------------------------------------------
    -- synthesis attributes to for ram style
    ---------------------------------------------------------------------------
    -- Xilinx and Altera attributes
    attribute ram_style        : string;
    attribute ram_style of mem : signal is g_storage;

    ---------------------------------------------------------------------------
    -- All signals (internal and external) are prefixed with rd or wr.
    -- This indicates the clock-domain in which they are generated (and used,
    -- except from the notable exeptions, the transfer of the pointers).
    ---------------------------------------------------------------------------

    ---------------------------------------------------------------------------
    -- Read and write pointers, both in both domains.
    ---------------------------------------------------------------------------
    signal rd_rd_ptr   : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal wr_wr_ptr   : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal wr_wr_ptr_d : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal wr_rd_ptr   : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal rd_wr_ptr   : t_gray(g_depth_bits-1 downto 0) := (others => '0');

    -- this value is used as the wr_rd_ptr while flushing is in progress.    
    signal wr_wr_ptr_flush : t_gray(g_depth_bits-1 downto 0) := (others => '0');

    -- this value is the position of the writepointer upon a flush,
    -- synchronised to the read clock domain.
    signal rd_wr_ptr_flush : t_gray(g_depth_bits-1 downto 0) := (others => '0');

    -- this value is a temp value to cast the std_logic_vector to a gray value.
    signal rd_wr_ptr_flush_std : std_logic_vector(g_depth_bits-1 downto 0);

    ---------------------------------------------------------------------------
    -- synthesis attributes to prevent duplication and balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_balancing              : string;
    attribute register_balancing of rd_wr_ptr : signal is "no";
    attribute register_balancing of wr_rd_ptr : signal is "no";
    -- Altera attributes
    attribute dont_retime                     : boolean;
    attribute dont_retime of rd_wr_ptr        : signal is true;
    attribute dont_retime of wr_rd_ptr        : signal is true;

    ---------------------------------------------------------------------------
    -- internal flags
    ---------------------------------------------------------------------------
    signal rd_empty_i    : std_logic;
    signal wr_full_i     : std_logic;
    signal rd_en_filt    : std_logic;
    signal wr_en_filt    : std_logic;
    signal rd_count_comb : unsigned(g_depth_bits-1 downto 0);
    signal wr_count_comb : unsigned(g_depth_bits-1 downto 0);
    signal rd_en_decr    : unsigned(g_depth_bits-1 downto 0);
    signal wr_en_incr    : unsigned(g_depth_bits-1 downto 0);
    ---------------------------------------------------------------------------
    -- extra flags for the flushing mechanism
    ---------------------------------------------------------------------------
    signal wr_do_flush   : std_logic;   -- do flush command in write domain
    signal rd_do_flush   : std_logic;   -- do flush command in read domain
    signal rd_flush_done : std_logic;
    
begin
    ---------------------------------------------------------------------------
    -- check parameters
    ---------------------------------------------------------------------------
    assert (g_data_width /= 0)
        report "error: g_data_width may not be 0!"
        severity error;

    assert (g_depth_bits /= 0)
        report "error: g_depth_bits may not be 0!"
        severity error;

    assert (g_count_bits /= 0)
        report "error: g_count_bits may not be 0!"
        severity error;

    assert (g_count_bits <= g_depth_bits)
        report "error: g_count_bits may not be greater than g_depth_bits"
        severity error;

    assert (g_threshold <= 2**g_depth_bits)
        report "error: g_threshold is too big for this g_depth_bits"
        severity error;

    assert (g_threshold > 0)
        report "error: g_threshold may not be smaller or equal to 0"
        severity error;

    ---------------------------------------------------------------------------
    -- filtered read and write enable
    ---------------------------------------------------------------------------
    rd_en_filt <= rd_en and not(rd_empty_i);
    wr_en_filt <= wr_en and not(wr_full_i) and not wr_flush and not wr_do_flush;

    rd_en_decr <= to_unsigned(1, g_depth_bits) when rd_en_filt = '1'else
                  to_unsigned(0, g_depth_bits);

    wr_en_incr <= to_unsigned(1, g_depth_bits) when wr_en_filt = '1'else
                  to_unsigned(0, g_depth_bits);

    ---------------------------------------------------------------------------
    -- read data process
    ---------------------------------------------------------------------------
    read : process(rd_clock)
    begin
        if rd_clock'event and rd_clock = '1' then
            if rd_en_filt = '1' then
                rd_dout <= mem(to_integer(unsigned(rd_rd_ptr)));
            ---------------------------------------------------------------
            -- beware: to_integer(unsigned()) does not do any computations.
            -- So the address is a gray value, coded as a std_locic_vector
            ---------------------------------------------------------------
            end if;
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- write data process
    ---------------------------------------------------------------------------
    write : process(wr_clock)
    begin
        if wr_clock'event and wr_clock = '1' then
            if wr_en_filt = '1' then
                mem(to_integer(unsigned(wr_wr_ptr))) <= wr_din;
            ---------------------------------------------------------------
            -- beware: to_integer(unsigned()) does not do any computations.
            -- So the address is a gray value, coded as a std_locic_vector
            ---------------------------------------------------------------
            end if;
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- read pointer count process, and rd_empty generation
    ---------------------------------------------------------------------------
    rd_count_comb <= to_unsigned(rd_wr_ptr) -
                     to_unsigned(rd_rd_ptr) -
                     rd_en_decr;

    proc_rd_rd_ptr : process (rd_clock)
    begin
        if rising_edge(rd_clock) then
            if rd_en_filt = '1' then
                rd_rd_ptr <= increment(rd_rd_ptr);
            end if;
            rd_wr_ptr <= wr_wr_ptr_d;

            rd_count <= std_logic_vector(rd_count_comb(c_count_high downto c_count_low));

            if (rd_count_comb = 0) then
                rd_empty_i <= '1';
            else
                rd_empty_i <= '0';
            end if;

            if (rd_count_comb <= g_threshold) then
                rd_almost_empty <= '1';
            else
                rd_almost_empty <= '0';
            end if;

            if rd_empty_i = '1' and rd_en = '1' then
                rd_error <= '1';
                report "read error!" severity error;
            else
                rd_error <= '0';
            end if;

            -------------------------------------------------------------------
            -- flush logic
            -------------------------------------------------------------------
            if rd_do_flush = '1' then
                rd_rd_ptr  <= rd_wr_ptr_flush;
                rd_empty_i <= '1';      -- to prevent reading to early
            end if;
            -------------------------------------------------------------------
            -- synchronous reset
            -------------------------------------------------------------------
            if rd_reset = '1' then
                rd_rd_ptr  <= (others => '0');
                rd_count   <= (others => '0');
                rd_empty_i <= '1';
                rd_error   <= '0';
            end if;
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- write pointer count process, and wr_full generation
    ---------------------------------------------------------------------------
    wr_count_comb <= to_unsigned(wr_wr_ptr) -
                     to_unsigned(wr_rd_ptr) +
                     wr_en_incr;
    
    proc_wr_wr_ptr : process (wr_clock)
    begin
        if rising_edge(wr_clock) then
            wr_wr_ptr_d <= wr_wr_ptr;
            if wr_en_filt = '1' then
                wr_wr_ptr <= increment(wr_wr_ptr);
                if wr_do_flush = '0' and wr_flush = '0' then
                    wr_wr_ptr_flush <= increment(wr_wr_ptr);
                -- this should already contain the correct value.
                -- Overruled (inhibited) if wr_do_flush = '1' or
                -- wr_flush = '1'
                end if;
            end if;


            wr_count <= std_logic_vector(wr_count_comb(c_count_high downto c_count_low));

            if (wr_count_comb = c_depth-1) then
                wr_full_i <= '1';
            else
                wr_full_i <= '0';
            end if;

            if (wr_count_comb >= c_depth-g_threshold-1) then
                wr_almost_full <= '1';
            else
                wr_almost_full <= '0';
            end if;

            if wr_do_flush = '1' or wr_flush = '1' then
                wr_rd_ptr <= wr_wr_ptr_flush;
            else
                wr_rd_ptr <= rd_rd_ptr;
            end if;

            if (wr_en = '1' and
                (wr_full_i = '1' or wr_do_flush = '1' or wr_flush = '1'))
            then
                wr_error <= '1';
                report "write error!" severity error;
            else
                wr_error <= '0';
            end if;

            -------------------------------------------------------------------
            -- flush logic
            -------------------------------------------------------------------
            if wr_flush = '1' then
                wr_do_flush <= '1';
            end if;

            if rd_flush_done = '1' then
                wr_do_flush <= '0';
            end if;

            -------------------------------------------------------------------
            -- synchronous reset
            -------------------------------------------------------------------
            if wr_reset = '1' then
                wr_wr_ptr       <= (others => '0');
                wr_wr_ptr_flush <= (others => '0');
                wr_full_i       <= '0';
                wr_almost_full  <= '0';
                wr_count        <= (others => '0');
                wr_error        <= '0';
                wr_do_flush     <= '0';
            end if;
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- fifo status output signals
    ---------------------------------------------------------------------------
    rd_empty   <= rd_empty_i;
    wr_full    <= wr_full_i;
    wr_inhibit <= wr_full_i or wr_do_flush or wr_flush;

    synchroniser_1 : entity work.synchroniser
        generic map (
            g_data_width => (g_depth_bits))
        port map (
            tx_clock    => wr_clock,
            tx_reset    => wr_reset,
            tx_push     => wr_flush,
            tx_data     => std_logic_vector(wr_wr_ptr),
            tx_done     => open,
            rx_clock    => rd_clock,
            rx_reset    => rd_reset,
            rx_new_data => rd_do_flush,
            rx_data     => rd_wr_ptr_flush_std);

    rd_wr_ptr_flush <= t_gray(rd_wr_ptr_flush_std);

    -- this second synchroniser is needed to make sure that the rd_rd_ptr is
    -- operating normal (e.g. not making a jump) around the time that the write
    -- logic starts to read the rd_rd_ptr to wr_rd_ptr
    synchroniser_2 : entity work.synchroniser
        generic map (
            g_data_width => (1))
        port map (
            tx_clock    => rd_clock,
            tx_reset    => rd_reset,
            tx_push     => rd_do_flush,
            tx_data     => "0",         -- not used.
            tx_done     => open,
            rx_clock    => wr_clock,
            rx_reset    => wr_reset,
            rx_new_data => rd_flush_done,
            rx_data     => open);

end rtl;
