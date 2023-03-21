library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;

entity mem_bus_ram is
generic (
    g_mem_addr_width : natural := 16 -- bytes
);
port (
    clock   : std_logic;
    req     : in  t_mem_req_32;
    resp    : out t_mem_resp_32
);
end entity;

architecture quick of mem_bus_ram is

    constant c_num_words    : natural := 2 ** (g_mem_addr_width - 2);
    type t_byte is array(natural range <>) of std_logic_vector(7 downto 0);

    -- -------------------------------------------------------------------------------------------------------------- --
    -- The memory (RAM) is built from 4 individual byte-wide memories b0..b3, since some synthesis tools have         --
    -- problems with 32-bit memories that provide dedicated byte-enable signals AND/OR with multi-dimensional arrays. --
    -- -------------------------------------------------------------------------------------------------------------- --

    -- RAM - not initialized at all --
    signal mem_ram_b0 : t_byte(0 to c_num_words-1);
    signal mem_ram_b1 : t_byte(0 to c_num_words-1);
    signal mem_ram_b2 : t_byte(0 to c_num_words-1);
    signal mem_ram_b3 : t_byte(0 to c_num_words-1);

    -- read data --
    signal mem_b0_rd, mem_b1_rd, mem_b2_rd, mem_b3_rd : std_logic_vector(7 downto 0);

    -- bus signals
    signal rack, dack           : std_logic;
    signal rack_tag, dack_tag   : std_logic_vector(7 downto 0);
    signal accessing            : std_logic;

begin
    accessing <= req.request and not rack;
    mem_access: process(clock)
        variable addr : unsigned(g_mem_addr_width-1 downto 2);
    begin
        if rising_edge(clock) then
            -- this RAM style should not require "no_rw_check" attributes as the read-after-write behavior
            -- is intended to be defined implicitly via the if-WRITE-else-READ construct
            addr := req.address(addr'range);

            if accessing = '1' and req.read_writen = '0' and req.byte_en(0) = '1' then -- byte 0
                mem_ram_b0(to_integer(unsigned(addr))) <= req.data(07 downto 00);
            elsif accessing = '1' and req.read_writen = '1' then
                mem_b0_rd <= mem_ram_b0(to_integer(unsigned(addr)));
            end if;

            if accessing = '1' and req.read_writen = '0' and req.byte_en(1) = '1' then -- byte 1
                mem_ram_b1(to_integer(unsigned(addr))) <= req.data(15 downto 08);
            elsif accessing = '1' and req.read_writen = '1' then
                mem_b1_rd <= mem_ram_b1(to_integer(unsigned(addr)));
            end if;

            if accessing = '1' and req.read_writen = '0' and req.byte_en(2) = '1' then -- byte 2
                mem_ram_b2(to_integer(unsigned(addr))) <= req.data(23 downto 16);
            elsif accessing = '1' and req.read_writen = '1' then
                mem_b2_rd <= mem_ram_b2(to_integer(unsigned(addr)));
            end if;

            if accessing = '1' and req.read_writen = '0' and req.byte_en(3) = '1' then -- byte 3
                mem_ram_b3(to_integer(unsigned(addr))) <= req.data(31 downto 24);
            elsif accessing = '1' and req.read_writen = '1' then
                mem_b3_rd <= mem_ram_b3(to_integer(unsigned(addr)));
            end if;

            rack <= '0';
            dack <= '0';
            rack_tag <= X"00";
            dack_tag <= X"00";
            if accessing = '1' then
                rack <= '1';
                rack_tag <= req.tag;
                if req.read_writen = '1' then
                    dack <= '1';
                    dack_tag <= req.tag;
                end if;
            end if;
        end if;
    end process mem_access;

    -- output gate --
    resp.data     <= (mem_b3_rd & mem_b2_rd & mem_b1_rd & mem_b0_rd) when dack = '1' else (others => '0');
    resp.rack     <= rack;
    resp.rack_tag <= rack_tag;
    resp.dack_tag <= dack_tag;

end architecture;
