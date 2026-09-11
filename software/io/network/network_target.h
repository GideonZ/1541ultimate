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
// 0x12 to 0x15 are taken by the TCP listener commands of the third party
// ultimateii-dos-lib, whose samples send them, so the chunked write takes the
// first code above those rather than a free looking one below them.
#define NET_CMD_WRITE_SOCKET_CHUNK  0x16

#define NET_CMD_BUFSIZE 2048

// Room for every socket lwip can hand out, so the table holds whatever this
// target opens and no client ever meets a limit of its own. lwip's NUM_SOCKETS
// is MEMP_NUM_NETCONN, which is 16 in software/network/config/lwipopts.h;
// network_target.cc checks that the two still agree. See #808.
#define NET_MAX_SOCKETS 16

// The largest payload READ_SOCKET accepts, which is the largest UDP payload
// that can reach the device: 1500 bytes of Ethernet MTU less 20 bytes of IPv4
// header and 8 bytes of UDP header. IP_REASSEMBLY is 0 in
// software/network/config/lwipopts.h, so a datagram above this arrives as
// fragments that are dropped before any socket sees them.
#define NET_MAX_SOCKET_READ 1472

// The largest payload a chunked write accepts, which is the largest UDP payload
// that can leave: IP_FRAG is 0 in software/network/config/lwipopts.h. Stream
// sockets are held to it too, having no framing for chunking to preserve.
#define NET_MAX_SOCKET_WRITE 1472

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

#if NET_MAX_SOCKET_WRITE > NET_CMD_BUFSIZE
#error "the socket write buffer cannot hold the largest accepted write"
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

    // A chunked write accumulates in `buffer` until the announced total has
    // arrived, so a payload too large for one command still leaves as one
    // datagram. write_offset bytes of write_total are in, for write_handle.
    uint8_t write_handle;
    int write_total;
    int write_offset;

    // The sockets this target opened, oldest first. It reads, writes and
    // closes only these, so a stale handle cannot reach a socket the firmware
    // opened for itself. Any command handing out a socket must track it here.
    int sockets[NET_MAX_SOCKETS];
    int socket_count;

    bool track_socket(int socketnr);
    void untrack_socket(int socketnr);
    bool owns_socket(int socketnr);
    void close_all_sockets(void);

    void open_socket(Message *command, Message **reply, Message **status, int);
    void read_socket(Message *command, Message **reply, Message **status);
    void write_socket(Message *command, Message **reply, Message **status);
    void write_socket_chunk(Message *command, Message **reply, Message **status);
    void send_to_socket(int socketnr, uint8_t *src, int length, Message **reply, Message **status);
    void close_socket(Message *command, Message **reply, Message **status);
    void start_read_reply(int payload_length, Message *result, Message **reply, Message **status);
    void discard_read_reply(void);
    void discard_write_chunk(void);
public:
	NetworkTarget(int id);
	virtual ~NetworkTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);
    void abort(int a);
    void c64_reset(void);
};

#endif /* IO_NETWORK_NETWORK_TARGET_H_ */
