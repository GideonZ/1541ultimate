#include "file_system.h"

/*
     length
     in bytes  contents
     --------  ---------------------------------------------------------
        1      1
        6      67, 68, 48, 48, 49 and 1, respectively (same as Volume
                 Descriptor Set Terminator)
        1      0
       32      system identifier
       32      volume identifier
        8      zeros
        8      total number of sectors, as a both endian double word
       32      zeros
        4      1, as a both endian word [volume set size]
        4      1, as a both endian word [volume sequence number]
        4      2048 (the sector size), as a both endian word
        8      path table length in bytes, as a both endian double word
        4      number of first sector in first little endian path table,
                 as a little endian double word
        4      number of first sector in second little endian path table,
                 as a little endian double word, or zero if there is no
                 second little endian path table
        4      number of first sector in first big endian path table,
                 as a big endian double word
        4      number of first sector in second big endian path table,
                 as a big endian double word, or zero if there is no
                 second big endian path table
       34      root directory record, as described below
      128      volume set identifier
      128      publisher identifier
      128      data preparer identifier
      128      application identifier
       37      copyright file identifier
       37      abstract file identifier
       37      bibliographical file identifier
       17      date and time of volume creation
       17      date and time of most recent modification
       17      date and time when volume expires
       17      date and time when volume is effective
        1      1
        1      0
*/

/*
     length
     in bytes  contents
     --------  ---------------------------------------------------------
        1      R, the number of bytes in the record (which must be even)
        1      0 [number of sectors in extended attribute record]
        8      number of the first sector of file data or directory
                 (zero for an empty file), as a both endian double word
        8      number of bytes of file data or length of directory,
                 excluding the extended attribute record,
                 as a both endian double word
        1      number of years since 1900
        1      month, where 1=January, 2=February, etc.
        1      day of month, in the range from 1 to 31
        1      hour, in the range from 0 to 23
        1      minute, in the range from 0 to 59
        1      second, in the range from 0 to 59
                 (for DOS this is always an even number)
        1      offset from Greenwich Mean Time, in 15-minute intervals,
                 as a twos complement signed number, positive for time
                 zones east of Greenwich, and negative for time zones
                 west of Greenwich (DOS ignores this field)
        1      flags, with bits as follows:
                 bit     value
                 ------  ------------------------------------------
                 0 (LS)  0 for a norma1 file, 1 for a hidden file
                 1       0 for a file, 1 for a directory
                 2       0 [1 for an associated file]
                 3       0 [1 for record format specified]
                 4       0 [1 for permissions specified]
                 5       0
                 6       0
                 7 (MS)  0 [1 if not the final record for the file]
        1      0 [file unit size for an interleaved file]
        1      0 [interleave gap size for an interleaved file]
        4      1, as a both endian word [volume sequence number]
        1      N, the identifier length
        N      identifier
        P      padding byte: if N is even, P = 1 and this field contains
                 a zero; if N is odd, P = 0 and this field is omitted
    R-33-N-P   unspecified field for system use; must contain an even
                 number of bytes

*/
#pragma pack(1)
#ifdef LITTLE_ENDIAN
    struct t_directory_record
    {
        uint8_t  record_length;                // 0x00
        uint8_t  sectors_in_extended_attr;     // 0x01
        uint32_t sector;                       // 0x02
        uint32_t be_sector;                    // 0x06
        uint32_t file_size;                    // 0x0A
        uint32_t be_file_size;                 // 0x0E
        uint8_t  years;                        // 0x12
        uint8_t  month;                        // 0x13
        uint8_t  day;                          // 0x14
        uint8_t  hour;                         // 0x15
        uint8_t  minute;                       // 0x16
        uint8_t  second;                       // 0x17
        char  time_offset;                  // 0x18
        uint8_t  flags;                        // 0x19
        uint8_t  file_unit_size;               // 0x1A
        uint8_t  interleave_gap;               // 0x1B
        uint16_t  volume_sequence_number;       // 0x1C
        uint16_t  be_volume_sequence_number;    // 0x1E
        uint8_t  identifier_length;            // 0x20
    } __attribute__((packed));

    struct t_iso9660_volume_descriptor
    {
        uint8_t key[8];                            // 0x00
        char system_identifier[32];             // 0x08
        char volume_identifier[32];             // 0x28
        uint8_t padding1[8];                       // 0x48
        uint32_t sector_count;                     // 0x50
        uint32_t be_sector_count;                  // 0x54
        uint8_t padding2[32];                      // 0x58
        uint16_t  volume_set_size;                  // 0x78
        uint16_t  be_volume_set_size;               // 0x7A
        uint16_t  volume_sequence_number;           // 0x7C
        uint16_t  be_volume_sequence_number;        // 0x7E
        uint16_t  sector_size;                      // 0x80
        uint16_t  be_sector_size;                   // 0x82
        uint32_t path_table_length;                // 0x84
        uint32_t be_path_table_length;             // 0x88
        uint32_t first_path_table_sector;          // 0x8C
        uint32_t second_path_table_sector;         // 0x90
        uint32_t be_first_path_table_sector;       // 0x94
        uint32_t be_second_path_table_sector;      // 0x98
        struct t_directory_record root_dir_rec; // 0x9C
        
    } __attribute__((packed));

