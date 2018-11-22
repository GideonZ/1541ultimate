--------------------------------------------------------------------------------
-- Entity: microwire_eeprom
-- Date:2018-08-13
-- Author: gideon     
--
-- Description: Emulation of ST M93C86 Microwire EEPROM.
--              The I/O interface enables the software to read/write directly to
--              the 2K embedded memory. The range is 4K. The lower 2K give access
--              to the "dirty" bit register. The upper 2K is memory.
--              This emulation supports multiple write, continuous read as well
--              as single and full erase operations. It does support the status
--              output, however, the output is not gated with the select input.
--              When used as a standalone, data_out shall be tristated when SEL
--              is 0.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;

entity microwire_eeprom is
port  (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
    
    -- microwire bus
    sel_in      : in  std_logic;
    clk_in      : in  std_logic;
    data_in     : in  std_logic;
    data_out    : out std_logic );

end entity;

architecture arch of microwire_eeprom is
    type t_state is (idle, selected, instruction, decode, collect_data, wait_deselect, execute, write2, error, fill, reading );
    signal state    : t_state;

    signal sel_c  : std_logic;
    signal sel_d  : std_logic;
    signal dat_c  : std_logic;
    signal dat_d  : std_logic;
    signal clk_c  : std_logic;
    signal clk_d  : std_logic;
    signal clk_d2 : std_logic;


    signal mem_io_rdata : std_logic_vector(7 downto 0);
    signal mem_io_en    : std_logic;
    signal mem_io_en_d  : std_logic;
    signal mem_rdata    : std_logic_vector(7 downto 0);
    signal mem_wdata    : std_logic_vector(7 downto 0);
    signal mem_en       : std_logic;
    signal mem_en_d     : std_logic := '0';
    signal mem_we       : std_logic;
    signal mem_address  : unsigned(10 downto 0);
    signal count        : integer range 0 to 15;
    signal write_enable : std_logic;
    signal instr        : std_logic_vector(11 downto 0);
    signal data_word    : std_logic_vector(15 downto 0);
    signal dirty        : std_logic;
    
