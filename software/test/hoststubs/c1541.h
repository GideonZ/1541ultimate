#ifndef TEST_STUBS_C1541_H
#define TEST_STUBS_C1541_H

// Host-test stub for software/drive/c1541.h.
//
// The real header makes C1541 a Browsable and a SubSystem, which drags in the
// entire user interface and lwIP network stack (browsable_root.h ->
// assembly_search.h -> ...). The command-interface tests only need to ask a
// drive for its IEC address, power state and subsystem id, so this stub
// provides exactly that, with setters so a test can arrange drive state.

#include "integer.h"
#include "subsys.h"

// Command ids, kept identical to software/drive/c1541.h.
#define MENU_1541_RESET     0x1501
#define MENU_1541_REMOVE    0x1502
#define MENU_1541_SAVED64   0x1503
#define MENU_1541_SAVEG64   0x1504
#define MENU_1541_BLANK     0x1505
#define MENU_1541_TURNON    0x1506
#define MENU_1541_TURNOFF   0x1507
#define FLOPPY_LOAD_DOS     0x1508
#define MENU_1541_UNLINK    0x1513
#define MENU_1541_SWAP      0x1514
#define MENU_1541_SET_MODE  0x1515

#define MENU_1541_READ_ONLY    0x8000
#define MENU_1541_UNLINKED     0x4000
#define MENU_1541_NO_FLAGS     0x1FFF

#define MENU_1541_MOUNT_D64    0x1520 // +1 for .d71, +2 for .d81
#define MENU_1541_MOUNT_D64_RO (MENU_1541_MOUNT_D64 | MENU_1541_READ_ONLY)
#define MENU_1541_MOUNT_D64_UL (MENU_1541_MOUNT_D64 | MENU_1541_UNLINKED)

#define MENU_1541_MOUNT_G64    0x1530
#define MENU_1541_MOUNT_G64_RO (MENU_1541_MOUNT_G64 | MENU_1541_READ_ONLY)
#define MENU_1541_MOUNT_G64_UL (MENU_1541_MOUNT_G64 | MENU_1541_UNLINKED)

class C1541 : public SubSystem
{
    int  iec_address;
    bool drive_power;
public:
    C1541(int id = SUBSYSID_DRIVE_A, int iec_address = 8)
        : SubSystem(id), iec_address(iec_address), drive_power(true) { }
    virtual ~C1541() { }

    const char *identify(void) { return "Stub 1541"; }

    int  get_effective_iec_address(void) { return iec_address; }
    void set_effective_iec_address(int address) { iec_address = address; }

    bool get_drive_power(void) { return drive_power; }
    void set_drive_power(bool on) { drive_power = on; }

    // Tests set this to steer C1541::get_last_mounted_drive().
    static C1541 *&last_mounted_drive()
    {
        static C1541 *drive = 0;
        return drive;
    }

    static C1541 *get_last_mounted_drive(void) { return last_mounted_drive(); }
};

extern C1541 *c1541_A;
extern C1541 *c1541_B;

#endif // TEST_STUBS_C1541_H
