library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    
library work;
    use work.io_bus_pkg.all;
    use work.slot_bus_pkg.all;
    use work.command_if_pkg.all;
    
entity command_protocol is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    -- io interface for local cpu
    io_req          : in  t_io_req; -- we get a 2K range
    io_resp         : out t_io_resp;

    -- C64 side interface
    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp;

    -- block memory
    address         : out unsigned(10 downto 0);
    rdata           : in  std_logic_vector(7 downto 0);
    wdata           : out std_logic_vector(7 downto 0);
    en              : out std_logic;
    we              : out std_logic );

end entity;

-- How does the protocol work?

-- The Ultimate software initializes the command and response buffers that the C64 can write to.
-- Each buffer has a start address, an end address, and a current read/write pointer as seen
-- from the C64 side. 
-- The C64 can write into the command buffer and set a handshake flag. The 1541U software
-- triggers on the flag, and reads the write pointer, copies the command, and stores the
-- result and status in their respective buffers, and sets the respective handshake flags back
-- to the C64. The buffers on the ultimate side are direct mapped; on the C64 side, each
-- buffer has its own data register. One bidirectional register (on the C64 side) acts as
-- handshake register. Command queueing is not supported.

-- Protocol:
-- There are 4 states:
-- 00: Ultimate is ready and waiting for new command
-- 01: C64 has written data into the Ultimate command buffer, and the Ultimate is processing it
-- 11: Ultimate has processed the command and replied with data/status. This is not the last data.
-- 10: Ultimate has processed the command and replied with data/status. This is the last data.

-- The status register (seen by the C64) holds the following bits:
-- Bit 7:    Response data available
-- Bit 6:    Status data available
-- Bit 5..4: State
-- Bit 3:    Error flag  (write 1 to clear)
-- Bit 2:    Abort flag set (cleared by Ultimate software)
-- Bit 1:    Data accepted bit set (cleared by Ultimate software)
-- Bit 0:    New command (set when new command is written, should NOT be used by C64)


architecture gideon of command_protocol is
    signal enabled          : std_logic;
    signal slot_base        : unsigned(6 downto 0);
    signal do_write         : std_logic;
    
    signal command_pointer  : unsigned(10 downto 0);
    signal response_pointer : unsigned(10 downto 0);
    signal status_pointer   : unsigned(10 downto 0);
    signal command_length   : unsigned(10 downto 0);
    signal response_length  : unsigned(10 downto 0);
    signal status_length    : unsigned(10 downto 0);
--    signal response_valid   : std_logic;
--    signal status_valid     : std_logic;
    signal rdata_resp       : std_logic_vector(7 downto 0);
    signal rdata_stat       : std_logic_vector(7 downto 0);
    
    signal slot_status      : std_logic_vector(7 downto 0);
    alias  response_valid   : std_logic                    is slot_status(7);
    alias  status_valid     : std_logic                    is slot_status(6);
    alias  state            : std_logic_vector(1 downto 0) is slot_status(5 downto 4);
    alias  error_busy       : std_logic                    is slot_status(3);
    alias  handshake_in     : std_logic_vector(2 downto 0) is slot_status(2 downto 0);