#else // big endian

    struct t_directory_record
    {
        uint8_t  record_length;                // 0x00
        uint8_t  sectors_in_extended_attr;     // 0x01
        uint32_t le_sector;                    // 0x02
        uint32_t sector;                       // 0x06
        uint32_t le_file_size;                 // 0x0A
        uint32_t file_size;                    // 0x0E
        uint8_t  years;                        // 0x12
        uint8_t  month;                        // 0x13
        uint8_t  day;                          // 0x14
        uint8_t  hour;                         // 0x15
        uint8_t  minute;                       // 0x16
        uint8_t  second;                       // 0x17
        char  time_offset;                  // 0x18
        uint8_t  flags;                        // 0x19
        uint8_t  file_unit_size;               // 0x1A
        uint8_t  interleave_gap;               // 0x1B
        uint16_t  le_volume_sequence_number;    // 0x1C
        uint16_t  volume_sequence_number;       // 0x1E
        uint8_t  identifier_length;            // 0x20
    } __attribute__((packed));

    struct t_iso9660_volume_descriptor
    {
        uint8_t key[8];                            // 0x00
        char system_identifier[32];             // 0x08
        char volume_identifier[32];             // 0x28
        uint8_t padding1[8];                       // 0x48
        uint32_t le_sector_count;                  // 0x50
        uint32_t sector_count;                     // 0x54
        uint8_t padding2[32];                      // 0x58
        uint16_t  le_volume_set_size;               // 0x78
        uint16_t  volume_set_size;                  // 0x7A
        uint16_t  le_volume_sequence_number;        // 0x7C
        uint16_t  volume_sequence_number;           // 0x7E
        uint16_t  le_sector_size;                   // 0x80
        uint16_t  sector_size;                      // 0x82
        uint32_t le_path_table_length;             // 0x84
        uint32_t path_table_length;                // 0x88
        uint32_t le_first_path_table_sector;       // 0x8C
        uint32_t le_second_path_table_sector;      // 0x90
        uint32_t first_path_table_sector;          // 0x94
        uint32_t second_path_table_sector;         // 0x98
        struct t_directory_record root_dir_rec; // 0x9C
        
    } __attribute__((packed));
#endif

struct t_aligned_directory_record {
    uint8_t dummy[2];
    struct t_directory_record actual;
    char identifier[256];
} __attribute__((packed));

#pragma pack(4)

const uint8_t c_volume_key[8] = { 0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00 };

struct t_iso_handle {
    uint32_t start;
    uint32_t sector;
    int   remaining;
    int   offset;
};

class FileSystem_ISO9660 : public FileSystem 
{
protected:
    Partition *prt;
    uint8_t *sector_buffer;
    uint32_t last_read_sector;
    uint32_t sector_size;
    uint32_t root_dir_sector;
    uint32_t root_dir_size;
    t_aligned_directory_record dir_record;
    bool initialized;
    bool joliet;
    
    void get_dir_record(void *);
public:
    FileSystem_ISO9660(Partition *p);
    ~FileSystem_ISO9660();

    static FileSystem* test(Partition *p);        // check if file system is present on this partition
    bool    init(void);              // Initialize file system
    
    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **dir, FileInfo *inf = 0); // Opens directory (creates dir object, NULL = root)
    void 	dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    
    // functions for reading and writing files
    FRESULT file_open(const char *path, Directory *, const char *filename, uint8_t flags, File **);  // Opens file (creates file object)
    void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_seek(File *f, uint32_t pos);

    FRESULT file_write(File *f, void *buffer, uint32_t len, uint32_t *transferred) {
        *transferred = 0;
        return FR_WRITE_PROTECTED;
    }
    
    FRESULT file_sync(File *f) {              // Clean-up cached data
        return FR_OK;
    }

    uint32_t get_file_size(File *f) {
        t_iso_handle *handle = (t_iso_handle *)f->handle;
        return (uint32_t)(handle->remaining);
    }

    uint32_t get_inode(File *f) {
        t_iso_handle *handle = (t_iso_handle *)f->handle;
        return (uint32_t)(handle->start);
    }

    bool     needs_sorting() { return false; } // aren't files already sorted in an ISO?

};
