#include "data_streamer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "profiler.h"
#include "u64.h"
#include "network_interface.h"
#include "socket.h"
#include "netdb.h"
#include "userinterface.h"

extern "C" {
#include "dump_hex.h"
}

DataStreamer :: DataStreamer()
{
    my_ip = 0;
    vic_dest_ip = 0;
    vic_dest_port = 0;
    vic_enable = 0;
    memset(vic_dest_mac, 0xFF, 6);
}

// This should never be called
DataStreamer :: ~DataStreamer()
{

}

int DataStreamer :: S_startVicStream(SubsysCommand *cmd)
{
    DataStreamer *str = (DataStreamer *)cmd->functionID;
    return str->startVicStream(cmd);
}

int DataStreamer :: S_stopVicStream(SubsysCommand *cmd)
{
    DataStreamer *str = (DataStreamer *)cmd->functionID;
    return str->stopVicStream(cmd);
}

int DataStreamer :: startVicStream(SubsysCommand *cmd)
{
    if (!cmd->user_interface) {
        return -1;
    }

    if(NetworkInterface :: getNumberOfInterfaces() < 1) {
        cmd->user_interface->popup("No Network Interface", BUTTON_OK);
        return -2;
    }
    NetworkInterface *intf = NetworkInterface :: getInterface(0);

    if (!intf) {
        return -3; // shouldn't happen
    }

    union {
        uint32_t ipaddr32[3];
        uint8_t ipaddr[12];
    } ip;

    uint8_t his_mac[6];
    bzero(his_mac, 6);
    intf->getIpAddr(ip.ipaddr);
    intf->getMacAddr(my_mac);
    my_ip = ip.ipaddr32[0];

    if (!(intf->is_link_up()) || (my_ip == 0)) {
        cmd->user_interface->popup("No (valid) link", BUTTON_OK);
        return -4;
    }

    printf("** Interface is up with valid IP. %08X (MAC: %b:%b:%b:%b:%b:%b)\n", my_ip,
            my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);

    static char *dest_ip = "239.0.1.64:11000\0\0\0\0\0\0\0\0\0\0\0\0";

    if (cmd->user_interface->string_box("Send to...", dest_ip, 26) < 0) {
        return -5;
    }

    int len = strlen(dest_ip);
    if (!len) {
        return -6;
    }

    char *behind_colon = NULL;
    for(int i=0;i<len;i++) {
        if (dest_ip[i] == ':') {
            behind_colon = dest_ip + i + 1;
            dest_ip[i] = 0;
            break;
        }
    }

    int error;
    struct hostent my_host, *ret_host;
    char buffer[128];
    int result = gethostbyname_r(dest_ip, &my_host, buffer, 128, &ret_host, &error);

    if (!ret_host) {
        cmd->user_interface->popup("Host could not be resolved.", BUTTON_OK);
        return -9;
    }
    uint32_t *addrs = (uint32_t *)*(ret_host->h_addr_list);

    vic_dest_ip = addrs[0];

    uint32_t query_ip = vic_dest_ip;
    if ((vic_dest_ip & ip.ipaddr32[1]) != (ip.ipaddr32[2] & ip.ipaddr32[1])) {
        printf("You requested an external IP address (%08X)\n", vic_dest_ip);
        query_ip = ip.ipaddr32[2];
    }

/*
    struct ip_addr vic_dest;
    vic_dest.addr = inet_addr(dest_ip);
    vic_dest_ip = vic_dest.addr;
*/

    vic_dest_port = 11000;
    if (behind_colon) {
        sscanf(behind_colon, "%d", &vic_dest_port);
    }
    if (!vic_dest_port) {
        return -7;
    }

    // destination mac
    if ((vic_dest_ip & 0x000000F8) == 0x000000E8) {
        printf("** User requested Multicast stream\n");
    } else if (vic_dest_ip == 0xFFFFFFFF) {
        printf("** User requested Broadcast stream\n");
    } else {
        send_udp_packet(query_ip, vic_dest_port);
        bool ok = false;
        for(int i=0;i<10;i++) {
            if (intf->peekArpTable(query_ip, vic_dest_mac)) {
                ok = true;
                break;
            }
            vTaskDelay(50);
        }
        if (ok) {
            printf("** Your MAC address is %b:%b:%b:%b:%b:%b. Got ya!\n", vic_dest_mac[0], vic_dest_mac[1], vic_dest_mac[2],
                    vic_dest_mac[3], vic_dest_mac[4], vic_dest_mac[5]);
        } else {
            cmd->user_interface->popup("Cannot find MAC of specified host.", BUTTON_OK);
            return -8;
        }
    }
    vic_enable = 1;

    // start stream!
    calculate_udp_headers();

    return 0;
}

