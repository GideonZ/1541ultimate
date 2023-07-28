--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2023
--
-- Description: Simple direct mapped cache controller, compatible with the
--              instruction bus of processor core.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;


entity icache is
port  (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    disable     : in  std_logic := '0';

    dmem_i      : in  t_imem_req;
    dmem_o      : out t_imem_resp;
    
    mem_o       : out t_dmem_req;
    mem_i       : in  t_dmem_resp );

end entity;

architecture arch of icache is
    constant c_cachable_area_bits : natural := 25;
    constant c_cache_size_bits    : natural := 11; -- 2**11 bytes = 2KB
--    constant c_tag_compare_width  : natural := c_cachable_area_bits - c_cache_size_bits;
    constant c_tag_size_bits      : natural := c_cache_size_bits - 2; -- 4 bytes per cache entry

    type t_tag is record
        addr_high   : std_logic_vector(c_cachable_area_bits-1 downto c_cache_size_bits);
        valid       : std_logic;
    end record;

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

    function vector_to_tag(i : std_logic_vector(c_tag_width-1 downto 0)) return t_tag is
        variable ret : t_tag;
    begin
        ret.valid := i(c_tag_width-1);
        ret.addr_high := i(c_tag_width-2 downto 0);        
        return ret;
    end function;

    function get_tag_index (addr : std_logic_vector) return std_logic_vector is
    begin
        return std_logic_vector(addr(c_tag_size_bits+1 downto 2));
    end function;
     
    function is_cacheable (addr : std_logic_vector) return boolean is
        variable v_addr : std_logic_vector(31 downto 0);
    begin
        v_addr := extend32(addr);
        return unsigned(v_addr(31 downto c_cachable_area_bits)) = 0;
    end function;

    signal tag_raddr            : std_logic_vector(c_tag_size_bits-1 downto 0);
    signal tag_rdata            : std_logic_vector(c_tag_width-1 downto 0);
    signal tag_waddr            : std_logic_vector(c_tag_size_bits-1 downto 0);
    signal tag_wdata            : std_logic_vector(c_tag_width-1 downto 0);
    signal tag_read             : std_logic;
    signal tag_write            : std_logic;

    signal cache_raddr          : std_logic_vector(c_cache_size_bits-1 downto 2);
    signal cache_rdata          : std_logic_vector(31 downto 0);
    signal cache_waddr          : std_logic_vector(c_cache_size_bits-1 downto 2);
    signal cache_wdata          : std_logic_vector(31 downto 0);
    signal cache_read           : std_logic;
    signal cache_write          : std_logic;

    signal d_tag_out            : t_tag;
    signal d_tag_in             : t_tag;
    signal d_miss               : std_logic;

    signal dmem_r               : t_imem_req;
    signal mem_r                : t_dmem_resp;

    type t_state is (idle, fill, reg);
    signal state        : t_state;
    type t_outsel is (cache, memreg);
    signal outsel   : t_outsel;
