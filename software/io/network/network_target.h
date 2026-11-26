/*
 * network_target.h
 *
 *  Created on: Jul 22, 2015
 *      Author: Gideon
 */

#ifndef IO_NETWORK_NETWORK_TARGET_H_
#define IO_NETWORK_NETWORK_TARGET_H_

#include "command_intf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

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

// The largest reply block the command transport can deliver.
#define NET_MAX_REPLY_BLOCK CMD_MAX_REPLY_BLOCK

// Payload bytes in the first block, which also carries the two byte header.
#define NET_FIRST_BLOCK_PAYLOAD (NET_MAX_REPLY_BLOCK - 2)

#if NET_MAX_SOCKET_READ > NET_CMD_BUFSIZE
#error "the socket read buffer cannot hold the largest accepted read"
#endif

// Commands the worker may hold: the one the C64 waits for, and ones that were aborted
// while they still run or wait. With none free, a command fails at once.
#define NET_JOBS 4

// How long a WRITE goes on trying while the link has no free buffer (ENOMEM).
#define NET_SEND_NOMEM_MS 1000

// One command as the worker runs it, with a copy of its bytes.
struct NetJob {
    bool in_use;
    bool done;              // set by the worker; with cancel, under a critical section
    volatile bool cancel;   // set by the UCI Server task
    uint32_t ticket;
    int opened;             // the socket an OPEN made, -1 if none
    Message command;
    uint8_t bytes[CMD_MAX_COMMAND_LEN + 1];
    Message *reply;
    Message *status;
};

// All commands, and the closing of the sockets at a C64 reset, run in order on one worker
// task, which owns the sockets: the UCI Server task never waits for the network, and no
// socket is touched by two tasks. The reply state below (data_message, status_message,
// buffer, read_*) and the socket table belong to the worker. The UCI Server task reads the
// reply state only once the worker has finished the command, as the reply and its Data
// More blocks are handed out; the worker starts no other command meanwhile.
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

    bool track_socket(int socketnr);
    void untrack_socket(int socketnr);
    bool is_datagram_socket(int socketnr);
    bool owns_socket(int socketnr);
    void close_all_sockets(void);

    void open_socket(Message *command, Message **reply, Message **status, int);
    void read_socket(Message *command, Message **reply, Message **status);
    void write_socket(Message *command, Message **reply, Message **status);
    void close_socket(Message *command, Message **reply, Message **status);
    void start_read_reply(int payload_length, Message *result, Message **reply, Message **status);
    void discard_read_reply(void);

    NetJob jobs[NET_JOBS];
    NetJob *job;                    // the command the C64 waits for (UCI Server task)
    QueueHandle_t queue;
    TaskHandle_t worker;
    volatile uint32_t resets;       // C64 resets seen by the UCI Server task
    uint32_t resets_closed;         // the count the worker last closed the sockets for
    volatile bool *cancel;          // the running command's (worker)
    int opened_socket;              // set by open_socket (worker)
    volatile int orphan;            // a socket an aborted OPEN made, for the worker to close
    void wake_worker(void);
    bool start_worker(void);
    void cancel_job(void);
    void close_sockets_after_reset(void);
    void close_orphan(void);
    static void worker_task(void *a);
    void execute(Message *command, Message **reply, Message **status);

    Message quick_message;          // the reply of a socket-free command (UCI Server task)
    uint8_t quick_bytes[16];
    void answer_quick(Message *command, Message **reply, Message **status);
public:
	NetworkTarget(int id);
	virtual ~NetworkTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void deferred_reply(Message **reply, Message **status);
    bool is_quick(Message *command);
    void get_more_data(Message **reply, Message **status);
    void abort(int a);
    void c64_reset(void);
};

#endif /* IO_NETWORK_NETWORK_TARGET_H_ */
