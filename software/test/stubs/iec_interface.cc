#include "iec_interface.h"

IecInterface *iec_if;

IecInterface *IecInterface :: get_iec_interface(void)
{
    if (!iec_if) {
        iec_if = new IecInterface();
    }
    return iec_if;
}

IecInterface :: IecInterface()
{
    for(int i=0;i<MAX_SLOTS;i++) {
        slaves[i] = NULL;
    }
    available_slots = MAX_SLOTS;
    addressed_slave = NULL;
    atn = false;
    talking = false;
    enable = false;
    jiffy_load = false;
    jiffy_transfer = 0;
}

IecInterface :: ~IecInterface()
{
}

void IecInterface :: configure(void)
{
    printf("IEC Interface configure.\n");
}

int IecInterface :: register_slave(IecSlave *slave)
{
    for(int i=0; i < available_slots; i++) {
        if(!slaves[i]) {
            slaves[i] = slave;
            printf("Registered %s to slot %d.\n", slaves[i]->iec_identify(), i);
            return i;
        }
    }
    return -1;
}

void IecInterface :: unregister_slave(int slot)
{
    if ((slot < 0) || (slot >= MAX_SLOTS)) {
        return;
    }
    slaves[slot] = NULL;
}

void IecInterface :: run_from_iec(iec_closure_t *c)
{
    printf("I am not going to run your closure, dude.. I am a stub!");
}
