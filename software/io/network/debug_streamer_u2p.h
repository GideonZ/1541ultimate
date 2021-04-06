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
        Action *startDbg;
        Action *stopDbg;
    } myActions;

    uint8_t  my_mac[6];
    uint32_t my_ip;

    stream_config_t streams[4];
    TimerHandle_t timers[4];

    static void S_timer(TimerHandle_t a);
    int startStream(SubsysCommand *cmd);
    int stopStream(SubsysCommand *cmd);

    void calculate_udp_headers(int id);
    void send_udp_packet(uint32_t ip, uint16_t port);
public:
    DataStreamer();
    virtual ~DataStreamer();

    static int  S_startStream(SubsysCommand *cmd);
    static int  S_stopStream(SubsysCommand *cmd);

    // from ObjectWithMenu
    void create_task_items(void);
    void update_task_items(bool writablePath, Path *path);

};

extern DataStreamer dataStreamer;

#endif
