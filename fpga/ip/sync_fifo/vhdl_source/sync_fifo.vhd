library ieee; 
    use ieee.std_logic_1164.all;
    use ieee.std_logic_arith.all;
    use ieee.std_logic_unsigned.all;

entity sync_fifo is
    generic (
        g_depth        : integer := 512;        -- Actual depth. 
        g_data_width   : integer := 32;
        g_threshold    : integer := 13;
        g_storage      : string  := "auto";     -- can also be "blockram" or "distributed"
        g_fall_through : boolean := false);
    port (
        clock       : in  std_logic;
        reset       : in  std_logic;

        rd_en       : in  std_logic;
        wr_en       : in  std_logic;

        din         : in  std_logic_vector(g_data_width-1 downto 0);
        dout        : out std_logic_vector(g_data_width-1 downto 0);

        flush       : in  std_logic;

        full        : out std_logic;
        almost_full : out std_logic;
        empty       : out std_logic;
        count       : out integer range 0 to g_depth
    );
end sync_fifo;


architecture  rtl  of  sync_fifo  is

    subtype t_data_element is std_logic_vector(g_data_width-1 downto 0);
    type    t_data_array   is array (0 to g_depth-1) of t_data_element;

    signal data_array   : t_data_array;   

    attribute ram_style        : string;
    attribute ram_style of data_array : signal is g_storage;

    signal rd_data      : std_logic_vector(g_data_width-1 downto 0);
    signal din_reg      : std_logic_vector(g_data_width-1 downto 0);
    
    signal rd_inhibit   : std_logic;
    signal rd_inhibit_d : std_logic;

    signal rd_en_flt    : std_logic;
    signal rd_enable    : std_logic;
    signal rd_pnt       : integer range 0 to g_depth-1;
    signal rd_pnt_next  : integer range 0 to g_depth-1;
    signal rd_index     : integer range 0 to g_depth-1;

    signal wr_en_flt    : std_logic;    
    signal wr_pnt       : integer range 0 to g_depth-1;

    signal num_el       : integer range 0 to g_depth;

begin

    -- Check generic values (also for synthesis)
    assert(g_threshold <= g_depth) report "Invalid parameter 'g_threshold'" severity failure;

    -- Filter fifo read/write enables for full/empty conditions
    rd_en_flt <= '1' when (num_el /= 0)       and (rd_en='1') else '0';    
    wr_en_flt <= '1' when (num_el /= g_depth) and (wr_en='1') else '0';
        
    -- Read enable depends on 'fall through' mode. In case fall through: prevent
    -- read & write at same address (when fifo is empty)
    rd_enable <= rd_en_flt  when not(g_fall_through) else
                 '0'        when rd_inhibit = '1' else         
                 '1';

    rd_inhibit <= '1' when rd_index = wr_pnt and wr_en_flt = '1' and g_fall_through else '0';

    rd_index <= rd_pnt_next when g_fall_through and rd_en_flt = '1' and num_el /= 0 else rd_pnt;
               
    -- FIFO output data. Combinatoric switch to fix simultaneous read/write issues. 
    dout <= din_reg when rd_inhibit_d = '1' else rd_data;

    p_dpram: process(clock)
    begin
        if rising_edge(clock) then
            if (wr_en_flt = '1') then
                data_array(wr_pnt) <= din;
            end if;
            if (rd_enable = '1') then
                rd_data <= data_array(rd_index);
            end if;
        end if;
    end process;

    rd_pnt_next <= 0 when (rd_pnt=g_depth-1) else rd_pnt + 1;
    process(clock)
        variable v_new_cnt : integer range 0 to g_depth;
    begin
        if (clock'event and clock='1') then

            rd_inhibit_d <= rd_inhibit;           
    
            -- Modify read/write pointers
            if (rd_en_flt='1') then
                rd_pnt <= rd_pnt_next;
            end if;
            
            if (wr_en_flt='1') then           
                -- Registered din is needed for BlockRAM based 'fall through' FIFO
                din_reg      <= din;
                if (wr_pnt=g_depth-1) then
                    wr_pnt <= 0;
                else
                    wr_pnt <= wr_pnt + 1;
                end if;
            end if;

            -- Update number of elements in fifo for next clock cycle
            if (rd_en_flt = '1') and (wr_en_flt = '0') then
                v_new_cnt := num_el - 1;
            elsif (rd_en_flt = '0') and (wr_en_flt = '1') then
                v_new_cnt := num_el + 1;
            elsif (flush='1') then
                v_new_cnt := 0;
            else
                v_new_cnt := num_el;
            end if;           
            num_el <= v_new_cnt;

            -- update (almost)full and empty indications
            almost_full <= '0';
            if (v_new_cnt >= g_threshold) then
                almost_full <= '1';
            end if;

            empty <= '0';
            if (v_new_cnt = 0) then
                empty <= '1';
            end if;

            full <= '0';
            if (v_new_cnt = g_depth) then
                full <= '1';
            end if;                                    

            if (flush='1') or (reset='1') then
                rd_pnt  <= 0;
                wr_pnt  <= 0;
                num_el <= 0;
                rd_inhibit_d <= '0';
                if (reset='1') then
                    full         <= '0';
                    empty        <= '1';
                    almost_full  <= '0';
                end if;
            end if;

        end if;
    end process;

    count <= num_el;
    
end rtl;
