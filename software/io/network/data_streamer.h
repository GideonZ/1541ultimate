#ifndef DATA_STREAMER_H
#define DATA_STREAMER_H

#include "netif/etharp.h"
#include "lwip/inet.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include <lwip/stats.h>
#include "menu.h"
#include "subsys.h"
#include "config.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "timers.h"

extern "C" {
#include "arch/sys_arch.h"
}


typedef struct {
    int      stream_id;
    uint32_t dest_ip;
    int      dest_port;
    uint8_t  dest_mac[6];
    uint8_t  enable;
} stream_config_t;


class DataStreamer : public ObjectWithMenu
{
    ConfigStore *cfg;

    struct {
        Action *startVic;
        Action *stopVic;
        Action *startAud;
        Action *stopAud;
        Action *startDbg;
        Action *stopDbg;
    } myActions;

    uint8_t  my_mac[6];
    uint32_t my_ip;
    volatile bool palette_stream_requested;
    volatile uint16_t palette_generation;
    TaskHandle_t palette_task_handle;
    int palette_socket;
    uint32_t palette_socket_ip;

    stream_config_t streams[4];
    TimerHandle_t timers[4];

    static void S_timer(TimerHandle_t a);
    static void S_palette_task(void *context);
    SubsysResultCode_e startStream(SubsysCommand *cmd);
    SubsysResultCode_e stopStream(SubsysCommand *cmd);

    void calculate_udp_headers(int id);
    void send_udp_packet(uint32_t ip, uint16_t port, const uint8_t *data, int length);
    bool sendVicPalette();
    void paletteTask();
public:
    DataStreamer();
    virtual ~DataStreamer();

    static SubsysResultCode_e S_startStream(SubsysCommand *cmd);
    static SubsysResultCode_e S_stopStream(SubsysCommand *cmd);

    void vicPaletteChanged();

    // from ObjectWithMenu
    void create_task_items(void);
    void update_task_items(bool writablePath);

};

extern DataStreamer *dataStreamer;

#endif
