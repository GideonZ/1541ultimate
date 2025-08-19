#include "cbmdos_parser.h"
#include "dump_hex.h"

// temporary
void print_file(filename_t& name);

class IecCommandExecuterStubs : public IecCommandExecuter
{
public:
    int do_block_read(int chan, int part, int track, int sector);
    int do_block_write(int chan, int part, int track, int sector);
    int do_block_allocate(int chan, int part, int track, int sector, bool allocate);
    int do_buffer_position(int chan, int pos);
    int do_set_current_partition(int part);
    int do_change_dir(filename_t& dest);
    int do_make_dir(filename_t& dest);
    int do_remove_dir(filename_t& dest);
    int do_copy(filename_t& dest, filename_t sources[], int n);
    int do_initialize();
    int do_format(uint8_t *name, uint8_t id1, uint8_t id2);
    int do_rename(filename_t &src, filename_t &dest);
    int do_scratch(filename_t filenames[], int n);
    int do_cmd_response(uint8_t *data, int len);
    int do_set_position(int chan, uint32_t pos, int recnr, int recoffset);
    int do_pwd_command();
    int do_get_partition_info(int part);
};


int IecCommandExecuterStubs::do_block_read(int chan, int part, int track, int sector)
{
    printf("Block read: Channel %d, Partition %d, T/S %d/%d\n", chan, part, track, sector);
    return 0;
}

int IecCommandExecuterStubs::do_block_write(int chan, int part, int track, int sector)
{
    printf("Block write: Channel %d, Partition %d, T/S %d/%d\n", chan, part, track, sector);
    return 0;
}

int IecCommandExecuterStubs::do_block_allocate(int chan, int part, int track, int sector, bool allocate)
{
    printf("Block %s: Channel %d, Partition %d, T/S %d/%d\n", allocate ? "allocate" : "free", chan, part, track, sector);
    return 0;
}

int IecCommandExecuterStubs::do_buffer_position(int chan, int pos)
{
    printf("Position in buffer: Chan %d: %d\n", chan, pos);
    return 0;
}

int IecCommandExecuterStubs::do_set_current_partition(int part)
{
    printf("Change to partition %d\n", part);
    return 0;
}

int IecCommandExecuterStubs::do_change_dir(filename_t& dest)
{
    printf("Partition %d Change dir %s:%s\n",  dest.partition, dest.path.c_str(), dest.filename.c_str());
    return 0;
}

int IecCommandExecuterStubs::do_make_dir(filename_t& dest)
{
    printf("Partition %d Make dir %s:%s\n",  dest.partition, dest.path.c_str(), dest.filename.c_str());
    return 0;
}

int IecCommandExecuterStubs::do_remove_dir(filename_t& dest)
{
    printf("Partition %d Remove dir %s:%s\n",  dest.partition, dest.path.c_str(), dest.filename.c_str());
    return 0;
}

int IecCommandExecuterStubs::do_copy(filename_t& dest, filename_t sources[], int n)
{
    printf("Copy %d sources to ", n);
    print_file(dest);
    for(int i=0;i<n;i++) {
        printf("  %d. ", i);
        print_file(sources[i]);
    }
    return 0;
}

int IecCommandExecuterStubs::do_initialize()
{
    return 73;
}

int IecCommandExecuterStubs::do_format(uint8_t *name, uint8_t id1, uint8_t id2)
{
    printf("Format: %s %02x %02x\n", name, id1, id2);
    return 0;
}

int IecCommandExecuterStubs::do_rename(filename_t &src, filename_t &dest)
{
    printf("Rename from: ");
    print_file(src);
    printf("  To: ");
    print_file(dest);
    return 0;
}

int IecCommandExecuterStubs::do_scratch(filename_t filenames[], int n)
{
    printf("Scratch %d files:\n", n);
    for(int i=0;i<n;i++) {
        printf("  %d. ", i);
        print_file(filenames[i]);
    }
    return 0;
}

int IecCommandExecuterStubs::do_cmd_response(uint8_t *data, int len)
{
    dump_hex_relative(data, len);
    return 0;
}

int IecCommandExecuterStubs::do_set_position(int chan, uint32_t pos, int recnr, int recoffset)
{
    printf("Set File position to %lu on chan %d (or record #%d:%d)\n", pos, chan, recnr, recoffset);
    return 0;
}

int IecCommandExecuterStubs::do_pwd_command()
{
    printf("PWD command\n");
    return 0;
}
