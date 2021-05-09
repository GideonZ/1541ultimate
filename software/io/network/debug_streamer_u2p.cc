#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "debug_streamer_u2p.h"
#include "profiler.h"
#include "u2p.h"
#include "network_interface.h"
#include "socket.h"
#include "netdb.h"
#include "userinterface.h"
#include "profiler.h"

extern "C" {
#include "dump_hex.h"
}

#define CFG_STREAM_DEST0    0xD0
#define CFG_STREAM_DEST1    0xD1
#define CFG_STREAM_DEST2    0xD2
#define CFG_STREAM_DEST3    0xD3
#define CFG_STREAM_BUSMODE  0xD4

static const char *modes[] = { "6510 Only", "VIC Only", "6510 & VIC", "1541 Only", "6510 & 1541", "6510 w/IEC", "6510 & VIC w/IEC" };
static const uint8_t modeBytes[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x09, 0x0B, 0x00 };

struct t_cfg_definition stream_cfg[] = {
    { CFG_STREAM_DEST2,         CFG_TYPE_STRING, "Stream Debug to",   "%s", NULL,  0, 36, (int)"239.0.1.66:11002" },
    { CFG_STREAM_BUSMODE,       CFG_TYPE_ENUM,   "Debug Stream Mode", "%s", modes, 0, 6,  0 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,  0,  0, 0 } };


DataStreamer :: DataStreamer()
{
    volatile uint8_t *hardware = (uint8_t *)U2P_DEBUG_ETH;
    (*hardware) = 0;
    if (*hardware) {
		my_ip = 0;
		memset(streams, 0, 4*sizeof(stream_config_t));

		cfg = ConfigManager :: getConfigManager()->register_store(0x44617461, "Data Streams", stream_cfg, NULL);

		for (int i=0; i < 4; i++) {
			timers[i] = xTimerCreate("StreamTimer", 100, pdFALSE, (void *)i, DataStreamer :: S_timer);
		}
	}
}

// This should never be called
DataStreamer :: ~DataStreamer()
{

}

DataStreamer dataStreamer;

int DataStreamer :: S_startStream(SubsysCommand *cmd)
{
    DataStreamer *str = (DataStreamer *)cmd->functionID;
    return str->startStream(cmd);
}

int DataStreamer :: S_stopStream(SubsysCommand *cmd)
{
    DataStreamer *str = (DataStreamer *)cmd->functionID;
    return str->stopStream(cmd);
}

void DataStreamer :: S_timer(TimerHandle_t a)
{
    int streamID = (int)pvTimerGetTimerID(a);
    printf("S_timer %p %d\n", a, streamID);
    if ((streamID >= 0) && (streamID <= 3)) {
        stream_config_t *stream = &dataStreamer.streams[streamID];
        stream->enable = 0;
        dataStreamer.calculate_udp_headers(streamID);
    }
}

int DataStreamer :: startStream(SubsysCommand *cmd)
{
    if(NetworkInterface :: getNumberOfInterfaces() < 1) {
        if (cmd->user_interface) cmd->user_interface->popup("No Network Interface", BUTTON_OK);
        return -2;
    }
    NetworkInterface *intf = NetworkInterface :: getInterface(0);

    if (!intf) {
        return -3; // shouldn't happen
    }

    int streamID = cmd->mode;
    if ((streamID < 0) || (streamID > 3)) {
        if (cmd->user_interface) cmd->user_interface->popup("Invalid Stream ID", BUTTON_OK);
        return -10;
    }
    stream_config_t *stream = &streams[streamID];

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
        if (cmd->user_interface) cmd->user_interface->popup("No (valid) link", BUTTON_OK);
        return -4;
    }

