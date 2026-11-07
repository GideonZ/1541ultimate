#ifndef HOST_UCI_SUBSYS_H
#define HOST_UCI_SUBSYS_H
#include "FreeRTOS.h"
#include <stdio.h>
#define SUBSYSID_CMD_IF 8
class SubSystem
{
public:
    SubSystem(int) { }
    virtual ~SubSystem() { }
};
#endif
