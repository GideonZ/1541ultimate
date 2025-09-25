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
#include "filetype_vpl.h"

#define DATA_DIRECTORY "/flash/data"

class U64Config;
extern U64Config *u64_configurator;

class U64Config : public ConfigurableObject, ObjectWithMenu, SubSystem
{
    struct {
        Action *poke;
        Action *saveedid;
        Action *siddetect;
        Action *esp32off;
        Action *esp32on;
        Action *esp32boot;
        Action *uartecho;
        Action *wifiecho;
    } myActions;

    t_video_mode systemMode;
    FileManager *fm;
	bool skipReset;
    TaskHandle_t resetTaskHandle;
    SidDevice *sidDevice[2];
    //alt_irq_context irq_context;
    bool temporary_stop;

    uint8_t hdmiSetting;
    TaskHandle_t hpd_monitor_task_handle;
    SemaphoreHandle_t hpd_monitor_sem;
    static uint8_t hpd_monitor_irq(void *a);
    static void hpd_monitor_task(void *a);

    class U64Mixer : public ConfigurableObject
    {
    public:
        U64Mixer();
        void effectuate_settings();
    };

#if U64 == 2
    class U64SpeakerMixer : public ConfigurableObject
    {
    public:
        U64SpeakerMixer();
        void effectuate_settings();
    };
#endif
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
#if U64 == 2
    U64SpeakerMixer speakercfg;
#endif
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
    static U64Config *getConfigurator() { return u64_configurator; }

    void ResetHandler();
    void create_task_items(void);
    void update_task_items(bool writablePath);
    SubsysResultCode_e executeCommand(SubsysCommand *cmd);
    void effectuate_settings();

    static int setPllOffset(ConfigItem *it);
    static int setScanMode(ConfigItem *it);
    static int setMixer(ConfigItem *it);
    static int setSpeakerMixer(ConfigItem *it);
    static int enableDisableSpeaker(ConfigItem *it);
    static int setFilter(ConfigItem *it);
    static int setSidEmuParams(ConfigItem *it);
    static int setLedSelector(ConfigItem *it);
    static int setCpuSpeed(ConfigItem *it);

    static void SetResampleFilter(t_video_mode mode);
    static void auto_mirror(uint8_t *base, uint8_t *mask, uint8_t *split, int count);
    static void get_sid_addresses(ConfigStore *cfg, uint8_t *base, uint8_t *mask, uint8_t *split);
    static void fix_splits(uint8_t *base, uint8_t *mask, uint8_t *split);
    static void list_palettes(ConfigItem *it, IndexedList<char *>& strings);
    static void set_palette_rgb(const uint8_t rgb[16][3]);
    static void set_palette_yuv(const uint8_t yuv[16][3]);
    static void rgb_to_yuv(const uint8_t rgb[3], uint8_t yuv[3], bool ntsc);
    static bool load_palette_vpl(const char *path, const char *filename);
    static void late_init_palette(void *obj, void *param);
    void set_palette_filename(const char *filename);

    bool SidAutoConfig(int count, t_sid_definition *requested);
    static void show_sid_addr(UserInterface *intf, ConfigItem *it);

    volatile uint8_t *access_socket_pre(int socket);
    void access_socket_post(int socket);
    void clear_ram(void);
    void setup_config_menu();
};

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