begin
--    assert false report integer'image(to_integer(c_cmd_if_command_buffer_end))  severity warning;
--    assert false report integer'image(to_integer(c_cmd_if_response_buffer_end)) severity warning;
--    assert false report integer'image(to_integer(c_cmd_if_status_buffer_end))   severity warning;
--
    command_length <= command_pointer - c_cmd_if_command_buffer_addr;

    with slot_req.bus_address(1 downto 0) select slot_resp.data <=
        slot_status when c_cif_slot_control,
        X"C9"       when c_cif_slot_command,
        rdata_resp  when c_cif_slot_response, 
        rdata_stat  when others;   
        
    rdata_resp <= rdata when response_valid='1' else X"00";
    rdata_stat <= rdata when status_valid='1' else X"00";

    slot_resp.reg_output <= enabled when slot_req.bus_address(8 downto 2) = slot_base else '0';
    slot_resp.irq <= '0';

    -- signals to RAM
    en <= enabled;
    we <= do_write;
    wdata <= slot_req.data;
    address <= command_pointer  when do_write='1' else
               response_pointer when slot_req.bus_address(0)='0' else
               status_pointer; 

    do_write <= '1' when slot_req.io_write='1' and slot_req.io_address(8 downto 0) = (slot_base & c_cif_slot_command) else '0';
    
    p_control: process(clock)
        procedure reset_response is
        begin
            response_pointer <= c_cmd_if_response_buffer_addr;
            status_pointer   <= c_cmd_if_status_buffer_addr;
            response_length  <= (others => '0');
            status_length    <= (others => '0');
        end procedure;
    begin
        if rising_edge(clock) then
            if (response_pointer - c_cmd_if_response_buffer_addr) < response_length then
                response_valid <= '1';
            else
                response_valid <= '0';
            end if;
            if (status_pointer - c_cmd_if_status_buffer_addr) < status_length then
                status_valid <= '1';
            else
                status_valid <= '0';
            end if;
            if (slot_req.io_address(8 downto 2) = slot_base) and (enabled = '1') then
                if slot_req.io_write='1' then
                    case slot_req.io_address(1 downto 0) is
                    when c_cif_slot_command =>
                        if command_pointer /= c_cmd_if_command_buffer_end then
                            command_pointer <= command_pointer + 1;
                        end if;
                    when c_cif_slot_control =>
                        if slot_req.data(3)='1' then
                            error_busy <= '0';
                        end if;
                        if slot_req.data(0)='1' then
                            if state = "00" then                            
                                reset_response;
                                state <= "01";
                                handshake_in(0) <= '1';
                            else
                                error_busy <= '1';
                            end if;
                        end if;
                        if slot_req.data(1)='1' and state(1) = '1' then -- data accept
                            handshake_in(1) <= '1'; -- data accepted, only ultimate can clear it
                            reset_response;
                            state(1) <= '0'; -- either goes to idle, or back to wait for software
                        end if;
                        if slot_req.data(2)='1' then
                            handshake_in(2) <= '1'; -- abort, only ultimate can clear it.
                            reset_response;
                        end if;
                    when others =>
                        null;
                    end case;
                elsif slot_req.io_read='1' then
                    case slot_req.io_address(1 downto 0) is
                    when c_cif_slot_response =>
                        if response_pointer /= c_cmd_if_response_buffer_end then
                            response_pointer <= response_pointer + 1;
                        end if;
                    when c_cif_slot_status =>
                        if status_pointer /= c_cmd_if_status_buffer_end then
                            status_pointer <= status_pointer + 1;
                        end if;
                    when others =>
                        null;
                    end case;
                end if;
            end if;
   
            io_resp <= c_io_resp_init;
            if io_req.write='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_cif_io_slot_base      =>
                    slot_base <= unsigned(io_req.data(slot_base'range));
                when c_cif_io_slot_enable    =>
                    enabled <= io_req.data(0);
                when c_cif_io_handshake_out  =>
                    if io_req.data(0)='1' then -- reset
                        handshake_in(0) <= '0';
                        command_pointer <= c_cmd_if_command_buffer_addr;
                    end if;
                    if io_req.data(1)='1' then -- data seen
                        handshake_in(1) <= '0';
                    end if;
                    if io_req.data(2)='1' then -- abort bit
                        handshake_in(2) <= '0';
                    end if;
                    if io_req.data(4)='1' then -- validate data
                        state(1) <= '1';
                        state(0) <= io_req.data(5); -- more bit
                    end if;                        
                    if io_req.data(7)='1' then
                        reset_response;
                        state <= "00";
                    end if;                        
                when c_cif_io_status_length  =>
                    status_pointer <= c_cmd_if_status_buffer_addr;
                    status_length(7 downto 0) <= unsigned(io_req.data); -- FIXME
                when c_cif_io_response_len_l =>
                    response_pointer <= c_cmd_if_response_buffer_addr;
                    response_length(7 downto 0) <= unsigned(io_req.data);
                when c_cif_io_response_len_h =>
                    response_length(10 downto 8) <= unsigned(io_req.data(2 downto 0));
                when others =>
                    null;
                end case;
            elsif io_req.read='1' then
                io_resp.ack <= '1';
                case io_req.address(3 downto 0) is
                when c_cif_io_slot_base      =>
                    io_resp.data(slot_base'range) <= std_logic_vector(slot_base);
                when c_cif_io_slot_enable    =>
                    io_resp.data(0) <= enabled;
                when c_cif_io_handshake_out  =>
                    io_resp.data(5 downto 4) <= state;
                when c_cif_io_handshake_in   =>
                    io_resp.data <= slot_status;
                when c_cif_io_command_start  =>
                    io_resp.data <= std_logic_vector(c_cmd_if_command_buffer_addr(10 downto 3));
                when c_cif_io_command_end    =>
                    io_resp.data <= std_logic_vector(c_cmd_if_command_buffer_end(10 downto 3));
                when c_cif_io_response_start =>
                    io_resp.data <= std_logic_vector(c_cmd_if_response_buffer_addr(10 downto 3));
                when c_cif_io_response_end   =>
                    io_resp.data <= std_logic_vector(c_cmd_if_response_buffer_end(10 downto 3));
                when c_cif_io_status_start   =>
                    io_resp.data <= std_logic_vector(c_cmd_if_status_buffer_addr(10 downto 3));
                when c_cif_io_status_end     =>
                    io_resp.data <= std_logic_vector(c_cmd_if_status_buffer_end(10 downto 3));
                when c_cif_io_status_length  =>
                    io_resp.data <= std_logic_vector(status_length(7 downto 0)); -- fixme
                when c_cif_io_response_len_l =>
                    io_resp.data <= std_logic_vector(response_length(7 downto 0));
                when c_cif_io_response_len_h =>
                    io_resp.data(2 downto 0) <= std_logic_vector(response_length(10 downto 8));
                when c_cif_io_command_len_l  =>
                    io_resp.data <= std_logic_vector(command_length(7 downto 0));
                when c_cif_io_command_len_h  =>
                    io_resp.data(2 downto 0) <= std_logic_vector(command_length(10 downto 8));
                when others =>
                    null;
                end case;
            end if;                     

            if reset='1' then
                command_pointer  <= c_cmd_if_command_buffer_addr;
                reset_response;
                handshake_in     <= "000";
                state            <= "00";
                enabled          <= '0';
                error_busy       <= '0';
                slot_base        <= (others => '0');
            end if;
        end if;
    end process;
end architecture;
