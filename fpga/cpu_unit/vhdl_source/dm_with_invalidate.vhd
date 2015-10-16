--------------------------------------------------------------------------------
-- Entity: dm_with_invalidate
-- Date: 2014-12-08  
-- Author: Gideon     
--
-- Description: Simple direct mapped cache controller, compatible with the
--              D bus of the mblite. This version has an invalidate port. 
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library mblite;
use mblite.core_Pkg.all;

entity dm_with_invalidate is
    generic (
        g_address_swap  : std_logic_vector(31 downto 0) := X"00000000" );
    port  (
        clock       : in  std_logic;
        reset       : in  std_logic;
        
        invalidate  : in  std_logic;
        inv_addr    : in  std_logic_vector(31 downto 0);

        dmem_i      : in  dmem_out_type;
        dmem_o      : out dmem_in_type;
        
        mem_o       : out dmem_out_type;
        mem_i       : in  dmem_in_type );

end entity;

architecture arch of dm_with_invalidate is
    constant c_cachable_area_bits : natural := 25;
    constant c_cache_size_bits    : natural := 11; -- 2**11 bytes = 2KB
    constant c_tag_size_bits      : natural := c_cache_size_bits - 2; -- 4 bytes per cache entry

    type t_tag is record
        addr_high   : std_logic_vector(c_cachable_area_bits-1 downto c_cache_size_bits);
        valid       : std_logic;
    end record;

    constant c_valid_zero_tag : t_tag := ( addr_high => (others => '0'), valid => '1' );

    function extend32(a : std_logic_vector) return std_logic_vector is
        variable ret : std_logic_vector(31 downto 0) := (others => '0');
    begin
        ret(a'length-1 downto 0) := a;
        return ret;        
    end function;
    
    function address_to_tag (addr  : std_logic_vector;
                             valid : std_logic) return t_tag is
        variable v_addr : std_logic_vector(31 downto 0);
        variable ret : t_tag; 
    begin
        v_addr := extend32(addr);
        ret.addr_high := v_addr(c_cachable_area_bits-1 downto c_cache_size_bits);
        ret.valid := valid;
        return ret;
    end function;
    
    constant c_tag_width : natural := c_cachable_area_bits - c_cache_size_bits + 1;
    function tag_to_vector(i: t_tag) return std_logic_vector is
    begin
        return i.valid & i.addr_high;
    end function;

    constant c_valid_zero_tag_vector : std_logic_vector(c_tag_width-1 downto 0) := tag_to_vector(c_valid_zero_tag);
    
    function vector_to_tag(i : std_logic_vector(c_tag_width-1 downto 0)) return t_tag is
        variable ret : t_tag;
    begin
        ret.valid := i(c_tag_width-1);
        ret.addr_high := i(c_tag_width-2 downto 0);        
        return ret;
    end function;

    function get_tag_index (addr : std_logic_vector) return unsigned is
    begin
        return unsigned(addr(c_tag_size_bits+1 downto 2));
    end function;
     
    function is_cacheable (addr : std_logic_vector) return boolean is
        variable v_addr : std_logic_vector(31 downto 0);
    begin
        v_addr := extend32(addr);
        return unsigned(v_addr(31 downto c_cachable_area_bits)) = 0;
    end function;


    signal tag_ram_a_address    : unsigned(c_tag_size_bits-1 downto 0);
    signal tag_ram_a_rdata      : std_logic_vector(c_tag_width-1 downto 0);
    signal tag_ram_a_wdata      : std_logic_vector(c_tag_width-1 downto 0);
    signal tag_ram_a_en         : std_logic;
    signal tag_ram_a_we         : std_logic;

    signal tag_ram_b_address    : unsigned(c_tag_size_bits-1 downto 0) := (others => '0');
    signal tag_ram_b_rdata      : std_logic_vector(c_tag_width-1 downto 0) := (others => '0');
    signal tag_ram_b_wdata      : std_logic_vector(c_tag_width-1 downto 0) := (others => '0');
    signal tag_ram_b_en         : std_logic := '0';
    signal tag_ram_b_we         : std_logic := '0';

    signal cache_ram_a_address  : unsigned(c_cache_size_bits-1 downto 2);
    signal cache_ram_a_rdata    : std_logic_vector(31 downto 0);
    signal cache_ram_a_wdata    : std_logic_vector(31 downto 0);
    signal cache_ram_a_en       : std_logic;
    signal cache_ram_a_we       : std_logic;

    signal cache_ram_b_address  : unsigned(c_cache_size_bits-1 downto 2) := (others => '0');
    signal cache_ram_b_rdata    : std_logic_vector(31 downto 0) := (others => '0');
    signal cache_ram_b_wdata    : std_logic_vector(31 downto 0) := (others => '0');
    signal cache_ram_b_en       : std_logic := '0';
    signal cache_ram_b_we       : std_logic := '0';

    signal d_tag_ram_out        : t_tag;
    signal d_miss               : std_logic;
    signal dmem_r               : dmem_out_type;

    signal dmem_o_comb          : dmem_in_type;
    signal dmem_o_reg           : dmem_in_type;

    type t_state is (idle, fill);
    signal state        : t_state;
begin
    i_tag_ram: entity work.dpram_sc
    generic map (
        g_width_bits   => c_tag_width,
        g_depth_bits   => c_tag_size_bits,
        g_global_init  => c_valid_zero_tag_vector,
        g_read_first_a => true,
        g_read_first_b => true,
        g_storage      => "block" )
    port map (
        clock          => clock,
        a_address      => tag_ram_a_address,
        a_rdata        => tag_ram_a_rdata,
        a_wdata        => tag_ram_a_wdata,
        a_en           => tag_ram_a_en,
        a_we           => tag_ram_a_we,
        b_address      => tag_ram_b_address,
        b_rdata        => tag_ram_b_rdata,
        b_wdata        => tag_ram_b_wdata,
        b_en           => tag_ram_b_en,
        b_we           => tag_ram_b_we );
        
    i_cache_ram: entity work.dpram_sc
    generic map (
        g_width_bits   => 32,
        g_depth_bits   => c_cache_size_bits-2,
        g_global_init  => X"FFFFFFFF",
        g_read_first_a => true,
        g_read_first_b => true,
        g_storage      => "block" )
    port map (
        clock          => clock,
        a_address      => cache_ram_a_address,
        a_rdata        => cache_ram_a_rdata,
        a_wdata        => cache_ram_a_wdata,
        a_en           => cache_ram_a_en,
        a_we           => cache_ram_a_we,
        b_address      => cache_ram_b_address,
        b_rdata        => cache_ram_b_rdata,
        b_wdata        => cache_ram_b_wdata,
        b_en           => cache_ram_b_en,
        b_we           => cache_ram_b_we );

    d_tag_ram_out <= vector_to_tag(tag_ram_a_rdata);

    -- handle the dmem address request here; split it up
    process(state, dmem_i, dmem_r, mem_i, d_tag_ram_out, cache_ram_a_rdata, invalidate, inv_addr)
    begin
        dmem_o_comb.ena_i <= '0'; -- registered out, use this signal as register load enable
        dmem_o_comb.dat_i <= (others => 'X');
        d_miss <= '0';
        
        tag_ram_a_address   <= get_tag_index(dmem_i.adr_o);
        tag_ram_a_wdata     <= (others => 'X');
        tag_ram_a_we        <= '0';
        tag_ram_a_en        <= '0';

        cache_ram_a_address <= unsigned(dmem_i.adr_o(c_cache_size_bits-1 downto 2));
        cache_ram_a_wdata   <= dmem_i.dat_o;
        cache_ram_a_we      <= '0';
        cache_ram_a_en      <= '0';

        tag_ram_b_address   <= get_tag_index(dmem_r.adr_o);
        tag_ram_b_wdata     <= tag_to_vector(address_to_tag(dmem_r.adr_o, '1'));
        tag_ram_b_we        <= '0';
        tag_ram_b_en        <= '0';

        cache_ram_b_address <= unsigned(dmem_r.adr_o(c_cache_size_bits-1 downto 2));
        cache_ram_b_wdata   <= mem_i.dat_i;
        cache_ram_b_we      <= '0';
        cache_ram_b_en      <= '0';

        if invalidate = '1' then
            tag_ram_a_address <= get_tag_index(inv_addr);
            tag_ram_a_wdata   <= tag_to_vector(address_to_tag(inv_addr, '0')); -- invalid
            tag_ram_a_en      <= '1';
            tag_ram_a_we      <= '1';
            
        elsif dmem_i.ena_o = '1' then -- processor address is valid, let's do our thing
            if dmem_i.we_o = '0' then -- read
                tag_ram_a_en <= '1';
                cache_ram_a_en <= '1';
            else -- write
                tag_ram_a_en <= '1';
                cache_ram_a_en <= '1';
                if is_cacheable(dmem_i.adr_o) then
                    tag_ram_a_we <= '1';
                    cache_ram_a_we <= '1';
                end if;                
                if dmem_i.sel_o = "1111" then -- full word results in a valid cache line
                    tag_ram_a_wdata <= tag_to_vector(address_to_tag(dmem_i.adr_o, '1')); -- valid
                else
                    tag_ram_a_wdata <= tag_to_vector(address_to_tag(dmem_i.adr_o, '0')); -- invalid
                end if;
            end if;
        end if;

        -- response to processor
        case state is
        when idle =>
            if dmem_r.ena_o = '1' then -- registered (=delayed request valid)
                if (address_to_tag(dmem_r.adr_o, '1') = d_tag_ram_out) and (dmem_r.we_o='0') and is_cacheable(dmem_r.adr_o) then -- read hit!
                    dmem_o_comb.dat_i <= cache_ram_a_rdata;
                    dmem_o_comb.ena_i <= '1';
                else -- miss or write
                    dmem_o_comb.ena_i <= '0';
                    d_miss <= '1';
                end if;
            end if;  -- else use default values, hence X

        when fill =>
            dmem_o_comb.ena_i <= '0';
            if mem_i.ena_i = '1' then
                dmem_o_comb.dat_i <= mem_i.dat_i; -- ouch, 32-bit multiplexer!
                dmem_o_comb.ena_i <= '1';

                if dmem_r.we_o='0' and is_cacheable(dmem_r.adr_o) then -- was a read
--                    assert dmem_r.sel_o = X"F" 
--                        report "Cache write of less than a word? " & hstr(dmem_r.adr_o)
--                        severity error;
                        
                    tag_ram_b_en <= '1';
                    cache_ram_b_en <= '1';
                    tag_ram_b_we <= '1';
                    cache_ram_b_we <= '1';
                end if;
            end if;
        end case;                
        
    end process;

    dmem_o.dat_i <= dmem_o_reg.dat_i;
    dmem_o.ena_i <= dmem_o_reg.ena_i and not invalidate;

    process(state, dmem_r, d_miss)
    begin
        mem_o       <= dmem_r;
        mem_o.adr_o <= dmem_r.adr_o xor g_address_swap(dmem_r.adr_o'range);
        mem_o.ena_o <= d_miss;
    end process;
    
    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                if d_miss = '1' then
                    state <= fill;
                end if;

            when fill =>
                if mem_i.ena_i = '1' then
--                    dmem_r.ena_o <= '0';
                    state <= idle;
                end if;
            end case;

            if dmem_i.ena_o = '1' then
                dmem_o_reg.ena_i <= '0';
            elsif dmem_o_comb.ena_i = '1' then
                dmem_o_reg.dat_i <= dmem_o_comb.dat_i;
                dmem_o_reg.ena_i <= '1';
                dmem_r.ena_o <= '0';
            end if;
            
            if dmem_i.ena_o = '1' then
                dmem_r <= dmem_i;
            end if;

            if reset='1' then
                state <= idle;
                dmem_o_reg.ena_i <= '1';
            end if;
        end if;
    end process;
end arch;
