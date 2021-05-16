library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

-- Concept:
-- The actual commands are implemented by the application software.
-- This allows direct coupling between a file and the read/write commands that
-- the drive performs. The drawback is maybe a timing incompatibility, but it
-- is currently assumed that this is not as strict as on a 1541. So this may
-- not be an issue. The "window" through which data is passed is located in
-- external memory. The location and length are determined by the software
-- and the actual transfer to/from the CPU in the 1571/1581 is done
-- autonomously by this block:
-- 
-- 'Read' command:  (6502 <- data <- application)
-- * 1581 performs the seek (through other commands)
-- * 1581 writes the read command into the WD177x register
-- * This block generates an interrupt to the appication software
--   and sets the busy bit.
-- * Application software interprets the command, and places the
--   data to be read in a memory block and passes the pointer to
--   it to the 'DMA' registers. -> Can the pointer be fixed?!
-- * 1581 will see data appearing in the data register through the
--   data valid bit (S1). Reading the register pops the data, and
--   DMA controller places new data until all bytes have been
--   transferred.
-- * This block clears the busy bit and the command completes.
-- 
-- 'Write' commands:  (6502 -> data -> application)   
-- * 1581 writes the write command into the WD177x register
-- * Block sets the busy bit and generates the IRQ to the application
-- * Application software interprets the command, and sets the
--   memory write address in the DMA controller and starts it.
-- * The block keeps 'DRQ' (S1) asserted for as long as it accepts data
-- * When all bytes are transferred, another IRQ is generated.
-- * The application recognizes that a write has completed and
--   processes the data.
-- * Application clears the busy bit, possibly sets the status bits
--   and the command completes.
-- 
-- 'Force Interrupt' command aborts the current transfer. In case of
-- a read, the DMA controller is reset and the busy bit is cleared.
-- In case of a write, an interrupt is generated (because of the
-- command), and the DMA controller is stopped. The IRQ causes the
-- application software to know that the write has been aborted.
--
-- So, commands that transfer data TO the 1581 CPU are simply handled
-- by one call to the application software, and the logic completes it.
-- Commands that transfer data FROM the 1581 CPU are handled by two
-- calls to the application software; one to request a memory pointer
-- and one to process the data and reset the busy bit / complete the
-- command programmatically.
-- 

entity wd177x is
generic (
    g_tag       : std_logic_vector(7 downto 0) := X"03" );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    -- register interface from 6502
    addr        : in  unsigned(1 downto 0);
    wen         : in  std_logic;
    ren         : in  std_logic;
    wdata       : in  std_logic_vector(7 downto 0);
    rdata       : out std_logic_vector(7 downto 0);

    -- memory interface
    mem_req         : out t_mem_req;
    mem_resp        : in  t_mem_resp;

    -- track stepper interface (for audio samples)
    goto_track      : out unsigned(6 downto 0);
    phys_track      : in  unsigned(6 downto 0);
    step_time       : out unsigned(4 downto 0);

    -- I/O interface from application CPU
    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;
    io_irq          : out std_logic );

end entity;

architecture behavioral of wd177x is
    signal mem_rwn          : std_logic;
    signal mem_rack         : std_logic;
    signal mem_dack         : std_logic;
    signal mem_request      : std_logic := '0';

    signal step_time_i     : std_logic_vector(4 downto 0);

    signal status           : std_logic_vector(7 downto 0);
    signal track            : std_logic_vector(7 downto 0);
    signal sector           : std_logic_vector(7 downto 0);
    signal command          : std_logic_vector(7 downto 0) := X"00";
    signal disk_data        : std_logic_vector(7 downto 0) := X"00";
    signal disk_wdata_valid : std_logic;
    signal disk_rdata_valid : std_logic;

    alias st_motor_on       : std_logic is status(7);
    alias st_write_prot     : std_logic is status(6);
    alias st_rectype_spinup : std_logic is status(5);
    alias st_rec_not_found  : std_logic is status(4);
    alias st_crc_error      : std_logic is status(3);
    alias st_lost_data      : std_logic is status(2);
    alias st_data_request   : std_logic is status(1);
    alias st_busy           : std_logic is status(0);

    signal dma_mode         : std_logic_vector(1 downto 0);
    signal transfer_addr    : unsigned(23 downto 0) := (others => '0');
    signal transfer_len     : unsigned(13 downto 0) := (others => '0');

    type t_dma_state is (idle, do_read, reading, do_write, writing);
    signal dma_state        : t_dma_state;
