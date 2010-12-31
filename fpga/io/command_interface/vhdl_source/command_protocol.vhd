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
    signal response_valid   : std_logic;
    signal status_valid     : std_logic;
    
    signal slot_status      : std_logic_vector(7 downto 0);
    alias  handshake_in     : std_logic_vector(3 downto 0) is slot_status(3 downto 0);
    alias  handshake_out    : std_logic_vector(3 downto 0) is slot_status(7 downto 4);
begin
    command_length <= command_pointer - c_cmd_if_command_buffer_addr;

    slot_resp.data <= slot_status when slot_req.bus_address(1 downto 0)="00" else
                      rdata; -- data from memory
    slot_resp.reg_output <= enabled when slot_req.io_address(8 downto 2) = slot_base else '0';
    slot_resp.irq <= '0';

    -- signals to RAM
    en <= enabled;
    we <= do_write;
    address <= command_pointer  when do_write='1' else
               response_pointer when slot_req.bus_address(0)='0' else
               status_pointer; 

    do_write <= '1' when slot_req.io_write='1' and slot_req.io_address = (slot_base & c_cif_slot_command) else '0';
    
    p_control: process(clock)
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
            if slot_req.io_address(8 downto 2) = slot_base then
                if slot_req.io_write='1' then
                    case slot_req.io_address(1 downto 0) is
                    when c_cif_slot_command =>
                        if command_pointer /= c_cmd_if_command_buffer_end then
                            command_pointer <= command_pointer + 1;
                        end if;
                    when c_cif_slot_control =>
                        handshake_in <= slot_req.data(3 downto 0);
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
                    handshake_out <= io_req.data(3 downto 0);
                when c_cif_io_status_length  =>
                    status_length(7 downto 0) <= unsigned(io_req.data); -- FIXME
                when c_cif_io_response_len_l =>
                    response_length(7 downto 0) <= unsigned(io_req.data);
                when c_cif_io_repsonse_len_h =>
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
                    io_resp.data(3 downto 0) <= handshake_out;
                when c_cif_io_handshake_in   =>
                    io_resp.data(3 downto 0) <= handshake_in;
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
                when c_cif_io_repsonse_len_h =>
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
                response_pointer <= c_cmd_if_response_buffer_addr;
                status_pointer   <= c_cmd_if_status_buffer_addr;
                handshake_in     <= (others => '0');
                handshake_out    <= (others => '0');
            end if;
        end if;
    end process;
end architecture;

--    constant c_cif_io_slot_base         : unsigned(3 downto 0) := X"0"; 
--    constant c_cif_io_slot_enable       : unsigned(3 downto 0) := X"1";
--    constant c_cif_io_handshake_out     : unsigned(3 downto 0) := X"2"; -- write will also cause pointers to be reset
--    constant c_cif_io_handshake_in      : unsigned(3 downto 0) := X"3";
--    constant c_cif_io_command_start     : unsigned(3 downto 0) := X"4"; -- read only; tells software where the buffers are.
--    constant c_cif_io_command_end       : unsigned(3 downto 0) := X"5";
--    constant c_cif_io_response_start    : unsigned(3 downto 0) := X"6"; -- read only; tells software where the buffers are.
--    constant c_cif_io_response_end      : unsigned(3 downto 0) := X"7";
--    constant c_cif_io_status_start      : unsigned(3 downto 0) := X"8"; -- read only; tells software where the buffers are.
--    constant c_cif_io_status_end        : unsigned(3 downto 0) := X"9";
--    constant c_cif_io_status_length     : unsigned(3 downto 0) := X"A"; -- write will reset status readout
--    constant c_cif_io_response_len_l    : unsigned(3 downto 0) := X"C"; -- write will reset response readout
--    constant c_cif_io_repsonse_len_h    : unsigned(3 downto 0) := X"D";
--    constant c_cif_io_command_len_l     : unsigned(3 downto 0) := X"E"; -- read only
--    constant c_cif_io_command_len_h     : unsigned(3 downto 0) := X"F"; 
--
--    constant c_cif_slot_control         : unsigned(1 downto 0) := "00"; -- R/W
--    constant c_cif_slot_command         : unsigned(1 downto 0) := "01"; -- WO
--    constant c_cif_slot_response        : unsigned(1 downto 0) := "10"; -- RO
--    constant c_cif_slot_status          : unsigned(1 downto 0) := "11"; -- RO
--
--    constant c_cmd_if_command_buffer_addr  : unsigned(10 downto 0) := to_unsigned(   0, 11);
--    constant c_cmd_if_response_buffer_addr : unsigned(10 downto 0) := to_unsigned( 896, 11);
--    constant c_cmd_if_status_buffer_addr   : unsigned(10 downto 0) := to_unsigned(1792, 11);
--    constant c_cmd_if_command_buffer_size  : integer := 896;
--    constant c_cmd_if_response_buffer_size : integer := 896;
--    constant c_cmd_if_status_buffer_size   : integer := 256;
--
--    constant c_cmd_if_command_buffer_end   : unsigned(10 downto 0) := c_cmd_if_command_buffer_addr + c_cmd_if_command_buffer_size;
--    constant c_cmd_if_response_buffer_end  : unsigned(10 downto 0) := c_cmd_if_response_buffer_addr + c_cmd_if_response_buffer_size;
--    constant c_cmd_if_status_buffer_end    : unsigned(10 downto 0) := c_cmd_if_status_buffer_addr + c_cmd_if_status_buffer_size;
