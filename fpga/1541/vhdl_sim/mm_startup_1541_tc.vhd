library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_bfm_pkg.all;
use work.tl_flat_memory_model_pkg.all;
use work.c1541_pkg.all;
use work.tl_string_util_pkg.all;
use work.iec_bus_bfm_pkg.all;

entity mm_startup_1541_tc is

end;

architecture tc of mm_startup_1541_tc is
    signal irq      : std_logic;

    constant c_wd177x_command       : unsigned(15 downto 0) := X"1800";
    constant c_wd177x_track         : unsigned(15 downto 0) := X"1801";
    constant c_wd177x_sector        : unsigned(15 downto 0) := X"1802";
    constant c_wd177x_datareg       : unsigned(15 downto 0) := X"1803";
    constant c_wd177x_status_clear  : unsigned(15 downto 0) := X"1804";
    constant c_wd177x_status_set    : unsigned(15 downto 0) := X"1805";
    constant c_wd177x_irq_ack       : unsigned(15 downto 0) := X"1806";
    constant c_wd177x_dma_mode      : unsigned(15 downto 0) := X"1807";
    constant c_wd177x_dma_addr      : unsigned(15 downto 0) := X"1808"; -- DWORD
    constant c_wd177x_dma_len       : unsigned(15 downto 0) := X"180C"; -- WORD

    constant c_param_ram            : unsigned(15 downto 0) := X"1000";
