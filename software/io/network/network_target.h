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
#define NET_CMD_GET_RANDOM          0x20
#define NET_CMD_OPEN_TLS            0x21
#define NET_CMD_TLS_TRUST_CA        0x22
#define NET_CMD_TLS_INFO            0x23
#define NET_CMD_START_TLS           0x24
#define NET_CMD_TLS_RELEASE         0x25

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

// The most random bytes GET_RANDOM returns at once, which is what the WiFi
// module hands out per request (RANDOM_MAX_BYTES in software/wifi/common/rpc_shared.h;
// network_target.cc and wifi_random.cc assert the three agree).
#define NET_MAX_RANDOM 512
// TLS sockets: the WiFi module runs the session, this target
// keeps the TCP socket and carries the ciphertext. U64 only; elsewhere the
// commands exist and answer that TLS is not available. The target with the WiFi
// module is the Nios II one (wifi_tls.cc is not built for the others).
#if (U64 == 1) && defined(NIOS)
#define NET_TLS 1
#else
#define NET_TLS 0
#endif

#define NET_MAX_TLS        2    // the module's cap (TLS_MAX_SESSIONS), asserted in network_target_tls.cc
#define NET_TLS_CT_MAX     1536 // ciphertext into one step
#define NET_TLS_PT_MAX     1024 // one step's output
#define NET_TLS_CA_MAX     1472 // one DER certificate
#define NET_TLS_STICKY_MAX 48   // the longest status a failed session keeps

#if NET_MAX_SOCKET_READ > NET_CMD_BUFSIZE
#error "the socket read buffer cannot hold the largest accepted read"
#endif

#if NET_MAX_RANDOM > NET_MAX_REPLY_BLOCK
#error "random bytes must fit a single reply block"
#endif
// One TLS socket: the TCP socket, which is also in sockets[], and the session
// on the module that runs over it.
struct TlsSocket {
    bool used;
    int socketnr;
    uint8_t slot;            // on the module; the index of this entry
    uint16_t tag;            // with slot, names the session on the module
    bool failed;             // sticky: every READ and WRITE answers `sticky` until CLOSE
    char sticky[NET_TLS_STICKY_MAX];
    bool claimed;            // any network command but $23 $FF and $25 claims every entry
    bool module_live;        // false once the module freed the session: CLOSE sends no RPC
    int rx_err;              // the first receive error, latched; 0 = none, no receive after it
    bool write_saw_rx_err;   // a WRITE reported rx_err: the stream ends in that 12
    bool rx_fin;             // TCP said end of stream; no receive after it
    bool module_more;        // the last step handed something out: the next READ steps at once
    uint8_t end;             // how the stream ends once rx is empty (TLS_END_*)
    uint8_t quiet;           // ct_in_len-0 drain steps in a row that changed nothing, across READs
    uint16_t rx_off, rx_len; // plaintext received, not yet read by the C64
    uint16_t held_len;       // ciphertext received, not yet taken by the module
    // What OPEN answered with, for TLS_INFO.
    uint16_t version, suite, group;
    uint8_t trust, bits;
    uint8_t spki[32];
    uint32_t handshake_ms;
    uint8_t rx[NET_TLS_PT_MAX];
    uint8_t held[NET_TLS_CT_MAX];
};

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

#if NET_TLS
    TlsSocket tls[NET_MAX_TLS];
    uint16_t tls_last_tag;
    bool tls_generation_known;
    uint32_t tls_generation;
    uint8_t ca_state;        // CA_EMPTY, CA_UPLOADING or CA_LOADED
    uint16_t ca_tag;         // never 0; a new one whenever the CA changes
    uint16_t ca_len;
    uint16_t ca_total;
    uint8_t ca_buf[NET_TLS_CA_MAX];
    // The last global STATS that answered, for TLS_INFO $FF: kept through a
    // STATS that fails for a busy module, so that one slow RPC does not flip
    // the ready bits; dropped when the module is gone, has no TLS, or restarts.
    bool info_valid;
    uint32_t info_generation;
    uint8_t info_ready;      // ready bits 0-2 as that STATS gave them
    uint16_t info_block_max_ms;
    uint8_t tls_ct_out[NET_TLS_CT_MAX];
    uint8_t tls_pt[NET_TLS_PT_MAX];

    void tls_init(void);
    void tls_before_command(Message *command);
    void tls_reset(void);
    TlsSocket *tls_find(int socketnr);
    TlsSocket *tls_reserve(void);
    int tls_in_use(bool unclaimed_only);
    uint16_t tls_new_tag(void);
    void tls_new_ca_tag(void);
    void tls_check_generation(void);
    void tls_fail(TlsSocket *e, const char *text);
    void tls_fail_module(TlsSocket *e, bool cleanup);
    void tls_fail_step(TlsSocket *e, int esp_err, uint8_t alert_in);
    void tls_close_entry(TlsSocket *e, bool send_notify);
    void tls_cleanup_close(TlsSocket *e);
    void tls_abandon(TlsSocket *e);
    int tls_run_step(TlsSocket *e, void *step, uint32_t deadline, bool use_deadline);
    int tls_send_all(int sock, const uint8_t *data, int length, uint32_t deadline);
    bool tls_wait(int sock, bool for_write, uint32_t until);
    int tls_resolve(const char *host, uint32_t deadline, uint32_t *address);
    int tls_connect(int sock, uint32_t address, uint16_t port, uint32_t deadline);
    Message *tls_prepare(TlsSocket *e, uint8_t trust, uint8_t flags, const uint8_t *pin, const char *name,
                         int name_len, uint32_t total, bool *keep);
    Message *tls_handshake(TlsSocket *e, uint32_t total);
    Message *tls_classify(TlsSocket *e, void *step);
    void tls_reply_open(TlsSocket *e, Message **reply);
    void tls_read(TlsSocket *e, uint32_t length, Message **reply, Message **status);
    void tls_write(TlsSocket *e, const uint8_t *data, int length, Message **reply, Message **status);
    void tls_end(TlsSocket *e, Message **reply, Message **status);
#endif
    Message *set_status(const char *text);
    Message *tls_parse_open(Message *command, int fixed, bool start, uint8_t *trust, uint8_t *flags,
                            const uint8_t **pin, const char **host, int *host_len, const char **name, int *name_len);
    void open_tls(Message *command, Message **reply, Message **status);
    void start_tls(Message *command, Message **reply, Message **status);
    void trust_ca(Message *command, Message **reply, Message **status);
    void tls_info(Message *command, Message **reply, Message **status);
    void tls_release(Message *command, Message **reply, Message **status);

    bool track_socket(int socketnr);
    void untrack_socket(int socketnr);
    bool is_datagram_socket(int socketnr);
    bool owns_socket(int socketnr);
    void close_all_sockets(void);

    void open_socket(Message *command, Message **reply, Message **status, int);
    void read_socket(Message *command, Message **reply, Message **status);
    void write_socket(Message *command, Message **reply, Message **status);
    void close_socket(Message *command, Message **reply, Message **status);
    void get_random(Message *command, Message **reply, Message **status);
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