begin
    i_tag_ram: entity work.dsram
    generic map (
        g_width => c_tag_width,
        g_depth => c_tag_size_bits
    )
    port map (
        dat_o   => tag_rdata,
        adr_i   => tag_raddr,
        ena_i   => tag_read,
        dat_w_i => tag_wdata,
        adr_w_i => tag_waddr,
        wre_i   => tag_write,
        clock   => clock
    );

    d_tag_out <= vector_to_tag(tag_rdata);
    tag_wdata <= tag_to_vector(d_tag_in);

    i_cache_ram: entity work.dsram
    generic map (
        g_width => 32,
        g_depth => c_cache_size_bits-2
    )
    port map (
        dat_o   => cache_rdata,
        adr_i   => cache_raddr,
        ena_i   => cache_read,
        dat_w_i => cache_wdata,
        adr_w_i => cache_waddr,
        wre_i   => cache_write,
        clock   => clock
    );

    -- Access pattern:
    -- dmem_o.ena_o goes active. In this cycle the cache and tag rams are read
    -- In case of a hit, the dmem_i.ena_i is '1' in the next cycle, and dmem_i.dat_i = cache data.
    -- In case of a miss, the dmem_i.ena_i is '0' in the next cycle, and a memory cycle is started by
    -- asserting mem_o.ena_o to 1. The state machine switches to 'fill' and the original address is
    -- registered. In this state, the result of the memory controller is awaited. If the data of the
    -- memory controller is received, it is passed to dmem_i, and written to the cache and tag.

    -- As long as dmem_o.ena_o remains 0, the output should remain valid! For cacheable areas this
    -- could be done by reading the cache output, but for non-cachable areas this is not possible,
    -- as this would cause a miss over and over again. So a register is needed to store the
    -- result of the memory controller.
    -- As an optimization, the memory results could not be forwarded directly, but always taken
    -- from the register.

    -- The output to the requester is thus reset as soon as dmem_i.ena_o = 1 to read the
    -- blockram output. Upon a miss, output is switched to registered output until the next
    -- dmem_i.ena_o = 1. 

    -- read port
    tag_raddr   <= get_tag_index(dmem_i.adr_o);
    tag_read    <= dmem_i.ena_o;
    cache_raddr <= std_logic_vector(dmem_i.adr_o(c_cache_size_bits-1 downto 2));
    cache_read  <= dmem_i.ena_o;

    -- write port
    tag_waddr   <= get_tag_index(dmem_r.adr_o);
    d_tag_in    <= address_to_tag(dmem_r.adr_o, '1');
    tag_write   <= '1' when state = fill and mem_i.ena_i = '1' else '0';

    cache_waddr <= std_logic_vector(dmem_r.adr_o(c_cache_size_bits-1 downto 2));
    cache_wdata <= mem_i.dat_i;
    cache_write <= '1' when state = fill and mem_i.ena_i = '1' else '0';

    -- port to memory
    mem_o.adr_o <= dmem_r.adr_o;
    mem_o.ena_o <= d_miss;
    mem_o.dat_o <= (others => '0');
    mem_o.sel_o <= X"F";
    mem_o.we_o  <= '0';

    -- reply to processor
    with outsel select dmem_o.dat_i <=
        mem_r.dat_i when memreg,
        -- mem_i.dat_i when direct,
        cache_rdata when others;

    with outsel select dmem_o.ena_i <=
        mem_r.ena_i when memreg,
        -- mem_i.ena_i when direct,
        not d_miss when others;

    -- register the request
    process(clock)
    begin
        if rising_edge(clock) then
            case state is
            when idle =>
                if d_miss = '1' then
                    state <= fill;
                    outsel <= memreg;
                    -- invalidate register, start new read
                    mem_r.ena_i <= '0';
                elsif dmem_i.ena_o = '1' then
                    dmem_r <= dmem_i;
                    outsel <= cache;
                end if;
            when fill =>
                if mem_i.ena_i = '1' then
                    mem_r <= mem_i;
                    outsel <= memreg;
                    state <= idle;
                    dmem_r.ena_o <= '0';
                end if;
            when others =>
                null;
            end case;
            if reset = '1' then
                mem_r.ena_i <= '0';
                dmem_r.ena_o <= '0';
                state <= idle;
                outsel <= cache;
            end if;
        end if;
    end process;

    -- check for misses
    process(state, dmem_r, d_tag_out, disable)
    begin
        d_miss <= '0';
        if state = idle then
            if dmem_r.ena_o = '1' then -- registered (=delayed request valid)
                if (address_to_tag(dmem_r.adr_o, '1') = d_tag_out) and is_cacheable(dmem_r.adr_o) and disable = '0' then -- read hit!
                    d_miss <= '0';
                else -- miss or write
                    d_miss <= '1';
                end if;
            end if;
        end if;
    end process;


end architecture;
