/*
 * network_target.h
 *
 *  Created on: Jul 22, 2015
 *      Author: Gideon
 */

#ifndef IO_NETWORK_NETWORK_TARGET_H_
#define IO_NETWORK_NETWORK_TARGET_H_

#include "command_intf.h"

#define NET_CMD_IDENTIFY            0x01
#define NET_CMD_GET_INTERFACE_COUNT 0x02
#define NET_CMD_SET_INTERFACE       0x03
#define NET_CMD_GET_NETADDR         0x04
#define NET_CMD_GET_IPADDR          0x05
#define NET_CMD_SET_IPADDR          0x06
#define NET_CMD_OPEN_TCP	        0x07
#define NET_CMD_OPEN_UDP	        0x08
#define NET_CMD_CLOSE		        0x09
#define NET_CMD_READ_SOCKET         0x10
#define NET_CMD_WRITE_SOCKET        0x11

#define NET_CMD_BUFSIZE 2048

class NetworkTarget : public CommandTarget {
    Message data_message;
    Message status_message;
    uint8_t buffer[NET_CMD_BUFSIZE];
    void open_socket(Message *command, Message **reply, Message **status, int);
    void read_socket(Message *command, Message **reply, Message **status);
    void write_socket(Message *command, Message **reply, Message **status);
public:
	NetworkTarget(int id);
	virtual ~NetworkTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);
    void abort(void);
};

#endif /* IO_NETWORK_NETWORK_TARGET_H_ */