begin
    mem_rack  <= '1' when mem_resp.rack_tag = g_tag else '0';
    mem_dack  <= '1' when mem_resp.dack_tag = g_tag else '0';
    
    mem_req.address <= "00" & transfer_addr;
    mem_req.data    <= disk_data;
    mem_req.read_writen <= mem_rwn;
    mem_req.request     <= mem_request;
    mem_req.size        <= "00"; -- one byte at a time
    mem_req.tag         <= g_tag;

    step_time  <= unsigned(step_time_i);

    with addr select rdata <= 
        status      when "00",
        track       when "01",
        sector      when "10",
        disk_data   when others;

    process(clock)
        variable v_addr : unsigned(3 downto 0);
    begin
        if rising_edge(clock) then
            if wen = '1' then
                case addr is
                when "00" =>
                    command <= wdata;
                    st_busy <= '1';
                    io_irq  <= '1';
                
                when "01" =>
                    track   <= wdata;
                
                when "10" =>
                    sector  <= wdata;
                
                when "11" =>
                    disk_data <= wdata;
                    disk_wdata_valid <= '1';
                
                when others =>
                    null;
                
                end case;
            end if;
            if ren = '1' then
                case addr is
                when "11" =>
                    disk_rdata_valid <= '0';
                    if disk_rdata_valid = '1' then
                        st_data_request <= '0';
                    end if;
                    
                when others =>
                    null; -- no other operations upon a read so far
                end case;
            end if;

            io_resp <= c_io_resp_init;
            v_addr := io_req.address(3 downto 0);
            if io_req.write = '1' then
                io_resp.ack <= '1';
                case v_addr is
                when X"4" =>
                    status <= status and not io_req.data;

                when X"5" =>
                    status <= status or io_req.data;

                when X"6" =>
                    io_irq  <= '0';

                when X"7" =>
                    dma_mode <= io_req.data(1 downto 0); -- 00 is off, 01 = read (to 1581), 10 = write (to appl)
                    
                when X"8" =>
                    transfer_addr(7 downto 0) <= unsigned(io_req.data);
                when X"9" =>
                    transfer_addr(15 downto 8) <= unsigned(io_req.data);
                when X"A" =>
                    transfer_addr(23 downto 16) <= unsigned(io_req.data);
                when X"C" =>
                    transfer_len(7 downto 0) <= unsigned(io_req.data);
                when X"D" =>
                    transfer_len(13 downto 8) <= unsigned(io_req.data(5 downto 0)); 
                when X"E" =>
                    goto_track  <= unsigned(io_req.data(goto_track'range));
                when X"F" =>
                    step_time_i  <= io_req.data(4 downto 0);

                when others =>
                    null;
                end case;
            end if;
            if io_req.read = '1' then
                io_resp.ack <= '1';
                case v_addr is
                when X"0" =>
                    io_resp.data <= command;
                
                when X"1" =>
                    io_resp.data <= track;
                
                when X"2" =>
                    io_resp.data <= sector;
                
                when X"3" =>
                    io_resp.data <= disk_data;

                when X"4"|X"5" =>
                    io_resp.data <= status;
                
                when X"7" =>
                    io_resp.data(1 downto 0) <= dma_mode;
                    disk_wdata_valid <= '0';
                
                when X"8" =>
                    io_resp.data <= std_logic_vector(transfer_addr(7 downto 0));
                when X"9" =>
                    io_resp.data <= std_logic_vector(transfer_addr(15 downto 8));
                when X"A" =>
                    io_resp.data <= std_logic_vector(transfer_addr(23 downto 16));
                when X"C" =>
                    io_resp.data <= std_logic_vector(transfer_len(7 downto 0));
                when X"D" =>
                    io_resp.data(5 downto 0) <= std_logic_vector(transfer_len(13 downto 8));
                when X"E" =>
                    io_resp.data(phys_track'range) <= std_logic_vector(phys_track);
                when X"F" =>
                    io_resp.data(4 downto 0) <= step_time_i;
                when others =>
                    null;
                end case;
            end if;                                          

            case dma_state is
            when idle =>
                if dma_mode = "01" then
                    if disk_rdata_valid = '0' then
                        dma_state <= do_read;
                    end if;
                elsif dma_mode = "10" then
                    st_data_request <= '1';
                    if disk_wdata_valid = '1' then
                        dma_state <= do_write;
                        st_data_request <= '0';
                        disk_wdata_valid <= '0';
                    end if;
                else -- no dma
                    st_data_request <= '0';
                    disk_wdata_valid <= '0';
                end if;
            
            when do_read =>
                if transfer_len = 0 then
                    st_busy <= '0';
                    dma_mode <= "00";
                    dma_state <= idle;
                else
                    mem_request <= '1';
                    mem_rwn <= '1';
                    dma_state <= reading;
                end if;
            
            when reading =>
                if mem_rack = '1' then
                    mem_request <= '0';
                end if;
                if mem_dack = '1' then
                    disk_data <= mem_resp.data;
                    disk_rdata_valid <= '1';
                    st_data_request <= '1';
                    transfer_len <= transfer_len - 1;
                    transfer_addr <= transfer_addr + 1;
                    dma_state <= idle;
                end if;
                    
            when do_write =>
                mem_request <= '1';
                mem_rwn <= '0';
                transfer_len <= transfer_len - 1;
                dma_state <= writing;

            when writing =>
                if mem_rack = '1' then
                    mem_request <= '0';
                    transfer_addr <= transfer_addr + 1;
                    if transfer_len = 0 then
                        dma_mode <= "11"; -- write complete
                        io_irq <= '1';
                    end if;
                    dma_state <= idle;
                end if;

            when others =>
                null;
            end case;                        

            if reset = '1' then
                mem_request <= '0';
                mem_rwn <= '1';
                disk_wdata_valid <= '0';
                disk_rdata_valid <= '0';
                track <= X"01";
                sector <= X"00";
                command <= X"00";
                io_irq <= '0';
                status <= X"00";
                dma_mode <= "00";
                step_time_i <= "01100";
                goto_track <= to_unsigned(0, goto_track'length);
            end if;
        end if;
    end process;
    
end architecture;
