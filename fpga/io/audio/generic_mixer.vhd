--------------------------------------------------------------------------------
-- Entity: generic_mixer
-- Date:2018-08-02  
-- Author: gideon     
--
-- Description: Audio mixer
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.audio_type_pkg.all;
use work.io_bus_pkg.all;

entity generic_mixer is
    generic (
        g_num_sources   : natural := 8 );
	port  (
        clock       : in  std_logic;
        reset       : in  std_logic;
        start       : in  std_logic;

        sys_clock   : in  std_logic;
        req         : in  t_io_req;
        resp        : out t_io_resp;

        -- audio
        inputs      : in  t_audio_array(0 to g_num_sources-1);
        out_L       : out std_logic_vector(23 downto 0);
        out_R       : out std_logic_vector(23 downto 0)                
	);
end entity;

architecture arch of generic_mixer is
    -- if 6dB per bit, then
    -- (0)1.1111111 ~ 2 = +6dB
    -- (0)1.0010000 ~ 1.13 = +1dB (0x90)
    -- (0)1.0000000 = 1 = 0 dB (0x80)
    -- (0)0.1110010 = .89 = -1dB (0x72)
    -- (0)0.1000000 = .5 = -6dB (0x40)
    -- (0)0.0100000 = .25 = -12dB
    -- (0)0.0000001 =     = -42dB
    -- conclusion: one byte volume control is enough
    -- Multiplication is such that 0dB yields the same value

    type t_state is (idle, accumulate, collect, collect2, collect3);
    signal state    : t_state;
    signal pointer  : natural range 0 to 2*g_num_sources;

    signal ram_addr : unsigned(7 downto 0);
    signal ram_rdata: std_logic_vector(7 downto 0);
    signal clear    : std_logic;
    signal a        : signed(17 downto 0);
    signal b        : signed(8 downto 0);
    signal result   : signed(31 downto 0);

    function clip(inp : signed(31 downto 0)) return std_logic_vector is
        variable result : std_logic_vector(23 downto 0);
    begin
        if inp(31 downto 24) = X"FF" or inp(31 downto 24) = X"00" then
            result := std_logic_vector(inp(24 downto 1));
        elsif inp(31) = '0' then
            result := X"7FFFFF";
        else
            result := X"800000";
        end if;
        return result;
    end function clip;
begin
    b <= '0' & signed(ram_rdata);
    ram_addr <= to_unsigned(pointer, 8);
    
    i_ram: entity work.dpram
    generic map (
        g_width_bits   => 8,
        g_depth_bits   => 8
    )
    port map(
        a_clock        => clock,
        a_address      => ram_addr,
        a_rdata        => ram_rdata,
        a_wdata        => X"00",
        a_en           => '1',
        a_we           => '0',
        
        b_clock        => sys_clock,
        b_address      => req.address(7 downto 0),
        b_rdata        => open,
        b_wdata        => req.data,
        b_en           => req.write,
        b_we           => req.write
    );
    
    resp.data <= X"00";
    resp.ack  <= req.write or req.read;
    
    process(clock)
    begin
        if rising_edge(clock) then
            clear <= '0';
            a <= inputs(pointer / 2);

            case state is
            when idle =>
                pointer <= 0;
                if start = '1' then
                    clear <= '1';
                    pointer <= 1;
                    state <= accumulate;
                end if;

            when accumulate =>
                if pointer = (2*g_num_sources)-1 then
                    state <= collect;
                else
                    pointer <= pointer + 1;
                end if;
            
            when collect =>
                state <= collect2;
            
            when collect2 =>
                out_L <= clip(result);
                state <= collect3;

            when collect3 =>
                out_R <= clip(result);
                state <= idle;
            
            end case;

            if reset = '1' then
                out_L <= (others => '0');
                out_R <= (others => '0');
                state <= idle;
            end if;
        end if;
    end process;
    
    i_muladd: entity work.mul_add
    port map (
        clock  => clock,
        clear  => clear,
        a      => a,
        b      => b,
        result => result
    );
    
end architecture;
