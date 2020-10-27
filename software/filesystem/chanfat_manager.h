#ifndef CHANFAT_MANAGER_H
#define CHANFAT_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include "partition.h"
#include "ff.h"

class ChanFATManager
{
    Partition* driveMap[FF_VOLUMES];

    ChanFATManager() // private constructor => Singleton
    {
        for (int i=0; i<FF_VOLUMES; i++) {
            driveMap[i] = NULL;
        }
    }
public:
    static ChanFATManager *getManager()
    {
        static ChanFATManager cm;
        return &cm;
    }

    uint8_t addDrive(Partition *p)
    {
        for (int i=0; i<FF_VOLUMES; i++) {
            if (driveMap[i] == NULL) {
                driveMap[i] = p;
                return (uint8_t)i;
            }
        }
        return 0xFF; // error
    }

    void removeDrive(uint8_t index)
    {
        if (index < FF_VOLUMES) {
            driveMap[index] = NULL;
        }
    }

    static Partition *getPartition(uint8_t index)
    {
        if (index < FF_VOLUMES) {
            return getManager()->driveMap[index];
        }
        return NULL;
    }
};

#endif

