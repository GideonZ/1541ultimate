#ifndef C1581_H_
#define C1581_H_

#include <stdio.h>
#include "integer.h"
#include "filemanager.h"
#include "file_system.h"
#include "config.h"
#include "subsys.h"
#include "menu.h" // to add menu items
#include "flash.h"
#include "iomap.h"
#include "browsable_root.h"
#include "iec.h"
#include "iec_channel.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define D81FILE_MOUNT      0x2102

class C1581 : public SubSystem, ConfigurableObject, ObjectWithMenu
{
    public:
        C1581(char letter);
        ~C1581();
        int  executeCommand(SubsysCommand *cmd);
};

extern C1581 *c1581_C;

#endif