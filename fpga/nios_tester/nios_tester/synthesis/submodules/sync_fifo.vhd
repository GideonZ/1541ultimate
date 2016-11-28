library ieee; 
    use ieee.std_logic_1164.all;

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
        valid       : out std_logic;
        count       : out integer range 0 to g_depth
    );
end sync_fifo;


architecture  rtl  of  sync_fifo  is

    subtype t_data_element is std_logic_vector(g_data_width-1 downto 0);
    type    t_data_array   is array (0 to g_depth-1) of t_data_element;

    signal data_array   : t_data_array;   

    attribute ram_style        : string;
    attribute ram_style of data_array : signal is g_storage;
    attribute ramstyle        : string;
    attribute ramstyle of data_array : signal is g_storage;

    signal rd_en_flt    : std_logic;
    signal rd_enable    : std_logic;
    signal rd_pnt       : integer range 0 to g_depth-1;
    signal rd_pnt_next  : integer range 0 to g_depth-1;

    signal wr_en_flt    : std_logic;    
    signal wr_pnt       : integer range 0 to g_depth-1;

    signal num_el       : integer range 0 to g_depth;
    signal full_i       : std_logic;
    signal empty_i      : std_logic;
    signal valid_i      : std_logic;
begin

    -- Check generic values (also for synthesis)
    assert(g_threshold <= g_depth) report "Invalid parameter 'g_threshold'" severity failure;

    -- Filter fifo read/write enables for full/empty conditions
    rd_en_flt <= '1' when (empty_i = '0') and (rd_en='1') else '0';    
    wr_en_flt <= '1' when (full_i = '0')  and (wr_en='1') else '0';
        
    -- Read enable depends on 'fall through' mode.
    -- In fall through mode, rd_enable is true when fifo is not empty and either rd_en_flt is '1' or output is invalid
    -- In non-fall through mode, rd_enable is only true when rd_en_flt = '1'
    rd_enable <= rd_en_flt or (not empty_i and not valid_i) when g_fall_through else
                 rd_en_flt;
               
    p_dpram: process(clock)
    begin
        if rising_edge(clock) then
            if (wr_en_flt = '1') then
                data_array(wr_pnt) <= din;
            end if;
            if (rd_enable = '1') then
                dout <= data_array(rd_pnt);
            end if;
        end if;
    end process;

    rd_pnt_next <= 0 when (rd_pnt=g_depth-1) else rd_pnt + 1;

    process(clock)
        variable v_new_cnt : integer range 0 to g_depth;
    begin
        if rising_edge(clock) then

            -- data on the output becomes valid after an external read OR after an automatic read when fifo is not empty.
            if rd_en = '1' then
                valid_i <= not empty_i;
            elsif valid_i = '0' and empty_i = '0' and g_fall_through then
                valid_i <= '1';
            end if;
    
            -- Modify read/write pointers
            if (rd_enable='1') then
                rd_pnt <= rd_pnt_next;
            end if;
            
            if (wr_en_flt='1') then           
                if (wr_pnt=g_depth-1) then
                    wr_pnt <= 0;
                else
                    wr_pnt <= wr_pnt + 1;
                end if;
            end if;

            -- Update number of elements in fifo for next clock cycle
            if (rd_enable = '1') and (wr_en_flt = '0') then
                v_new_cnt := num_el - 1;
            elsif (rd_enable = '0') and (wr_en_flt = '1') then
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

            empty_i <= '0';
            if (v_new_cnt = 0) then
                empty_i <= '1';
            end if;

            full_i <= '0';
            if (v_new_cnt = g_depth) then
                full_i <= '1';
            end if;                                    

            if (flush='1') or (reset='1') then
                rd_pnt      <= 0;
                wr_pnt      <= 0;
                num_el      <= 0;
                full_i      <= '0';
                empty_i     <= '1';
                valid_i     <= '0';
                almost_full <= '0';
            end if;
        end if;
    end process;

    count <= num_el;
    empty <= empty_i when not g_fall_through else not valid_i;
    full  <= full_i;
    valid <= valid_i;
        
end rtl;
