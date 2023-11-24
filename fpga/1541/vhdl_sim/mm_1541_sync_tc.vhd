library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_bfm_pkg.all;
use work.tl_flat_memory_model_pkg.all;
use work.c1541_pkg.all;
use work.tl_string_util_pkg.all;
use work.iec_bus_bfm_pkg.all;

entity mm_1541_sync_tc is

end;

architecture tc of mm_1541_sync_tc is
    signal irq      : std_logic;

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

        load_memory("run.bin", dram, X"0000FF00");

        rotation_speed := (31250000 / 20);
        for i in 0 to 34 loop
            value := X"00100000";
            params(15 downto 0) := X"1E0C"; -- 1E0C, 1BE7, 1A0B, 186A

            bit_time := rotation_speed / to_integer(unsigned(params(15 downto 0)));
            params(31 downto 16) := std_logic_vector(to_unsigned(bit_time, 16));
            report "Track " & integer'image(i+1) & ": " & hstr(value) & " - " & hstr(params);
            io_write_32(io, c_param_ram + 16*i, std_logic_vector(value) );
            io_write_32(io, c_param_ram + 16*i + 4, params );
            io_write_32(io, c_param_ram + 16*i + 8, std_logic_vector(value) );
            io_write_32(io, c_param_ram + 16*i + 12, params );
        end loop;

        value := X"00100000";
        for i in 0 to 640 loop -- 641 x 12 bytes = 7692 bytes
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"5A"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"5A"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
            write_memory_8(dram, std_logic_vector(value), X"FF"); value := value + 1;
        end loop;

        wait for 20 us;
        io_write(io, c_drvreg_drivetype, X"00"); -- switch to 1541 mode
        io_write(io, c_drvreg_power, X"01");
        wait for 20 us;
        io_write(io, c_drvreg_inserted, X"01");
        io_write(io, c_drvreg_reset, X"00");
        
        wait;                
        
    end process;

end architecture;