int DataStreamer :: stopVicStream(SubsysCommand *cmd)
{
    vic_enable = 0;
    calculate_udp_headers();
    return 0;
}

int  DataStreamer :: fetch_task_items(Path *path, IndexedList<Action*> &item_list)
{
    item_list.append(new Action("Start VIC Stream", DataStreamer :: S_startVicStream, (int)this, 0));
    item_list.append(new Action("Stop VIC Stream", DataStreamer :: S_stopVicStream, (int)this, 0));
    return 2;
}

void DataStreamer :: send_udp_packet(uint32_t ip, uint16_t port)
{
    int sockfd;
    static struct sockaddr_in server;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        printf("Error opening socket to send UDP packet\n");
        return;
    }

    bzero((char*)&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = ip;
    server.sin_port = htons(port);

    uint8_t buffer[2] = { 0, 0 };
    printf("Send UDP data...\n");
    if (sendto(sockfd, buffer, 2, 0, (const struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Error in sendto()\n");
    }
    // close the socket again
    lwip_close(sockfd);
}


void DataStreamer :: calculate_udp_headers()
{
    uint8_t *hardware = (uint8_t *)U64_UDP_BASE;

    uint8_t header[42] = { 0x01, 0x00, 0x5E, 0x00, 0x01, 0x40, // destination MAC
                           0x00, 0x15, 0x41, 0xAA, 0xAA, 0x01, // source MAC
                           0x08, 0x00, // ethernet type = IP
                           // Ethernet Header (offset 14)
                           0x45, 0x00,
                           0x00, 0x1C, // 16, length = 28, but needs to be updated
                           0xBB, 0xBB, // 18, ID // sum = 100e5 => 00e6
                           0x40, 0x00, // 20, fragmenting and stuff => disable, no offset
                           0x03, 0x11, // 22, TTL, protocol = UDP
                           0x00, 0x00, // 24, checksum to be filled in
                           0xEF, 0x00, 0x01, 0x40, // 26, source IP
                           0xC0, 0xA8, 0x01, 0x40, // 30, destination IP
                           // UDP header
                           0xD0, 0x00, // 34, source port (53248 = VIC)
                           0x2A, 0xF8, // 36, destination port (11000)
                           0x00, 0x08, // 38, Length of UDP = 8 + payload length
                           0x00, 0x00 }; // 40, unused checksum

    if ((my_ip == 0) || (vic_enable == 0)) {
        hardware[63] = 0; // disable
        return;
    }

    printf("__Calculate UDP Headers__\nIP = %08x, Dest = %08x:%d, Enable = %d\n", my_ip, vic_dest_ip, vic_dest_port, vic_enable);

    // Source MAC
    memcpy(header+6, my_mac, 6);

    // Source IP
    header[29] = (uint8_t)(my_ip >> 24);
    header[28] = (uint8_t)(my_ip >> 16);
    header[27] = (uint8_t)(my_ip >> 8);
    header[26] = (uint8_t)(my_ip >> 0);

    // Destination IP
    header[33] = (uint8_t)(vic_dest_ip >> 24);
    header[32] = (uint8_t)(vic_dest_ip >> 16);
    header[31] = (uint8_t)(vic_dest_ip >> 8);
    header[30] = (uint8_t)(vic_dest_ip >> 0);

    // destination mac
    if ((vic_dest_ip & 0x000000F8) == 0x000000E8) {
        header[3] = header[31] & 0x7F;
        header[4] = header[32];
        header[5] = header[33];
    } else if(vic_dest_ip == 0xFFFFFFFF) {
        memset(header, 0xFF, 6);
    } else {
        memcpy(header, vic_dest_mac, 6);
    }

    // Destination port
    header[36] = (uint8_t)(vic_dest_port >> 8);
    header[37] = (uint8_t)(vic_dest_port & 0xFF);

    // Now recalculate the IP checksum
    uint32_t sum = 0;
    for (int i=0; i < 10; i++) {
        uint16_t temp = ((uint16_t)header[2*i + 14]) << 8;
        temp |= header[2*i + 15];
        sum += temp;
    }
    // one's complement fix up
    while(sum & 0xFFFF0000) {
        sum += (sum >> 16);
        sum &= 0xFFFF;
    }
    // write back
    header[24] = 0xFF ^ (uint8_t)(sum >> 8);
    header[25] = 0xFF ^ (uint8_t)sum;

    // copy to hw
    for(int i=0;i<42;i++) {
        hardware[i] = header[i];
    }

    dump_hex_relative(header, 42);

    // enable
    hardware[63] = 1;
}

DataStreamer dataStreamer;
