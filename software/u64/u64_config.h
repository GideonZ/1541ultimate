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
#include "menu.h"
#include "sid_device.h"
#include "u64.h"

class U64Config : public ConfigurableObject, ObjectWithMenu, SubSystem
{
    struct {
        Action *poke;
        Action *saveedid;
        Action *siddetect;
        Action *wifioff;
        Action *wifion;
        Action *wifiboot;
    } myActions;

    t_video_mode systemMode;
    FileManager *fm;
	bool skipReset;
    TaskHandle_t resetTaskHandle;
    SidDevice *sidDevice[2];
    alt_irq_context irq_context;
    bool temporary_stop;

    class U64Mixer : public ConfigurableObject
    {
    public:
        U64Mixer();
        void effectuate_settings();
    };

    class U64SidSockets : public ConfigurableObject
    {
    public:
        U64SidSockets();
        void effectuate_settings();
        void detect();
    };

    class U64SidAddressing : public ConfigurableObject
    {
    public:
        U64SidAddressing();
        void effectuate_settings();
    };

    class U64UltiSids : public ConfigurableObject
    {
    public:
        U64UltiSids();
        void effectuate_settings();
    };

    U64Mixer mixercfg;
    U64SidSockets sockets;
    U64UltiSids ultisids;
    U64SidAddressing sidaddressing;
    bool hdmiMonitor;

    uint8_t GetSidType(int slot);
    void SetSidType(int slot, uint8_t sidType);
    bool SetSidAddress(int slot, bool single, uint8_t actualType, uint8_t base);
    bool MapSid(int index, int totalCount, uint16_t& mappedSids, uint8_t *mappedOnSlot, t_sid_definition *requested, bool any);
    void SetMixerAutoSid(uint8_t *slots, int count);
    static void unmapAllSids(void);
    static void reset_task(void *a);
    void run_reset_task();
    static void show_mapping(uint8_t *base, uint8_t *mask, uint8_t *split, int count);
    static void DetectSidImpl(uint8_t *buffer) __attribute__ ((section ("detect_sid"), used));
    static void S_SetupDetectionAddresses();
    static void S_RestoreDetectionAddresses();
    static int S_SidDetector(int &sid1, int &sid2);
    int detectRemakes(int socket);
    int detectFPGASID(int socket);
    int detectDukestahAdapter();
    bool IsMonitorHDMI();
    SidDevice *getDevice(int index) { return sidDevice[index]; }
public:
    U64Config();
    ~U64Config() {}

    void ResetHandler();
    void create_task_items(void);
    void update_task_items(bool writablePath, Path *p);
    int executeCommand(SubsysCommand *cmd);
    void effectuate_settings();

    static int setPllOffset(ConfigItem *it);
    static int setScanMode(ConfigItem *it);
    static int setMixer(ConfigItem *it);
    static int setFilter(ConfigItem *it);
    static int setSidEmuParams(ConfigItem *it);
    static int setLedSelector(ConfigItem *it);
    static int setCpuSpeed(ConfigItem *it);

    static void SetResampleFilter(t_video_mode mode);
    static void auto_mirror(uint8_t *base, uint8_t *mask, uint8_t *split, int count);
    static void get_sid_addresses(ConfigStore *cfg, uint8_t *base, uint8_t *mask, uint8_t *split);
    static void fix_splits(uint8_t *base, uint8_t *mask, uint8_t *split);

    bool SidAutoConfig(int count, t_sid_definition *requested);
    static void show_sid_addr(UserInterface *intf);

    volatile uint8_t *access_socket_pre(int socket);
    void access_socket_post(int socket);
};

bool isEliteBoard(void);
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
