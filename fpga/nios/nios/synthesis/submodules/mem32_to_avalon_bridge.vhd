-------------------------------------------------------------------------------
-- Title      : mem32_to_avalon_bridge
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This module bridges the memory bus to avalon, in order to
--              attach it to the DDR2 memory controller (for the time being).
--              Note that the Ultimate2 logic actually used the burst size
--              re-ordering, which is something that the avalon memory
--              controller does not support. For this reason, this block
--              performs data word rotation.
-------------------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;


entity mem32_to_avalon_bridge is
port (
    clock               : in  std_logic;
    reset               : in  std_logic;
    
    memreq_tag          : in  std_logic_vector(7 downto 0);
    memreq_request      : in  std_logic;
    memreq_read_writen  : in  std_logic;
    memreq_address      : in  std_logic_vector(25 downto 0);
    memreq_byte_en      : in  std_logic_vector(3 downto 0);
    memreq_data         : in  std_logic_vector(31 downto 0);
    memresp_data        : out std_logic_vector(31 downto 0);
    memresp_rack        : out std_logic;
    memresp_rack_tag    : out std_logic_vector(7 downto 0);
    memresp_dack_tag    : out std_logic_vector(7 downto 0);

    avm_read            : out std_logic;
    avm_write           : out std_logic;
    avm_address         : out std_logic_vector(25 downto 0);
    avm_writedata       : out std_logic_vector(31 downto 0);
    avm_byte_enable     : out std_logic_vector(3 downto 0);
    avm_ready           : in  std_logic;
    avm_readdata        : in  std_logic_vector(31 downto 0);
    avm_readdatavalid   : in  std_logic );

end entity;

architecture rtl of mem32_to_avalon_bridge is
    signal wr   : unsigned(1 downto 0);
    signal rd   : unsigned(1 downto 0);
    type t_tag_store is array(natural range <>) of std_logic_vector(7 downto 0);
    signal tag_store    : t_tag_store(0 to 3);
    type t_offset_store is array(natural range <>) of std_logic_vector(1 downto 0);
    signal offset_store : t_offset_store(0 to 3);
begin
    memresp_rack_tag <= X"00" when avm_ready = '0' else memreq_tag;
    memresp_rack     <= avm_ready and memreq_request;

    avm_read        <= memreq_request and memreq_read_writen;
    avm_write       <= memreq_request and not memreq_read_writen;
    avm_address     <= memreq_address(25 downto 2) & "00"; 

    -- Avalon is little endian.
    -- For example, if the memory reads the bytes A0 EA 67 FE, logically, FE will be
    -- on address 3, while A0 is on address 0. The word from memory will be "FE67EAA0": Order: 3210
    -- So, when address is 0, the byte is already in the correct location.
    -- When address is 1, "EA" needs to be returned in the lower byte, so word is rotated: 0321
    -- WHen address is 2, "67" needs to be returned in the lower byte, so word is rotated: 1032
    -- ... 
    -- For write: the data is supplied in the lower byte (0). The same rotation applies:
    with memreq_address(1 downto 0) select avm_writedata <=
        memreq_data(23 downto 0) & memreq_data(31 downto 24) when "01", -- --X-
        memreq_data(15 downto 0) & memreq_data(31 downto 16) when "10", -- -X--
        memreq_data( 7 downto 0) & memreq_data(31 downto  8) when "11", -- X---
        memreq_data                                        when others; -- ---X 

    with memreq_address(1 downto 0) select avm_byte_enable <=
        memreq_byte_en(2 downto 0) & memreq_byte_en(3 downto 3) when "01",
        memreq_byte_en(1 downto 0) & memreq_byte_en(3 downto 2) when "10",
        memreq_byte_en(0 downto 0) & memreq_byte_en(3 downto 1) when "11",
        memreq_byte_en                                        when others;
        
    process(clock)
        variable v_rdoff : std_logic_vector(1 downto 0);
    begin
        if rising_edge(clock) then
            if memreq_request='1' then
                if memreq_read_writen = '1' then 
                    if avm_ready = '1' then
                        tag_store(to_integer(wr)) <= memreq_tag;
                        offset_store(to_integer(wr)) <= memreq_address(1 downto 0);
                        wr <= wr + 1;
                    end if;
                end if;
            end if;

            memresp_dack_tag <= X"00";
            if avm_readdatavalid = '1' then
                memresp_dack_tag <= tag_store(to_integer(rd)); 
                v_rdoff := offset_store(to_integer(rd));
                case v_rdoff is
                when "01" =>
                    memresp_data <= avm_readdata(7 downto 0) & avm_readdata(31 downto 8);
                when "10" =>
                    memresp_data <= avm_readdata(15 downto 0) & avm_readdata(31 downto 16);
                when "11" =>
                    memresp_data <= avm_readdata(23 downto 0) & avm_readdata(31 downto 24);
                when others => 
                    memresp_data <= avm_readdata;
                end case;                
                rd <= rd + 1;
            end if;
                
            if reset='1' then
                rd <= (others => '0');
                wr <= (others => '0');
                tag_store <= (others => (others => '0')); -- avoid using M9K for just 32 bits
                offset_store <= (others => (others => '0')); -- or even more so for just 8 bits
            end if;
        end if;
    end process;

end architecture;
