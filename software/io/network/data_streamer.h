#ifndef DATA_STREAMER_H
#define DATA_STREAMER_H

#include "netif/etharp.h"
#include "lwip/inet.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include <lwip/stats.h>
#include "menu.h"
#include "subsys.h"
extern "C" {
#include "arch/sys_arch.h"
}


typedef struct {
    bool enable;
    int stream_id;
    struct ip_addr my_ip;
    struct ip_addr your_ip;
    uint16_t src_port;
    uint16_t dest_port;
} stream_config_t;


class DataStreamer : public ObjectWithMenu
{
    uint32_t my_ip;
    uint32_t vic_dest_ip;
    int      vic_dest_port;
    uint8_t  vic_enable;
    uint8_t  my_mac[6];
    uint8_t  vic_dest_mac[6];

    static int S_startVicStream(SubsysCommand *cmd);
    static int S_stopVicStream(SubsysCommand *cmd);

    int startVicStream(SubsysCommand *cmd);
    int stopVicStream(SubsysCommand *cmd);

    void calculate_udp_headers();
    void send_udp_packet(uint32_t ip, uint16_t port);
public:
    DataStreamer();
    virtual ~DataStreamer();

    // from ObjectWithMenu
    int fetch_task_items(Path *path, IndexedList<Action*> &item_list);

};

extern DataStreamer dataStreamer;

#endif