begin

    i_harness: entity work.harness_mm
    port map (
        io_irq => irq
    );

    process
        variable io : p_io_bus_bfm_object;
        variable dram : h_mem_object;
        variable bfm : p_iec_bus_bfm_object;
        variable msg : t_iec_message;
        variable value : unsigned(31 downto 0);
        variable params : std_logic_vector(31 downto 0);
        variable rotation_speed : natural;
        variable bit_time : natural;
    begin
        wait for 1 ns;
        bind_io_bus_bfm("io_bfm", io);
        bind_mem_model("dram", dram);
        bind_iec_bus_bfm("iec_bfm", bfm);

        load_memory("../../../roms/1541.bin", dram, X"00008000");
        load_memory("../../../roms/1541.bin", dram, X"0000C000");
        -- load_memory("../../../roms/sounds.bin", dram, X"00000000");
        load_memory("../../../disks/cpm.g71", dram, X"00100000" ); -- 1 MB offset

        wait for 20 us;
        io_write(io, c_drvreg_drivetype, X"00"); -- switch to 1541 mode
        io_write(io, c_drvreg_power, X"01");
        wait for 20 us;
        io_write(io, c_drvreg_reset, X"00");

        rotation_speed := (31250000 / 20);
        for i in 0 to 34 loop
            value := unsigned(read_memory_32(dram, std_logic_vector(to_unsigned(16#100000# + 12 + i*8, 32))));
            value := value + X"00100000";
            params(15 downto 0) := read_memory_16(dram, std_logic_vector(value));
            value := value + 2;
            bit_time := rotation_speed / to_integer(unsigned(params(15 downto 0)));
            params(31 downto 16) := std_logic_vector(to_unsigned(bit_time, 16));
            report "Track " & integer'image(i+1) & ": " & hstr(value) & " - " & hstr(params);
            io_write_32(io, c_param_ram + 16*i, std_logic_vector(value) );
            io_write_32(io, c_param_ram + 16*i + 4, params );
            io_write_32(io, c_param_ram + 16*i + 12, params );
        end loop;

        wait for 1800 ms;
        io_write(io, c_drvreg_inserted, X"01");

--        iec_drf(bfm);
        iec_send_atn(bfm, X"48"); -- Drive 8, Talk, I will listen
        iec_send_atn(bfm, X"6F"); -- Open channel 15
        iec_turnaround(bfm);      -- start to listen
        iec_get_message(bfm, msg);
        iec_print_message(msg);
        iec_send_atn(bfm, X"5F", true); -- Drives, Untalk!
        wait for 10 ms;
        
        wait;                
        
    end process;

--  Type  Command            7   6   5   4   3   2   1   0
--  -------------------------------------------------------
--   I    Restore            0   0   0   0   h   v   r1  r0
--   I    Seek               0   0   0   1   h   v   r1  r0
--   I    Step               0   0   1   u   h   v   r1  r0
--   I    Step in            0   1   0   u   h   v   r1  r0
--   I    Step out           0   1   1   u   h   v   r1  r0
--   II   Read sector        1   0   0   m  h/s  e  0/c  0
--   II   Write sector       1   0   1   m  h/s  e  p/c  a
--   III  Read address       1   1   0   0  h/0  e   0   0
--   III  Read track         1   1   1   0  h/0  e   0   0
--   III  Write track        1   1   1   1  h/0  e  p/0  0
--   IV   Force interrupt    1   1   0   1   i3  i2  i1  i0
    
    process
        variable io     : p_io_bus_bfm_object;
        variable dram   : h_mem_object;
        variable cmd    : std_logic_vector(7 downto 0);
        variable byte   : std_logic_vector(7 downto 0);
        variable track  : natural := 0;
        variable sector : natural := 0;
        variable dir    : std_logic := '1';
        variable side   : std_logic := '0';

        procedure do_step(update : std_logic) is
        begin
            if dir = '0' then
                if track < 80 then
                    track := track + 1;
                end if;
            else
                if track > 0 then
                    track := track - 1;
                end if;
            end if;
            if update = '1' then
                io_read(io, c_wd177x_track, byte);
                if dir = '0' then
                    byte := std_logic_vector(unsigned(byte) + 1);
                else
                    byte := std_logic_vector(unsigned(byte) - 1);
                end if;
                io_write(io, c_wd177x_track, byte);
            end if;
        end procedure;
    begin
        wait for 1 ns;
        bind_io_bus_bfm("io_bfm", io);
        bind_mem_model("dram", dram);

        while true loop
            wait until irq = '1';
            io_read(io, c_wd177x_command, cmd);
            report "Command: " & hstr(cmd);
            wait for 50 us;
            if cmd(7 downto 4) = "0000" then
                report "WD1770 Command: Restore";
                io_write(io, c_wd177x_track, X"00"); -- set track to zero
                track := 0;
                -- no data transfer
                io_write(io, c_wd177x_status_clear, X"01");
            elsif cmd(7 downto 4) = "0001" then
                io_read(io, c_wd177x_datareg, byte);
                report "WD1770 Command: Seek: Track = " & integer'image(to_integer(unsigned(byte)));
                io_write(io, c_wd177x_track, byte);
                track := to_integer(unsigned(byte));
                -- no data transfer
                io_write(io, c_wd177x_status_clear, X"01");
            elsif cmd(7 downto 5) = "001" then
                report "WD1770 Command: Step.";
                do_step(cmd(4));
                io_write(io, c_wd177x_status_clear, X"01");
            elsif cmd(7 downto 5) = "010" then 
                report "WD1770 Command: Step In.";
                dir := '1';
                do_step(cmd(4));
                io_write(io, c_wd177x_status_clear, X"01");
            elsif cmd(7 downto 5) = "011" then 
                report "WD1770 Command: Step Out.";
                dir := '0';
                do_step(cmd(4));
                io_write(io, c_wd177x_status_clear, X"01");
            elsif cmd(7 downto 5) = "100" then
                io_read(io, c_wd177x_sector, byte);
                sector := to_integer(unsigned(byte));
                io_read(io, c_drvreg_status, byte);
                side := byte(1);
                report "WD1770 Command: Read Sector " & integer'image(sector) & " (Track: " & integer'image(track) & ", Side: " & std_logic'image(side) & ")";
                io_write_32(io, c_wd177x_dma_addr, X"0000C000" ); -- read a piece of the ROM for now
                io_write(io, c_wd177x_dma_len, X"00");
                io_write(io, c_wd177x_dma_len+1, X"02"); -- 0x200 = sector size
                io_write(io, c_wd177x_dma_mode, X"01"); -- read
                -- data transfer, so we are not yet done
            elsif cmd(7 downto 5) = "101" then
                io_read(io, c_wd177x_sector, byte);
                sector := to_integer(unsigned(byte));
                io_read(io, c_drvreg_status, byte);
                side := byte(1);
                report "WD1770 Command: Write Sector " & integer'image(sector) & " (Track: " & integer'image(track) & ", Side: " & std_logic'image(side) & ")";

                io_write_32(io, c_wd177x_dma_addr, X"00010000" ); -- just write somewhere safe
                io_write(io, c_wd177x_dma_len, X"00");
                io_write(io, c_wd177x_dma_len+1, X"02"); -- 0x200 = sector size
                io_write(io, c_wd177x_dma_mode, X"02"); -- write
                -- data transfer, so we are not yet done
            elsif cmd(7 downto 4) = "1100" then
                report "WD1770 Command: Read Address.";
                write_memory_8(dram, X"00020000", std_logic_vector(to_unsigned(track, 8)) );
                write_memory_8(dram, X"00020001", X"00" ); -- side (!!)
                write_memory_8(dram, X"00020002", std_logic_vector(to_unsigned(sector, 8)) );
                write_memory_8(dram, X"00020003", X"02" ); -- sector length = 512
                write_memory_8(dram, X"00020004", X"F9" ); -- CRC1
                write_memory_8(dram, X"00020005", X"5E" ); -- CRC2
                io_write_32(io, c_wd177x_dma_addr, X"00020000" ); 
                io_write(io, c_wd177x_dma_len, X"06");
                io_write(io, c_wd177x_dma_len+1, X"00"); -- transfer 6 bytes
                io_write(io, c_wd177x_dma_mode, X"01"); -- read
            
            elsif cmd(7 downto 4) = "1110" then
                report "WD1770 Command: Read Track (not implemented).";
            
            elsif cmd(7 downto 4) = "1111" then
                report "WD1770 Command: Write Track.";

                io_write_32(io, c_wd177x_dma_addr, X"00010000" ); -- just write somewhere safe
                io_write(io, c_wd177x_dma_len, X"6A");
                io_write(io, c_wd177x_dma_len+1, X"18"); -- 6250 bytes
                io_write(io, c_wd177x_dma_mode, X"02"); -- write

            elsif cmd(7 downto 4) = "1101" then
                io_write(io, c_wd177x_dma_mode, X"00"); -- stop
                io_write(io, c_wd177x_status_clear, X"01");

            end if;
            
            io_write(io, c_wd177x_irq_ack, X"00");
        end loop;        
    end process;

end architecture;