/*
    printf("** Interface is up with valid IP. %08X (MAC: %b:%b:%b:%b:%b:%b)\n", my_ip,
            my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);
*/

    char dest_host[40];

    if ((cmd->path.length() > 0) && (cmd->user_interface == NULL)) { // we were not called from the menu, and a destination name is given in the path.
        strncpy(dest_host, cmd->path.c_str(), 36);
    } else {
        const char *default_host = cfg->get_string(CFG_STREAM_DEST0 + streamID);
        if (default_host) {
            strncpy(dest_host, default_host, 36);
        } else {
        	return -10;
        }
        if (cmd->user_interface) {
            if (cmd->user_interface->string_box("Send to...", dest_host, 36) < 0) {
                return -5;
            }
            cfg->set_string(CFG_STREAM_DEST0 + streamID, dest_host);
            cfg->write();
        }
    }

    int len = strlen(dest_host);
    if (!len) {
        return -6;
    }

    char *behind_colon = NULL;
    for(int i=0;i<len;i++) {
        if (dest_host[i] == ':') {
            behind_colon = dest_host + i + 1;
            dest_host[i] = 0;
            break;
        }
    }

    int error;
    struct hostent my_host, *ret_host;
    char buffer[128];
    int result = gethostbyname_r(dest_host, &my_host, buffer, 128, &ret_host, &error);

    if (!ret_host) {
        if (cmd->user_interface) cmd->user_interface->popup("Host could not be resolved.", BUTTON_OK);
        else printf("Host '%s' could not be resolved.", dest_host);
        return -9;
    }
    uint32_t *addrs = (uint32_t *)*(ret_host->h_addr_list);

    stream->dest_ip = addrs[0];

    uint32_t query_ip = stream->dest_ip;
    if ((stream->dest_ip & ip.ipaddr32[1]) != (ip.ipaddr32[2] & ip.ipaddr32[1])) {
        printf("You requested an external IP address (%08X)\n", stream->dest_ip);
        query_ip = ip.ipaddr32[2];
    }

/*
    struct ip_addr vic_dest;
    vic_dest.addr = inet_addr(dest_ip);
    vic_dest_ip = vic_dest.addr;
*/

    stream->dest_port = 11000 + streamID;
    if (behind_colon) {
        sscanf(behind_colon, "%d", &stream->dest_port);
    }
    if (!stream->dest_port) {
        if (cmd->user_interface) cmd->user_interface->popup("Destination Port cannot be 0.", BUTTON_OK);
        return -7;
    }

    // destination mac
    if ((stream->dest_ip & 0x000000F8) == 0x000000E8) {
        printf("** User requested Multicast stream\n");
    } else if (stream->dest_ip == 0xFFFFFFFF) {
        printf("** User requested Broadcast stream\n");
    } else {
        bool ok = false;
        for(int i=0;i<10;i++) {
            send_udp_packet(query_ip, stream->dest_port);
            vTaskDelay(20);
            if (intf->peekArpTable(query_ip, stream->dest_mac)) {
                ok = true;
                break;
            }
        }
        if (ok) {
            printf("** Your MAC address is %b:%b:%b:%b:%b:%b. Got ya!\n", stream->dest_mac[0], stream->dest_mac[1], stream->dest_mac[2],
                    stream->dest_mac[3], stream->dest_mac[4], stream->dest_mac[5]);
        } else {
            if (cmd->user_interface) cmd->user_interface->popup("Cannot find MAC of specified host.", BUTTON_OK);
            return -8;
        }
    }
    stream->enable = 1;

    // start stream!
    calculate_udp_headers(streamID);

    if (cmd->bufferSize) {
        int stopAfter = cmd->bufferSize;
        cmd->bufferSize = 0;

        if (xTimerChangePeriod(timers[streamID], stopAfter, 50) == pdTRUE) {
            if (xTimerStart(timers[streamID], 50) != pdTRUE) {
                return -12;
            }
            printf("Timer started to stop stream %d after %d ticks.\n", streamID, stopAfter);

        }
    }

    return 0;
}

int DataStreamer :: stopStream(SubsysCommand *cmd)
{
    int streamID = cmd->mode;
    if ((streamID < 0) || (streamID > 3)) {
        return -10;
    }
    stream_config_t *stream = &streams[streamID];
    stream->enable = 0;
    calculate_udp_headers(streamID);
    return 0;
}

void DataStreamer :: create_task_items()
{
    volatile uint8_t *hardware = (uint8_t *)U2P_DEBUG_ETH;
	if (*hardware) {
		TaskCategory *dev = TasksCollection :: getCategory("Developer", 999);

		myActions.startDbg = new Action("  Debug Stream", DataStreamer :: S_startStream, (int)this, 2);
		myActions.stopDbg  = new Action("\023 Debug Stream", DataStreamer :: S_stopStream, (int)this, 2);

		dev->append(myActions.startDbg);
		dev->append(myActions.stopDbg);
	}
}

