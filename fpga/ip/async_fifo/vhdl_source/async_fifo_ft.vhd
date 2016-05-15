-------------------------------------------------------------------------------
-- Title      : asynchronous fall-through fifo
-- Author     : Gideon Zweijtzer (gideon.zweijtzer@gmail.com)
-------------------------------------------------------------------------------
-- Description: Asynchronous fifo for transfer of data between 2 clock domains
-------------------------------------------------------------------------------
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

library work;
    use work.gray_code_pkg.all;

entity async_fifo_ft is
    generic(
        g_fast       : boolean := true;
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
        rd_valid        : out std_logic );

    ---------------------------------------------------------------------------
    -- synthesis attributes to prevent duplication and balancing.
    ---------------------------------------------------------------------------
    -- Xilinx attributes
    attribute register_duplication                      : string;
    attribute register_duplication of async_fifo_ft     : entity is "no";
    -- Altera attributes
    attribute dont_replicate                            : boolean;
    attribute dont_replicate of async_fifo_ft           : entity is true;
    ---------------------------------------------------------------------------
end entity;

architecture rtl of async_fifo_ft is
    function iif(condition : boolean; if_true : std_logic; if_false : std_logic) return std_logic is
    begin
        if condition then
            return if_true;
        else
            return if_false;
        end if;
    end function;

    constant c_edge : std_logic := iif(g_fast, '0', '1');

    ---------------------------------------------------------------------------
    -- constants
    ---------------------------------------------------------------------------
    constant c_depth : natural := 2 ** g_depth_bits;

    ---------------------------------------------------------------------------
    -- storage memory for the data
    ---------------------------------------------------------------------------
    type t_mem is array (0 to c_depth-1) of std_logic_vector(g_data_width-1 downto 0);
    signal mem : t_mem;

--    ---------------------------------------------------------------------------
--    -- synthesis attributes to for ram style
--    ---------------------------------------------------------------------------
--    -- Xilinx and Altera attributes
--    attribute ram_style        : string;
--    attribute ramstyle         : string;
--    attribute ram_style of mem : signal is g_storage;
--    attribute ramstyle of mem  : signal is g_storage;

    ---------------------------------------------------------------------------
    -- All signals (internal and external) are prefixed with rd or wr.
    -- This indicates the clock-domain in which they are generated and used.
    ---------------------------------------------------------------------------
    ---------------------------------------------------------------------------
    -- Reset
    ---------------------------------------------------------------------------
    signal aclr                 : std_logic;
    signal aclr_wr              : std_logic_vector(2 downto 0);
    signal aclr_rd              : std_logic_vector(2 downto 0);
    signal rd_reset_i           : std_logic;
    signal wr_reset_i           : std_logic;
    
    ---------------------------------------------------------------------------
    -- Read and write pointers, both in both domains.
    ---------------------------------------------------------------------------
    signal wr_head              : unsigned(g_depth_bits-1 downto 0) := (others => '0');
    signal wr_head_next         : unsigned(g_depth_bits-1 downto 0) := (others => '0');
    signal wr_tail              : unsigned(g_depth_bits-1 downto 0) := (others => '0');

    signal rd_tail              : unsigned(g_depth_bits-1 downto 0) := (others => '0');
    signal rd_tail_next         : unsigned(g_depth_bits-1 downto 0) := (others => '0');
    signal rd_head              : unsigned(g_depth_bits-1 downto 0) := (others => '0');

    ---------------------------------------------------------------------------
    -- temporaries
    ---------------------------------------------------------------------------
    signal wr_head_gray_tig_src : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal rd_head_gray_tig_dst : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal rd_head_gray         : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    
    signal rd_tail_gray_tig_src : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal wr_tail_gray_tig_dst : t_gray(g_depth_bits-1 downto 0) := (others => '0');
    signal wr_tail_gray         : t_gray(g_depth_bits-1 downto 0) := (others => '0');

    ---------------------------------------------------------------------------
    -- internal flags
    ---------------------------------------------------------------------------
    signal rd_empty_i    : std_logic;
    signal wr_full_i     : std_logic;
    signal rd_en_filt    : std_logic;
    signal wr_en_filt    : std_logic;

