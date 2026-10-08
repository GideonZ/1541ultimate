/*
 * ultisid_models.h
 *
 * Which SID model each UltiSID has been given while the SID player maps a tune.
 * With "UltiSID Range Split", one UltiSID serves two of the player's slots,
 * UltiSID 1 slots 2 and 6, UltiSID 2 slots 3 and 7, and both SIDs behind it
 * share one set of combined waveforms and one filter curve. The first SID mapped
 * onto an UltiSID claims its model; a SID of the other model must not take the
 * second slot, because making the UltiSID that model would change the SID that
 * is already mapped.
 *
 * Free of firmware dependencies, so software/test/ultisid_models tests what ships.
 */

#ifndef SOFTWARE_U64_ULTISID_MODELS_H_
#define SOFTWARE_U64_ULTISID_MODELS_H_

#include <stdint.h>

class UltiSidModels
{
    uint8_t claimed[2]; // 0 = no model yet, 1 = 6581, 2 = 8580
public:
    UltiSidModels() { clear(); }

    void clear(void)
    {
        claimed[0] = claimed[1] = 0;
    }

    // -1 for a slot that is not an UltiSID: the sockets and their second SIDs
    static int ultisidOf(int slot)
    {
        switch (slot) {
        case 2: case 6: return 0;
        case 3: case 7: return 1;
        }
        return -1;
    }

    // Whether a SID asking for sidType (1 = 6581, 2 = 8580, 3 = either) can go on this slot
    bool fits(int slot, uint8_t sidType) const
    {
        int emu = ultisidOf(slot);
        if ((emu < 0) || (sidType != 1 && sidType != 2)) {
            return true;
        }
        return (claimed[emu] == 0) || (claimed[emu] == sidType);
    }

    // Records the model of a SID mapped on the slot. True when the UltiSID should be
    // made that model; false when nothing needs to change, or when the UltiSID already
    // has the other model and keeps it.
    bool claim(int slot, uint8_t sidType)
    {
        int emu = ultisidOf(slot);
        if ((emu < 0) || (sidType != 1 && sidType != 2)) {
            return false;
        }
        if (claimed[emu] != 0) {
            return false;
        }
        claimed[emu] = sidType;
        return true;
    }
};

#endif /* SOFTWARE_U64_ULTISID_MODELS_H_ */
