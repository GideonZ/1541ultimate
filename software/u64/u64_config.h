/*
 * u64_config.h
 *
 *  Created on: Oct 6, 2017
 *      Author: Gideon
 */

#ifndef U64_CONFIG_H_
#define U64_CONFIG_H_

#include "config.h"
#include "filemanager.h"
#include "subsys.h"
#include "sid_config.h"

class U64Config : public ConfigurableObject, ObjectWithMenu, SubSystem
{
    int systemMode;
    FileManager *fm;
	bool skipReset;
    TaskHandle_t resetTaskHandle;

    int DetectSid(void);
    uint8_t GetSidType(int slot);
    void SetSidType(int slot, uint8_t sidType);
    bool SetSidAddress(int slot, bool single, uint8_t actualType, uint8_t base);
    bool MapSid(int index, int totalCount, uint16_t& mappedSids, uint8_t *mappedOnSlot, t_sid_definition *requested, bool any);
    void SetMixerAutoSid(uint8_t *slots, int count);
    static void reset_task(void *a);
    void run_reset_task();

public:
    U64Config();
    ~U64Config() {}

    void ResetHandler();
    void effectuate_settings();
    int fetch_task_items(Path *p, IndexedList<Action*> &item_list);
    int executeCommand(SubsysCommand *cmd);

    static void setPllOffset(ConfigItem *it);
    static void setScanMode(ConfigItem *it);
    static void setMixer(ConfigItem *it);
    static void setFilter(ConfigItem *it);
    static void setSidEmuParams(ConfigItem *it);
    static void setLedSelector(ConfigItem *it);

    bool SidAutoConfig(int count, t_sid_definition *requested);
};

extern U64Config u64_configurator;

extern uint8_t C64_EMUSID1_BASE_BAK;
extern uint8_t C64_EMUSID2_BASE_BAK;
extern uint8_t C64_SID1_BASE_BAK;
extern uint8_t C64_SID2_BASE_BAK;
extern uint8_t C64_EMUSID1_MASK_BAK;
extern uint8_t C64_EMUSID2_MASK_BAK;
extern uint8_t C64_SID1_MASK_BAK;
extern uint8_t C64_SID2_MASK_BAK;
extern uint8_t C64_SID1_EN_BAK;
extern uint8_t C64_SID2_EN_BAK;
extern uint8_t C64_STEREO_ADDRSEL_BAK;


#endif /* U64_CONFIG_H_ */
