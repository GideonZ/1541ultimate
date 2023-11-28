#include "subsys.h"
#include "userinterface.h"

SubsysResultCode_t SubsysCommand :: execute(void)
{
    SubsysResultCode_t retval = { SSRET_SUBSYS_NOT_PRESENT };
    SubsysResultCode_e retcode;
    if(user_interface) {
        user_interface->command_flags = MENU_NOP;
    }
    if(direct_call) {
        retcode = direct_call(this);
        retval = { retcode }; // FIXME
    } else {
        SubSystem *subsys;    // filled in by factory
        subsys = (*SubSystem :: getSubSystems())[subsysID];
        if (subsys) {
            printf("About to execute a command in subsys %s (%p)\n", subsys->identify(), subsys->myMutex);
            if (xSemaphoreTake(subsys->myMutex, 1000)) {
                retcode = subsys->executeCommand(this);
                retval = { retcode }; // FIXME
                xSemaphoreGive(subsys->myMutex);
            } else {
                printf("Could not get lock on %s. Command not executed.\n", subsys->identify());
                retval = { SSRET_NO_LOCK };
            }
        }
    }
    delete this;
    return retval;
}
