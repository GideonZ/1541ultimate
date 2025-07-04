#include "cbmdos_parser.h"
#include "dump_hex.h"

// temporary
void print_file(filename_t& name);

int IecCommandExecuter::do_block_read(int chan, int part, int track, int sector)
{
    printf("Block read: Channel %d, Partition %d, T/S %d/%d\n", chan, part, track, sector);
    return 0;
}

int IecCommandExecuter::do_block_write(int chan, int part, int track, int sector)
{
    printf("Block write: Channel %d, Partition %d, T/S %d/%d\n", chan, part, track, sector);
    return 0;
}

int IecCommandExecuter::do_buffer_position(int chan, int pos)
{
    printf("Position in buffer: Chan %d: %d\n", chan, pos);
    return 0;
}

int IecCommandExecuter::do_set_current_partition(int part)
{
    printf("Change to partition %d\n", part);
    return 0;
}

int IecCommandExecuter::do_change_dir(int part, mstring& path)
{
    printf("Partition %d Change dir %s\n",  part, path.c_str());
    return 0;
}

int IecCommandExecuter::do_make_dir(int part, mstring& path)
{
    printf("Partition %d Make dir %s\n",  part, path.c_str());
    return 0;
}

int IecCommandExecuter::do_remove_dir(int part, mstring& path)
{
    printf("Partition %d Remove dir %s\n",  part, path.c_str());
    return 0;
}

int IecCommandExecuter::do_copy(filename_t& dest, filename_t sources[], int n)
{
    printf("Copy %d sources to ", n);
    print_file(dest);
    for(int i=0;i<n;i++) {
        printf("  %d. ", i);
        print_file(sources[i]);
    }
    return 0;
}

int IecCommandExecuter::do_initialize()
{
    return 73;
}

int IecCommandExecuter::do_format(uint8_t *name, uint8_t id1, uint8_t id2)
{
    printf("Format: %s %02x %02x\n", name, id1, id2);
    return 0;
}

int IecCommandExecuter::do_rename(filename_t &src, filename_t &dest)
{
    printf("Rename from: ");
    print_file(src);
    printf("  To: ");
    print_file(dest);
    return 0;
}

int IecCommandExecuter::do_scratch(filename_t filenames[], int n)
{
    printf("Scratch %d files:\n", n);
    for(int i=0;i<n;i++) {
        printf("  %d. ", i);
        print_file(filenames[i]);
    }
    return 0;
}

int IecCommandExecuter::do_cmd_response(uint8_t *data, int len)
{
    dump_hex_relative(data, len);
    return 0;
}

int IecCommandExecuter::do_set_position(int chan, uint32_t pos)
{
    printf("Set File position to %lu on chan %d\n", pos, chan);
    return 0;
}
