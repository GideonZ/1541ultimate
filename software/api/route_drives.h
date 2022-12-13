#include "dict.h"
#include "subsys.h"
#include "c1541.h"
#include <string.h>

class DriveToSubsys : public Dict<const char *, int>
{
public:
    DriveToSubsys() : Dict(2, NULL, -1, &strcasecmp) {
        set("a", SUBSYSID_DRIVE_A);
        set("b", SUBSYSID_DRIVE_B);
    }
};

class ImageTypeToInt : public Dict<const char *, int>
{
public:
    ImageTypeToInt() : Dict(3, NULL, -1, &strcasecmp) {
        set("d64", 1541);
        set("d71", 1571);
        set("d81", 1581);
    }
};

class ImageTypeToCommand : public Dict<const char *, int>
{
public:
    ImageTypeToCommand() : Dict(5, NULL, -1, &strcasecmp) {
        set("d64", MENU_1541_MOUNT_D64);
        set("d71", MENU_1541_MOUNT_D64);
        set("d81", MENU_1541_MOUNT_D64);
        set("g64", MENU_1541_MOUNT_G64);
        set("g71", MENU_1541_MOUNT_G64);
    }
};

class ModeToInt : public Dict<const char *, int>
{
public:
    ModeToInt() : Dict(3, NULL, 0, &strcasecmp) {
        set("readwrite", 0);
        set("readonly", MENU_1541_READ_ONLY);
        set("unlinked", MENU_1541_UNLINKED);
    }
};
