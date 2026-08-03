/*
 * Host stub for software/io/c64/c64.h.
 *
 * The real header cannot be compiled for a 64-bit host: get_cartridge_max_rom()
 * casts pointers to uint32_t, which is a hard error off the 32-bit target.
 *
 * dos.cc only reaches C64 for the two MP3 ramdrive queries below, neither of
 * which is on any path this test exercises, so stubbing the header collapses
 * the whole dependency without changing what is under test.
 */
#ifndef HOSTSTUB_C64_H
#define HOSTSTUB_C64_H

#include <stdint.h>

#define REU_MEMORY_BASE 0x1000000
#define DRVTYPE_MP3_DNP 31

// Defined in firmware_globals.cc, which reports both ram drives as absent.
class C64
{
public:
    static int isMP3RamDrive(int dev);
    static int getSizeOfMP3NativeRamdrive(int dev);
};

#endif
