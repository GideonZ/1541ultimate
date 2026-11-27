// Stand-in for browsable_root.h: the directory entry that FileTypeTap is built from.
#ifndef TAPE_TESTS_BROWSABLE_ROOT_H
#define TAPE_TESTS_BROWSABLE_ROOT_H
#include <string>
#include "browsable.h"
#include "filemanager.h"
#include "filetypes.h"
class UserInterface;
class BrowsableDirEntry : public Browsable
{
public:
    std::string name, path;
    FileInfo info;
    BrowsableDirEntry(const char *n, const char *p, const char *ext) : name(n), path(p), info(n) {
        strncpy(info.extension, ext, 3);
        info.extension[3] = 0;
    }
    const char *getName() { return name.c_str(); }
    const char *getPath() { return path.c_str(); }
    FileInfo *getInfo() { return &info; }
};
#endif