void DataStreamer :: update_task_items(bool writablePath, Path *path)
{
    volatile uint8_t *hardware = (uint8_t *)U2P_DEBUG_ETH;
	if (*hardware) {
		myActions.startDbg->setHidden(streams[2].enable != 0);
		myActions.stopDbg->setHidden(streams[2].enable == 0);
	}
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


void DataStreamer :: calculate_udp_headers(int id)
{
    uint8_t header[42] = { 0x01, 0x00, 0x5E, 0x00, 0x01, 0x40, // destination MAC
                           0x00, 0x15, 0x41, 0xAA, 0xAA, 0x01, // source MAC
                           0x08, 0x00, // ethernet type = IP
                           // Ethernet Header (offset 14)
                           0x45, 0x00,
                           0x00, 0x1C, // 16, length = 28, but needs to be updated
                           0xBB, 0xBB, // 18, ID // sum = 100e5 => 00e6
                           0x40, 0x00, // 20, fragmenting and stuff => disable, no offset
                           0x40, 0x11, // 22, TTL, protocol = UDP
                           0x00, 0x00, // 24, checksum to be filled in
                           0xEF, 0x00, 0x01, 0x40, // 26, source IP
                           0xC0, 0xA8, 0x01, 0x40, // 30, destination IP
                           // UDP header
                           0xD0, 0x00, // 34, source port (53248 = VIC)
                           0x2A, 0xF8, // 36, destination port (11000)
                           0x00, 0x08, // 38, Length of UDP = 8 + payload length
                           0x00, 0x00 }; // 40, unused checksum

    // The only supported stream is stream #2
    if (id != 2) {
    	return;
    }
    stream_config_t *stream = &streams[id];

    volatile uint8_t *hardware = (uint8_t *)U2P_DEBUG_ETH;
    hardware += 64 * id;

    if ((my_ip == 0) || (stream->enable == 0)) {
        hardware[63] = 0;
        return;
    }


    printf("__Calculate UDP Headers__\nID = %d, IP = %08x, Dest = %08x:%d, Enable = %d\n", id, my_ip, stream->dest_ip, stream->dest_port, stream->enable);

    // source port: 53248, 54272, 6510 or 1541
    switch(id) {
    case 0:
        header[34] = 0xD0; header[35] = 0x00; break;
    case 1:
        header[34] = 0xD4; header[35] = 0x00; break;
    case 2:
        header[34] = 0x19; header[35] = 0x6E; break;
    case 3:
        header[34] = 0x06; header[35] = 0x05; break;
    }

    // Source MAC
    memcpy(header+6, my_mac, 6);

    // Source IP
    header[29] = (uint8_t)(my_ip >> 24);
    header[28] = (uint8_t)(my_ip >> 16);
    header[27] = (uint8_t)(my_ip >> 8);
    header[26] = (uint8_t)(my_ip >> 0);

    // Destination IP
    header[33] = (uint8_t)(stream->dest_ip >> 24);
    header[32] = (uint8_t)(stream->dest_ip >> 16);
    header[31] = (uint8_t)(stream->dest_ip >> 8);
    header[30] = (uint8_t)(stream->dest_ip >> 0);

    // destination mac
    if ((stream->dest_ip & 0x000000F8) == 0x000000E8) {
        header[3] = header[31] & 0x7F;
        header[4] = header[32];
        header[5] = header[33];
    } else if(stream->dest_ip == 0xFFFFFFFF) {
        memset(header, 0xFF, 6);
    } else {
        memcpy(header, stream->dest_mac, 6);
    }

    // Destination port
    header[36] = (uint8_t)(stream->dest_port >> 8);
    header[37] = (uint8_t)(stream->dest_port & 0xFF);

    // Now recalculate the IP checksum
    uint32_t sum = 0;
    for (int i=0; i < 10; i++) {
        uint16_t temp = ((uint16_t)header[2*i + 14]) << 8;
        temp |= header[2*i + 15];
        sum += temp;
    }
    // one's complement fix up
    while(sum & 0xFFFF0000) {
    	sum = (sum >> 16) + (sum & 0xFFFF);
    }

    // write back
    header[24] = 0xFF ^ (uint8_t)(sum >> 8);
    header[25] = 0xFF ^ (uint8_t)sum;

    // copy to hw
    for(int i=0;i<42;i++) {
        hardware[i] = header[i];
    }

    dump_hex_relative(header, 42);

	uint8_t mode = modeBytes[cfg->get_value(CFG_STREAM_BUSMODE)];
    hardware[63] = (mode << 4) | (1 << 2); // enable
}
