#ifndef CBMNAME_H
#define CBMNAME_H

class CbmFileName
{
    char    name[19];
    uint8_t type;
    int     length;
    bool    cut;
    bool    initialized;
public:
    CbmFileName()
    {
        initialized = false; // rest is don't care;
    }

    CbmFileName(const char *filename)
    {
        this->init(filename);
    }

    void init(const char *filename)
    {
        char ext[4];
        cut = false;
        get_extension(filename, ext);

        if (strcasecmp(ext, "prg") == 0) {
            type = 0x02; // PRG, opened file
            cut = true;
        } else if (strcasecmp(ext, "usr") == 0) {
            type = 0x03; // USR, opened file
            cut = true;
        } else if (strcasecmp(ext, "rel") == 0) {
            type = 0x04; // REL, opened file
            cut = true;
        } else if (strcasecmp(ext, "seq") == 0) {
            type = 0x01; // SEQ, opened file
            cut = true;
        } else if (strcasecmp(ext, "cvt") == 0) {
            type = 0x07; // This is a hack!
            cut = true;
        } else {
            type = 0x02;
        }
        fat_to_petscii(filename, cut, name, 16, true);
        length = strlen(name);
        initialized = true;
    };

    void init_dir(const char *dirname)
    {
        cut = false;
        type = 0x06;
        fat_to_petscii(dirname, cut, name, 16, true);
        length = strlen(name);
        initialized = true;
    }

    const char *getExtension(void)
    {
        switch(type & 0x07) {
        case 0: return "del";
        case 1: return "seq";
        case 2: return "prg";
        case 3: return "usr";
        case 4: return "rel";
        case 5: return "cbm";
        case 7: return "cvt";
//        case 6: return "dir";
        }
        return "";
    };

    uint8_t getType(void)
    {
        return type;
    }

    bool hadExtension(void)
    {
        return cut;
    }

    int getLength(void)
    {
        return length;
    }

    const char *getName(void)
    {
        return name;
    }

    bool isInitialized(void)
    {
        return initialized;
    }

    void reset(void)
    {
        initialized = false;
    }
};
#endif