begin
    ---------------------------------------------------------------------------
    -- reset generation
    ---------------------------------------------------------------------------
    aclr <= wr_reset or rd_reset;
    process(wr_clock, aclr)
    begin
        if aclr = '1' then
            aclr_wr <= (others => '0');
        elsif rising_edge(wr_clock) then
            aclr_wr <= '1' & aclr_wr(2 downto 1);
        end if;
    end process;
    wr_reset_i <= not aclr_wr(0);

    process(rd_clock, aclr)
    begin
        if aclr = '1' then
            aclr_rd <= (others => '0');
        elsif rising_edge(rd_clock) then
            aclr_rd <= '1' & aclr_rd(2 downto 1);
        end if;
    end process;
    rd_reset_i <= not aclr_rd(0);

    ---------------------------------------------------------------------------
    -- filtered read and write enable
    ---------------------------------------------------------------------------
    rd_en_filt <= rd_next and not rd_empty_i;
    wr_en_filt <= wr_en and not wr_full_i;

    ---------------------------------------------------------------------------
    -- write data process
    ---------------------------------------------------------------------------
    process(wr_en_filt, wr_head)
    begin
        wr_head_next <= wr_head;
        if wr_en_filt = '1' then
            wr_head_next <= wr_head + 1;
        end if;
    end process;

    p_write : process(wr_clock)
    begin
        if rising_edge(wr_clock) then
            if wr_en_filt = '1' then
                mem(to_integer(wr_head)) <= wr_din;
            end if;
            if wr_reset_i = '1' then
                wr_head <= (others => '0');
            else
                wr_head <= wr_head_next;
            end if;                
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- read data process
    ---------------------------------------------------------------------------
    process(rd_en_filt, rd_tail)
    begin
        rd_tail_next <= rd_tail;

        if rd_en_filt = '1' then
            rd_tail_next <= rd_tail + 1;
        end if;
    end process;

    p_read : process(rd_clock)
    begin
        if rising_edge(rd_clock) then
            rd_dout <= mem(to_integer(unsigned(rd_tail_next)));

            if rd_reset_i = '1' then
                rd_tail <= (others => '0');
            else
                rd_tail <= rd_tail_next;
            end if;
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- synchronize pointers to the other side
    ---------------------------------------------------------------------------
    p_wr_sync: process(wr_clock)
    begin
        if rising_edge(wr_clock) then
            -- second flop. We are now stable
            wr_tail_gray <= wr_tail_gray_tig_dst;
            -- conversion to binary may infer a longer xor chain, so we use a whole clock cycle here.
            wr_tail <= to_unsigned(wr_tail_gray);
        end if;
        if wr_clock'event and wr_clock = c_edge then
            -- conversion from binary to gray is very fast (one xor), so we will win some time
            -- by using the falling edge (if c_edge = '0')
            wr_head_gray_tig_src <= to_gray(wr_head); 
            
            -- two synchronization flipflops after one another is good for metastability
            -- but bad for latency, so we use a falling edge flop for the first stage (if c_edge = '0')
            wr_tail_gray_tig_dst <= rd_tail_gray_tig_src;
        end if;
    end process;
    
    p_rd_sync: process(rd_clock)
    begin
        if rising_edge(rd_clock) then
            -- second flop. We are now stable
            rd_head_gray <= rd_head_gray_tig_dst; 
            -- conversion to binary may infer a longer xor chain, so we use a whole clock cycle here.
            rd_head <= to_unsigned(rd_head_gray);
        end if;

        if rd_clock'event and rd_clock = c_edge then
            -- two synchronization flipflops after one another is good for metastability
            -- but bad for latency, so we use a falling edge flop for the first stage (if c_edge = '0')
            rd_head_gray_tig_dst <= wr_head_gray_tig_src;

            -- conversion from binary to gray is very fast (one xor), so we will win some time
            -- by using the falling edge (if c_edge = '0')
            rd_tail_gray_tig_src <= to_gray(rd_tail); 
        end if;
    end process;
    
    ---------------------------------------------------------------------------
    -- read empty generation
    ---------------------------------------------------------------------------
    p_proc_read_count : process (rd_clock)
        variable v_next_count : unsigned(rd_head'range);
    begin
        if rising_edge(rd_clock) then
            v_next_count := rd_head - rd_tail_next;

            if v_next_count = 0 then
                rd_empty_i <= '1';
            else
                rd_empty_i <= '0';
            end if;

            -------------------------------------------------------------------
            -- synchronous reset
            -------------------------------------------------------------------
            if rd_reset_i = '1' then
                rd_empty_i <= '1';
            end if;
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- write full generation
    ---------------------------------------------------------------------------
    p_proc_write_count : process (wr_clock, wr_reset_i)
        variable v_next_count : unsigned(wr_tail'range);
    begin
        if rising_edge(wr_clock) then
            v_next_count := wr_head_next - wr_tail;

            if signed(v_next_count) = -1 then
                wr_full_i <= '1';
            else
                wr_full_i <= '0';
            end if;
        end if;
        
        -------------------------------------------------------------------
        -- asynchronous reset
        -------------------------------------------------------------------
        if wr_reset_i = '1' then
            wr_full_i <= '1';
        end if;
    end process;

    ---------------------------------------------------------------------------
    -- fifo status output signals
    ---------------------------------------------------------------------------
    rd_valid        <= not rd_empty_i;
    wr_full         <= wr_full_i;
end rtl;