begin
    mem_io_en <= io_req.address(11) and (io_req.read or io_req.write);
    
    i_mem: entity work.dpram
    generic map (
        g_width_bits   => 8,
        g_depth_bits   => 11
    )
    port map(
        a_clock        => clock,
        a_address      => io_req.address(10 downto 0),
        a_rdata        => mem_io_rdata,
        a_wdata        => io_req.data,
        a_en           => mem_io_en,
        a_we           => io_req.write,

        b_clock        => clock,
        b_address      => mem_address,
        b_rdata        => mem_rdata,
        b_wdata        => mem_wdata,
        b_en           => mem_en,
        b_we           => mem_we
    );
    
    process(clock)
    begin
        if rising_edge(clock) then
            io_resp <= c_io_resp_init;
            mem_io_en_d <= mem_io_en;
            
            if mem_io_en_d = '1' then
                io_resp.ack <= '1';
                io_resp.data <= mem_io_rdata;
            elsif io_req.read = '1' and io_req.address(11) = '0' then -- registers
                io_resp.data(0) <= dirty;
                io_resp.ack <= '1';
            elsif io_req.write = '1' and io_req.address(11) = '0' then -- registers
                dirty <= '0';
                io_resp.ack <= '1';
            end if;
            if mem_we = '1' then
                dirty <= '1';
            end if;

            sel_c  <= sel_in;
            sel_d  <= sel_c;
            dat_c  <= data_in;
            dat_d  <= dat_c;
            clk_c  <= clk_in;
            clk_d  <= clk_c;
            clk_d2 <= clk_d;
            
            mem_en <= '0';
            mem_we <= '0';
            mem_en_d <= mem_en;
            
            case state is
            when idle =>
                count <= 11;
                data_out <= '1'; -- ready
                data_word <= (others => '1');
                if sel_d = '1' then
                    state <= selected;
                end if;
            
            when selected =>
                if sel_d = '0' then
                    state <= idle;
                elsif clk_d = '1' and clk_d2 = '0' then -- rising edge
                    if dat_d = '1' then
                        state <= instruction;
                    else
                        state <= error;
                    end if;
                end if;
            
            when instruction =>
                if sel_d = '0' then
                    state <= idle;
                elsif clk_d = '1' and clk_d2 = '0' then -- rising edge
                    instr <= instr(instr'high-1 downto 0) & dat_d;
                    if count = 0 then
                        state <= decode;
                    else
                        count <= count - 1;
                    end if;
                end if;
            
            when decode =>
                count <= 15;
                if instr(11 downto 10) = "01" then -- WRITE
                    state <= collect_data;
                elsif instr(11 downto 8) = "0001" then -- WRALL
                    state <= collect_data;
                elsif instr(11 downto 10) = "10" then -- READ
                    mem_address <= unsigned(instr(9 downto 0)) & '0';
                    mem_en <= '1';
                    state <= reading;
                else
                    state <= wait_deselect;
                end if;
                
            when collect_data =>
                if sel_d = '0' then
                    state <= idle;
                elsif clk_d = '1' and clk_d2 = '0' then -- rising edge
                    data_word <= data_word(14 downto 0) & dat_d;
                    if count = 0 then
                        state <= wait_deselect;
                    else
                        count <= count - 1;
                    end if;
                end if;
            
            when wait_deselect =>
                if clk_d = '1' and clk_d2 = '0' then -- rising edge
                    state <= error;
                elsif sel_d = '0' then
                    state <= execute;
                end if;
            
            when execute =>
                data_out <= '0';
                if instr(10) = '1' then -- WRITE or ERASE single
                    mem_address <= unsigned(instr(9 downto 0)) & '0';
                    mem_wdata <= data_word(15 downto 8);
                    mem_en <= '1';
                    mem_we <= write_enable;
                    state <= write2;
                elsif instr(11 downto 8) = "0011" then -- WREN
                    write_enable <= '1';
                    state <= idle;
                elsif instr(11 downto 8) = "0000" then -- WRDIS
                    write_enable <= '0';
                    state <= idle;
                elsif instr(11 downto 8) = "0010" then -- ERAL
                    mem_address <= (others => '0');
                    state <= fill;
                elsif instr(11 downto 8) = "0001" then -- WRALL
                    mem_address <= (others => '0');
                    state <= fill;
                else
                    state <= idle;
                end if;

            when error =>
                if sel_d = '0' then
                    state <= idle;
                end if;
            
            when fill =>
                mem_en <= '1';
                mem_we <= write_enable;
                if mem_address(0) = '1' then
                    mem_wdata <= data_word(7 downto 0);
                else
                    mem_wdata <= data_word(15 downto 8);
                end if;
                if signed(mem_address) = -1 then
                    state <= idle;
                else
                    mem_address <= mem_address + 1;
                end if;

            when write2 =>
                mem_en <= '1';
                mem_we <= write_enable;
                mem_wdata <= data_word(7 downto 0);
                mem_address(0) <= '1';
                state <= idle;

            when reading =>
                if mem_en_d = '1' then
                    data_word(7 downto 0) <= mem_rdata;
                    count <= 7;
                end if;
                if sel_d = '0' then
                    state <= idle;
                elsif clk_d = '1' and clk_d2 = '0' then -- rising edge
                    data_out <= data_word(count);
                    if count = 0 then
                        mem_address <= mem_address + 1;
                        mem_en <= '1';
                    else
                        count <= count - 1;
                    end if;
                end if;

            when others =>
                null;
            end case;                                         

            if reset = '1' then
                state <= idle;
                write_enable <= '0';
                count <= 0;
                dirty <= '0';
                mem_address <= (others => '0');
            end if;
        end if;
    end process;

end architecture;
