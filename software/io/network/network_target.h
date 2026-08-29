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
#define NET_CMD_CLOSE_SOCKET        0x09
#define NET_CMD_READ_SOCKET         0x10
#define NET_CMD_WRITE_SOCKET        0x11

#define NET_CMD_BUFSIZE 2048

// How many sockets one client can hold. Opening past this is refused with 85,
// as an exhausted lwip pool already answers, so a leak shows on the open that
// caused it rather than later. Same shape as TELNET_MAX_SESSIONS. See #808.
#define NET_MAX_SOCKETS 4

// The largest payload READ_SOCKET accepts, which is the largest UDP payload
// that can reach the device: 1500 bytes of Ethernet MTU less 20 bytes of IPv4
// header and 8 bytes of UDP header. IP_REASSEMBLY is 0 in
// software/network/config/lwipopts.h, so a datagram above this arrives as
// fragments that are dropped before any socket sees them.
#define NET_MAX_SOCKET_READ 1472

// The largest reply block the transport can deliver. The FPGA stops the
// response pointer on the last byte of the response buffer while it still
// reports data available, so a block of exactly CMD_MAX_REPLY_LEN never ends
// and a client that reads on that flag never stops. See
// fpga/io/command_interface/vhdl_source/command_protocol.vhd.
#define NET_MAX_REPLY_BLOCK (CMD_MAX_REPLY_LEN - 1)

// Payload bytes in the first block, which also carries the two byte header.
#define NET_FIRST_BLOCK_PAYLOAD (NET_MAX_REPLY_BLOCK - 2)

#if NET_MAX_SOCKET_READ > NET_CMD_BUFSIZE
#error "the socket read buffer cannot hold the largest accepted read"
#endif

class NetworkTarget : public CommandTarget {
    Message data_message;
    Message status_message;
    uint8_t buffer[NET_CMD_BUFSIZE];

    // A socket read whose payload does not fit one reply block is handed out
    // over several, through the command interface's Data More mechanism. The
    // socket receive happens once, while READ_SOCKET runs, so a later block
    // never touches the socket again and cannot consume the next datagram.
    // These describe what is left of that one payload in `buffer`:
    // read_offset bytes of read_total have been placed in a block already, and
    // read_status is the result to report on the block that ends the reply.
    // `buffer` is shared with open_socket's name resolution, which is safe
    // because parse_command drops any pending reply before it runs a command.
    int read_total;
    int read_offset;
    Message *read_status;

    // The sockets this target opened, oldest first. It reads, writes and
    // closes only these, so a stale handle cannot reach a socket the firmware
    // opened for itself. Any command handing out a socket must track it here.
    int sockets[NET_MAX_SOCKETS];
    int socket_count;

    void track_socket(int socketnr);
    void untrack_socket(int socketnr);
    bool owns_socket(int socketnr);
    void close_all_sockets(void);

    void open_socket(Message *command, Message **reply, Message **status, int);
    void read_socket(Message *command, Message **reply, Message **status);
    void write_socket(Message *command, Message **reply, Message **status);
    void close_socket(Message *command, Message **reply, Message **status);
    void start_read_reply(int payload_length, Message *result, Message **reply, Message **status);
    void discard_read_reply(void);
public:
	NetworkTarget(int id);
	virtual ~NetworkTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);
    void abort(int a);
    void c64_reset(void);
};

#endif /* IO_NETWORK_NETWORK_TARGET_H_ */
