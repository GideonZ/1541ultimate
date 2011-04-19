-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 - Gideon's Logic Architectures
--
-------------------------------------------------------------------------------
-- Title      : Wave package
-------------------------------------------------------------------------------
-- File       : wave_pkg.vhd
-- Author     : Gideon Zweijtzer <gideon.zweijtzer@gmail.com>
-------------------------------------------------------------------------------
-- Description: This package provides ways to write (and maybe in future read)
--              .wav files.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.tl_flat_memory_model_pkg.all;
use work.tl_file_io_pkg.all;

package wave_pkg is

    type t_wave_channel is record
        number_of_samples   : integer;
        memory              : h_mem_object;
    end record;
    
    type t_wave_channel_array   is array(natural range <>) of t_wave_channel;
    
    procedure open_channel(chan : out t_wave_channel);
    procedure push_sample(chan : inout t_wave_channel; sample : integer);
    procedure write_wave(name: string; rate : integer; channels : t_wave_channel_array);

end package;

package body wave_pkg is

    procedure open_channel(chan : out t_wave_channel) is
        variable ch : t_wave_channel;
    begin
        register_mem_model("path", "channel", ch.memory);
        ch.number_of_samples := 0;
        chan := ch;
    end procedure;
    
    procedure push_sample(chan : inout t_wave_channel; sample : integer) is
        variable s  : integer;
    begin
        s := sample;
        if s > 32767 then s := 32767; end if;
        if s < -32768 then s := -32768; end if;
        write_memory_int(chan.memory, chan.number_of_samples, s);
        chan.number_of_samples := chan.number_of_samples + 1;
    end procedure;
    
    procedure write_vector_le(x : std_logic_vector; file f : t_binary_file; r : inout t_binary_file_rec) is
        variable bytes : integer := (x'length + 7) / 8;
        variable xa : std_logic_vector(7+bytes*8 downto 0);
    begin
        xa := (others => '0');
        xa(x'length-1 downto 0) := x;
        for i in 0 to bytes-1 loop
            write_byte(f, xa(i*8+7 downto i*8), r);
        end loop;
    end procedure;

    procedure write_int_le(x : integer; file f : t_binary_file; r : inout t_binary_file_rec) is
        variable x_slv  : std_logic_vector(31 downto 0);
    begin
        x_slv := std_logic_vector(to_signed(x, 32));
        write_vector_le(x_slv, f, r);
    end procedure;
   
    procedure write_short_le(x : integer; file f : t_binary_file; r : inout t_binary_file_rec) is
        variable x_slv  : std_logic_vector(15 downto 0);
    begin
        x_slv := std_logic_vector(to_signed(x, 16));
        write_vector_le(x_slv, f, r);
    end procedure;


    procedure write_wave(name: string; rate : integer; channels : t_wave_channel_array) is
        file myfile    : t_binary_file;
        variable myrec : t_binary_file_rec;
        variable stat  : file_open_status;
        variable file_size  : integer;
        variable data_size  : integer;
        variable max_length : integer;
    begin
        -- open file
        file_open(stat, myfile, name, write_mode);
        assert (stat = open_ok)
            report "Could not open file " & name & " for writing."
            severity failure;
        init_record(myrec);

        max_length := 0;
        for i in channels'range loop
            if channels(i).number_of_samples > max_length then
                max_length := channels(i).number_of_samples;
            end if;
        end loop;

        data_size := (max_length * channels'length * 2);
        file_size := 12 + 16 + 8 + data_size;

        -- header
        write_vector_le(X"46464952",                myfile, myrec); -- "RIFF"
        write_int_le   (file_size-8,                myfile, myrec);
        write_vector_le(X"45564157",                myfile, myrec); -- "WAVE"
                                                    
        -- chunk header                             
        write_vector_le(X"20746D66",                myfile, myrec); -- "fmt "
        write_int_le   (16,                         myfile, myrec);
        write_short_le (1,                          myfile, myrec); -- compression code = uncompressed
        write_short_le (channels'length,            myfile, myrec);
        write_int_le   (rate,                       myfile, myrec); -- sample rate
        write_int_le   (rate * channels'length * 2, myfile, myrec); -- Bps
        write_short_le (channels'length * 2,        myfile, myrec); -- alignment
        write_short_le (16,                         myfile, myrec); -- bits per sample
        
        write_vector_le(X"61746164",                myfile, myrec); -- "data"
        write_int_le   (data_size,                  myfile, myrec);
        
        -- now write out all data!
        for i in 0 to max_length-1 loop
            for j in channels'range loop
                write_short_le(read_memory_int(channels(j).memory, i), myfile, myrec);
            end loop;
        end loop;
        purge(myfile, myrec);
        file_close(myfile);

    end procedure;
end;
