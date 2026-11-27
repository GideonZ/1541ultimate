// Fixture for the TAP file type tests: a tape controller stand-in, a user interface, and a
// way to make a .tap file the fake FileManager can open.
#ifndef TAPE_FILE_UTIL_H
#define TAPE_FILE_UTIL_H
#include "filetype_tap.h"
#include "tape_controller.h"
#include "tape_test_util.h"

namespace tapetest {
struct TapeEnvScope {
    UserInterface ui;
    TapeEnvScope() { reset_env(); tape_controller = new TapeController; }
    ~TapeEnvScope() { delete tape_controller; tape_controller = NULL; fs.release_graveyard(); }
    // A .tap as the recorder writes it: signature, version 1, data length at offset 16.
    void put_tap(const char *name, uint32_t datalen)
    {
        std::string s("C64-TAPE-RAW\001\0\0\0", 16);
        s.push_back((char)(datalen & 0xFF));
        s.push_back((char)((datalen >> 8) & 0xFF));
        s.push_back((char)((datalen >> 16) & 0xFF));
        s.push_back((char)((datalen >> 24) & 0xFF));
        s.append(datalen, '\x30');
        fs.put(name, s);
    }
    SubsysResultCode_e execute(int function, const char *filename)
    {
        SubsysCommand cmd(&ui, SUBSYSID_TAPE_PLAYER, function, 0, "/Usb0", filename);
        return FileTypeTap::execute_st(&cmd);
    }
};

// A full cycle of what the browser does with a .tap that has an index: create the type,
// ask for the entries, let the browser drop the entries, delete the type.
inline void browse_with_index()
{
    BrowsableDirEntry entry("game.tap", "/Usb0", "TAP");
    FileType *ft = FileTypeTap::test_type(&entry);
    IndexedList<Browsable *> list(4, NULL);
    ft->getCustomBrowsables(&entry, list);
    for (int i = 0; i < list.get_elements(); i++) delete list[i];
    delete ft;
}
}
#endif
