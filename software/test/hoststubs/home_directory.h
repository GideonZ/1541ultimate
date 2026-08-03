#ifndef TEST_HOSTSTUBS_HOME_DIRECTORY_H
#define TEST_HOSTSTUBS_HOME_DIRECTORY_H

// Host-test stub for software/userinterface/home_directory.h.
//
// The real class owns a polling task, a TreeBrowser and the config store. Only
// the configured path matters to the command interface, so this stub keeps it
// in a plain string that tests can set directly.

#include <string.h>

class HomeDirectory
{
public:
    static const char *getHomeDirectory(void)
    {
        return storage();
    }

    static void setHomeDirectory(const char *path)
    {
        if (!path) {
            storage()[0] = 0;
            return;
        }
        strncpy(storage(), path, max_length - 1);
        storage()[max_length - 1] = 0;
    }

private:
    static const int max_length = 256;

    static char *storage()
    {
        static char path[max_length] = "/";
        return path;
    }
};

#endif // TEST_HOSTSTUBS_HOME_DIRECTORY_H
