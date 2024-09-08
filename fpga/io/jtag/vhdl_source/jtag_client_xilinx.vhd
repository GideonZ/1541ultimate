--------------------------------------------------------------------------------
-- Entity: jtag_client_xilinx
-- Date: 2024-07-20  
-- Author: Gideon     
--
-- Description: Client for User JTAG on 7-Series Xilinx FPGA.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity jtag_client_xilinx is
generic (
    g_sample_width  : natural := 8;
    g_write_width   : natural := 8
);
port (
    sys_clock       : in  std_logic;
    sys_reset       : in  std_logic;
    mem_req         : out t_mem_req_32;
    mem_resp        : in  t_mem_resp_32;
    io_req          : out t_io_req;
    io_resp         : in  t_io_resp;

    console_data    : in  std_logic_vector(7 downto 0) := X"00";
    console_valid   : in  std_logic := '0';
    console2_data   : in  std_logic_vector(7 downto 0) := X"00";
    console2_valid  : in  std_logic := '0';
    console2_clear  : in  std_logic := '0';
    sample_vector   : in  std_logic_vector(g_sample_width-1 downto 0) := (others => '0');
    write_vector    : out std_logic_vector(g_write_width-1 downto 0)
);
end entity;

architecture arch of jtag_client_xilinx is


    component BSCANE2
    generic (
        DISABLE_JTAG : string := "FALSE";
        JTAG_CHAIN : integer := 1
    );
    port (
        CAPTURE  : out std_ulogic := 'H';
        DRCK     : out std_ulogic := 'H';
        RESET    : out std_ulogic := 'H';
        RUNTEST  : out std_ulogic := 'L';
        SEL      : out std_ulogic := 'L';
        SHIFT    : out std_ulogic := 'L';
        TCK      : out std_ulogic := 'L';
        TDI      : out std_ulogic := 'L';
        TMS      : out std_ulogic := 'L';
        UPDATE   : out std_ulogic := 'L';
        TDO      : in  std_ulogic := 'X'
    );
    end component;
    attribute BOX_TYPE : string;
    attribute BOX_TYPE of BSCANE2 : component is "PRIMITIVE";

    signal jreset             : std_logic;
    signal jcapture           : std_logic;
    signal jshift             : std_logic;                                       -- jshift
    signal jupdate            : std_logic;
    signal jsel               : std_logic;
    signal jtck               : std_logic;                                       -- clk
    signal jtdi               : std_logic;
    signal jtdo               : std_logic;

    signal expect_sel         : std_logic := '0';
    signal ir_shift           : std_logic_vector(3 downto 0) := (others => '0'); -- ir_out
    signal ir_in              : std_logic_vector(3 downto 0);                    -- ir_in
    signal isel, dsel         : std_logic := '0';

    constant c_rom            : std_logic_vector(31 downto 0) := X"DEAD1541";
    signal read_vec           : std_logic_vector(31 downto 0);
    signal shiftreg_sample    : std_logic_vector(sample_vector'range);
    signal shiftreg_write     : std_logic_vector(write_vector'range);
    signal shiftreg_debug     : std_logic_vector(31 downto 0);
    signal debug_bits         : std_logic_vector(3 downto 0);
    signal write_vector_i     : std_logic_vector(write_vector'range) := (others => '0');

    signal bypass_reg         : std_logic;
    signal bit_count          : unsigned(4 downto 0) := (others => '0');    
    signal wbit_count         : unsigned(3 downto 0) := (others => '0');    

    signal write_fifo_full    : std_logic;
    signal write_fifo_put     : std_logic := '0';
    signal write_fifo_din     : std_logic_vector(11 downto 0);
    signal read_fifo_get      : std_logic;
    signal shiftreg_fifo      : std_logic_vector(15 downto 0);
    signal read_fifo_count    : unsigned(7 downto 0);
    signal read_fifo_data     : std_logic_vector(7 downto 0);
    signal cmd_count          : unsigned(7 downto 0) := X"00";
    signal last_command       : std_logic_vector(11 downto 0);

    -- signals that live in the avm clock domain
    -- Console
    signal console_fifo_get     : std_logic;
    signal console_fifo_data    : std_logic_vector(7 downto 0);
    signal shiftreg_console     : std_logic_vector(7 downto 0);
    signal console_fifo_count   : unsigned(10 downto 0);

    signal console2_flush       : std_logic;
    signal console2_fifo_get    : std_logic;
    signal console2_fifo_data   : std_logic_vector(7 downto 0);
    signal shiftreg_console2    : std_logic_vector(7 downto 0);
    signal console2_fifo_count  : unsigned(10 downto 0);

    -- JTAG to Avalon 
    type t_state is (idle, do_write, do_read, do_read2, do_read3, do_io_read, do_io_write);
    signal state            : t_state;
    signal incrementing     : std_logic;
    signal byte_count       : integer range 0 to 3;
    signal read_count       : unsigned(7 downto 0);
    signal avm_read_reg     : std_logic_vector(31 downto 0) := (others => '0');
    signal write_enabled    : std_logic;
    signal write_data       : std_logic_vector(31 downto 0) := (others => '0');
    signal write_be         : std_logic_vector(3 downto 0) := (others => '0');
    signal address          : unsigned(31 downto 0) := (others => '0');

    -- JTAG to Avalon FIFO
    signal avm_rfifo_put    : std_logic;
    signal avm_rfifo_full   : std_logic;
    signal avm_wfifo_get    : std_logic;
    signal avm_wfifo_valid  : std_logic;
    signal avm_read_i       : std_logic;
    signal avm_write_i      : std_logic;
    signal avm_wfifo_dout   : std_logic_vector(11 downto 0);
    signal avm_wfifo_count  : unsigned(4 downto 0);
    signal avm_exec_count   : unsigned(2 downto 0) := "000";

    -- Avalon signals
    signal avm_read             : std_logic := '0';
    signal avm_write            : std_logic := '0';
    signal avm_address          : std_logic_vector(31 downto 0);
    signal avm_writedata        : std_logic_vector(31 downto 0);
    signal avm_byteenable       : std_logic_vector(3 downto 0);
    signal avm_waitrequest      : std_logic;
    signal avm_readdata         : std_logic_vector(31 downto 0);
    signal avm_readdatavalid    : std_logic;
    signal mem_addr_jtag        : std_logic_vector(25 downto 0);

    signal io_read              : std_logic := '0';
    signal io_write             : std_logic := '0';
    signal io_wdata             : std_logic_vector(7 downto 0);
begin
    i_jtag: BSCANE2
    generic map (
        DISABLE_JTAG => "FALSE",
        JTAG_CHAIN   => 4
    )
    port map (
        CAPTURE  => jcapture,
        DRCK     => open,
        RESET    => jreset,
        RUNTEST  => open,
        SEL      => jsel,
        SHIFT    => jshift,
        TCK      => jtck,
        TDI      => jtdi,
        TMS      => open,
        UPDATE   => jupdate,
        TDO      => jtdo
    );

    -- Recreate IR. Since there is only the data register, let
    -- the first bit decide whether it is (local) instruction or data.
    -- This state machine generates multiple selects, based on IR.
    process(jtck)
    begin
        if rising_edge(jtck) then
            if jsel = '1' then
                if jreset = '1' then
                    ir_in <= (others => '0');
                    expect_sel <= '0';
                    isel <= '0';
                    dsel <= '0';
                elsif jcapture = '1' then
                    ir_shift <= ir_in;
                    expect_sel <= '1';
                elsif jshift = '1' then
                    expect_sel <= '0';
                    if expect_sel = '1' then
                        isel <= jtdi;
                        dsel <= not jtdi;
                    else -- not first bit
                        if isel = '1' then
                            ir_shift <= jtdi & ir_shift(ir_shift'high downto 1);
                        else
                            -- shift data register, but we'll do that later by simply looking at ir_in and dsel.
                        end if;
                    end if;
                elsif jupdate = '1' then
                    if isel = '1' then
                        ir_in <= ir_shift;
                    end if;
                    isel <= '0';
                    dsel <= '0';
                end if;
            end if;
        end if;
    end process;

    process(jtck)
    begin
        if rising_edge(jtck) then
            console_fifo_get <= '0';
            console2_fifo_get <= '0';
            read_fifo_get <= '0';
            write_fifo_put <= '0';
            if write_fifo_put = '1' then
                last_command <= write_fifo_din;
            end if;

            -- WRITE
            if jshift = '1' and dsel = '1' then
                bypass_reg <= jtdi;
                shiftreg_sample <= jtdi & shiftreg_sample(shiftreg_sample'high downto 1);
                shiftreg_write <= jtdi & shiftreg_write(shiftreg_write'high downto 1);
                shiftreg_debug <= jtdi & shiftreg_debug(shiftreg_debug'high downto 1);
                wbit_count <= wbit_count + 1;
                if ir_in = X"5" then
                    shiftreg_fifo <= jtdi & shiftreg_fifo(shiftreg_fifo'high downto 1);
                    if wbit_count(3 downto 0) = "1111" then
                        write_fifo_put <= '1';
                        cmd_count <= cmd_count + 1;
                    end if;
                elsif ir_in = X"6" then
                    shiftreg_fifo <= jtdi & shiftreg_fifo(shiftreg_fifo'high downto 1);
                    if wbit_count(2 downto 0) = "111" then
                        write_fifo_put <= '1';
                        cmd_count <= cmd_count + 1;
                    end if;
                end if;
            end if;

            -- READ
            if jshift = '1' and dsel = '1' then
                bit_count <= bit_count + 1;
                if ir_in = X"4" then
                    if bit_count(2 downto 0) = "111" then
                        shiftreg_fifo <= X"00" & read_fifo_data;
                        read_fifo_get <= not jtdi;
                    else
                        shiftreg_fifo <= '0' & shiftreg_fifo(shiftreg_fifo'high downto 1);
                    end if;
                elsif ir_in = X"A" then
                    if bit_count(2 downto 0) = "111" then
                        shiftreg_console <= console_fifo_data;
                        console_fifo_get <= not jtdi;
                    else
                        shiftreg_console <= '0' & shiftreg_console(shiftreg_console'high downto 1);
                    end if;
                elsif ir_in = X"B" then
                    if bit_count(2 downto 0) = "111" then
                        shiftreg_console2 <= console2_fifo_data;
                        console2_fifo_get <= not jtdi;
                    else
                        shiftreg_console2 <= '0' & shiftreg_console2(shiftreg_console2'high downto 1);
                    end if;
                end if;
            end if;

            -- CAPTURE. Note that this will also happen when IR is accessed, as it is before
            -- knowing the start bit
            if jsel = '1' and jcapture = '1' then -- Capture state
                shiftreg_write  <= write_vector_i;
                shiftreg_sample <= sample_vector;
                bit_count <= (others => '0');
                wbit_count <= (others => '0');
                shiftreg_fifo <= X"00" & std_logic_vector(read_fifo_count);
                if console_fifo_count >= 256 then
                    shiftreg_console <= X"FF";
                else
                    shiftreg_console <= std_logic_vector(console_fifo_count(7 downto 0));
                end if;
                if console2_fifo_count >= 256 then
                    shiftreg_console2 <= X"FF";
                else
                    shiftreg_console2 <= std_logic_vector(console2_fifo_count(7 downto 0));
                end if;
                shiftreg_debug <= std_logic_vector(read_fifo_count) & debug_bits & last_command & std_logic_vector(cmd_count);
            end if;
        
            -- UPDATE
            if jupdate = '1' and dsel = '1' then
                case ir_in is
                when X"2" =>
                    write_vector_i <= shiftreg_write;
                when others =>
                    null;
                end case;
            end if;
        end if;
    end process;
    write_vector <= write_vector_i;

    process(isel, ir_shift, ir_in, bit_count, shiftreg_sample, shiftreg_write, bypass_reg, shiftreg_fifo,
            shiftreg_debug, shiftreg_console, shiftreg_console2)
    begin
        if isel = '1' then
            jtdo <= ir_shift(0);
        else
            case ir_in is
            when X"0" =>
                jtdo <= c_rom(to_integer(bit_count));
            when X"1" =>
                jtdo <= shiftreg_sample(0);
            when X"2" =>
                jtdo <= shiftreg_write(0);
            when X"3" =>
                jtdo <= shiftreg_debug(0);
            when X"4" =>
                jtdo <= shiftreg_fifo(0);
            when X"A" =>
                jtdo <= shiftreg_console(0);
            when X"B" =>
                jtdo <= shiftreg_console2(0);
            when others =>
                jtdo <= bypass_reg;
            end case;
        end if;
    end process;
    
    -- Avalon JTAG commands
    -- BA98.7
    -- E000   => write data (byte is data, E = byte enable)
    -- 0001 0 => Start Write Non Incrementing  (only upper bit of byte used)
    -- 0001 1 => Start Write incrementing  (only upper bit of byte used)
    -- 0010   => Do Read Non-Incrementing (byte is length)
    -- 0011   => Do Read incrementing (byte is length)
    -- 01yy   => Set address (bytes is address data) yy = index of address byte
    -- 110n   => Do IO Read (n = increment)
    -- 111n   => Do IO Write (n = increment)
    -- 
    -- When instruction is '6', write data can be passed as bytes, with byte enable
    -- set automatically as a shorthand notiation. When instruction is X"5", 16 bits
    -- have to be shifted in for one particular command.
    -- For block reads; issue a '011' command first, together with the length.
    -- Then read out the data through instruction '4'. Important is to keep TDI to zero,
    -- for the fifo to be read; otherwise you'll be reading the same byte over and over.

    write_fifo_din <= ("1000" & shiftreg_fifo(15 downto 8)) when ir_in = X"6" else
                      (shiftreg_fifo(11 downto 0));
                      
    i_write_fifo: entity work.async_fifo_ft
    generic map(
        g_data_width => 12,
        g_depth_bits => 4
    )
    port map (
        wr_clock     => jtck,
        wr_reset     => '0',
        wr_en        => write_fifo_put,
        wr_din       => write_fifo_din,
        wr_full      => write_fifo_full,
        rd_clock     => sys_clock,
        rd_reset     => sys_reset,
        rd_next      => avm_wfifo_get,
        rd_dout      => avm_wfifo_dout,
        rd_valid     => avm_wfifo_valid,
        rd_count     => avm_wfifo_count
    );
    
    i_read_fifo: entity work.async_fifo_ft
    generic map(
        g_data_width => 8,
        g_depth_bits => 7 )
    port map(
        wr_clock     => sys_clock,
        wr_reset     => sys_reset,
        wr_en        => avm_rfifo_put,
        wr_din       => avm_read_reg(7 downto 0),
        wr_full      => avm_rfifo_full,
        rd_clock     => jtck,
        rd_reset     => '0',
        rd_next      => read_fifo_get,
        rd_dout      => read_fifo_data,
        rd_valid     => open,
        rd_count     => read_fifo_count
    );

    i_console_fifo: entity work.async_fifo_ft
    generic map(
        g_data_width => 8,
        g_depth_bits => 10 )
    port map(
        wr_clock     => sys_clock,
        wr_reset     => sys_reset,
        wr_en        => console_valid,
        wr_din       => console_data,
        wr_full      => open,

        rd_clock     => jtck,
        rd_reset     => '0',
        rd_next      => console_fifo_get,
        rd_dout      => console_fifo_data,
        rd_valid     => open,
        rd_count     => console_fifo_count
    );

    console2_flush <= sys_reset or console2_clear;
    i_console2_fifo: entity work.async_fifo_ft
    generic map(
        g_data_width => 8,
        g_depth_bits => 10 )
    port map(
        wr_clock     => sys_clock,
        wr_reset     => console2_flush,
        wr_en        => console2_valid,
        wr_din       => console2_data,
        wr_full      => open,

        rd_clock     => jtck,
        rd_reset     => '0',
        rd_next      => console2_fifo_get,
        rd_dout      => console2_fifo_data,
        rd_valid     => open,
        rd_count     => console2_fifo_count
    );

    process(sys_clock)
        variable v_cmd  : std_logic_vector(3 downto 0);
    begin
        if rising_edge(sys_clock) then
            io_read <= '0';
            io_write <= '0';
            case state is
            when idle =>
                avm_rfifo_put <= '0';
                if avm_wfifo_valid = '1' then -- command?
                    avm_exec_count <= avm_exec_count + 1;
                    v_cmd := avm_wfifo_dout(11 downto 8);
                    case v_cmd is
                    when "0000" | "1000" =>
                        if write_enabled = '1' then
                            write_be(3) <= avm_wfifo_dout(11);
                            write_data(31 downto 24) <= avm_wfifo_dout(7 downto 0);
                            write_be(2 downto 0) <= write_be(3 downto 1);
                            write_data(23 downto 00) <= write_data(31 downto 8);
                            if byte_count = 3 then
                                avm_write_i <= '1';
                                state <= do_write;
                                byte_count <= 0;
                            else
                                byte_count <= byte_count + 1;
                            end if;
                        end if;
                    when "0001" =>
                        byte_count <= 0;
                        write_enabled <= '1';
                        incrementing <= avm_wfifo_dout(7);
                    when "0010" | "0011" =>
                        write_enabled <= '0';
                        read_count <= unsigned(avm_wfifo_dout(7 downto 0));
                        state <= do_read;
                        incrementing <= v_cmd(0);
                    when "0100" =>
                        write_enabled <= '0';
                        address(7 downto 0) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when "0101" =>
                        write_enabled <= '0';
                        address(15 downto 8) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when "0110" =>
                        write_enabled <= '0';
                        address(23 downto 16) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when "0111" =>
                        write_enabled <= '0';
                        address(31 downto 24) <= unsigned(avm_wfifo_dout(7 downto 0)); 
                    when "1100" | "1101" => -- IO read
                        io_read <= '1';
                        state <= do_io_read;
                        incrementing <= v_cmd(0);
                    when "1110" | "1111" => -- IO write
                        io_wdata <= avm_wfifo_dout(7 downto 0);
                        io_write <= '1';
                        state <= do_io_write;
                        incrementing <= v_cmd(0);
                    when others =>
                        null;
                    end case;
                end if;

            when do_io_read =>
                if io_resp.ack = '1' then
                    avm_read_reg(7 downto 0) <= io_resp.data;
                    avm_rfifo_put <= '1';
                    io_read <= '0';
                    io_write <= '0';
                    if incrementing = '1' then
                        address <= address + 1;
                    end if;
                    state <= idle;
                end if;

            when do_io_write =>
                if io_resp.ack = '1' then
                    io_read <= '0';
                    io_write <= '0';
                    if incrementing = '1' then
                        address <= address + 1;
                    end if;
                    state <= idle;
                end if;

            when do_write =>
                if avm_waitrequest = '0' then
                    avm_write_i <= '0';
                    state <= idle;
                    if incrementing = '1' then
                        address(31 downto 2) <= address(31 downto 2) + 1;
                    end if;
                end if;

            when do_read =>
                write_be <= "1111";
                avm_read_i <= '1';
                state <= do_read2;

            when do_read2 =>
                if avm_waitrequest = '0' then
                    avm_read_i <= '0';
                end if;
                if avm_readdatavalid = '1' then
                    avm_read_reg <= avm_readdata;
                    byte_count <= 0;
                    avm_rfifo_put <= '1';
                    state <= do_read3;
                end if;

            when do_read3 =>
                if avm_rfifo_full = '0' then
                    if byte_count = 3 then
                        byte_count <= 0;
                        avm_rfifo_put <= '0';
                        read_count <= read_count - 1;
                        if read_count = 0 then
                            state <= idle;
                        else
                            state <= do_read;
                            if incrementing = '1' then
                                address(31 downto 2) <= address(31 downto 2) + 1;
                            end if;
                        end if;
                    else
                        byte_count <= byte_count + 1;
                        avm_read_reg(23 downto 0) <= avm_read_reg(31 downto 8);
                    end if;
                end if;
            when others =>
                null;
            end case;

            if sys_reset = '1' then
                state <= idle;
                incrementing <= '0';
                byte_count <= 0;
                read_count <= (others => '0');
                write_enabled <= '0';
                write_be <= "0000";
                avm_read_i <= '0';
                avm_write_i <= '0';
                avm_rfifo_put <= '0';
                avm_exec_count <= "000";
            end if;                        
                            
        end if;
    end process;

    with state select debug_bits(2 downto 0) <=
        "000" when idle,
        "001" when do_write,
        "010" when do_read,
        "011" when do_read2,
        "100" when do_read3,
        "101" when do_io_read,
        "110" when do_io_write,
        "111" when others;

    debug_bits(3) <= avm_read_i or avm_write_i or io_read or io_write;
    
    avm_read      <= avm_read_i;
    avm_write     <= avm_write_i;
    avm_wfifo_get <= '1' when avm_wfifo_valid = '1' and state = idle else '0';
    
    avm_address <= std_logic_vector(address(31 downto 2) & "00");
    avm_byteenable <= write_be;
    avm_writedata <= write_data;
    
    --- Convert ---
    i_avalon_to_mem32: entity work.avalon_to_mem32_bridge
        generic map (
            g_tag => X"3F"
        )
        port map (
            clock               => sys_clock,
            reset               => sys_reset,
            avs_read            => avm_read,
            avs_write           => avm_write,
            avs_address         => avm_address(25 downto 0),
            avs_writedata       => avm_writedata,
            avs_byteenable      => avm_byteenable,
            avs_waitrequest     => avm_waitrequest,
            avs_readdata        => avm_readdata,
            avs_readdatavalid   => avm_readdatavalid,

            mem_req_read_writen => mem_req.read_writen,
            mem_req_request     => mem_req.request,
            mem_req_address     => mem_addr_jtag,
            mem_req_wdata       => mem_req.data,
            mem_req_byte_en     => mem_req.byte_en,
            mem_req_tag         => mem_req.tag,

            mem_resp_rack_tag   => mem_resp.rack_tag,
            mem_resp_dack_tag   => mem_resp.dack_tag,
            mem_resp_data       => mem_resp.data
        );
    mem_req.address <= unsigned(mem_addr_jtag(25 downto 0));

    io_req.address <= address(io_req.address'range);
    io_req.read   <= io_read;
    io_req.write <= io_write;
    io_req.data <= io_wdata;

end architecture;
