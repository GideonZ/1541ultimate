/*
 * rpc_calls.h
 *
 *  Created on: Nov 7, 2021
 *      Author: gideon
 */

#ifndef SOFTWARE_WIFI_SCAN_MAIN_RPC_CALLS_H_
#define SOFTWARE_WIFI_SCAN_MAIN_RPC_CALLS_H_

#include <stdint.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define DEFAULT_SCAN_LIST_SIZE 24

typedef struct {
    uint8_t command;
    uint8_t thread;
    uint16_t sequence;
} rpc_header_t;

#include "rpc_shared.h"

typedef struct {
    rpc_header_t hdr;
    int esp_err; // to be casted to esp_err_t
} rpc_espcmd_resp;

typedef struct {
    rpc_header_t hdr;
} rpc_identify_req;

typedef struct {
    rpc_header_t hdr;
    uint16_t major;
    uint16_t minor;
    char string;
} rpc_identify_resp;

typedef struct {
    rpc_header_t hdr;
    int esp_err; // to be casted to esp_err_t
    uint16_t vbus;
    uint16_t vaux;
    uint16_t v50;
    uint16_t v33;
    uint16_t v18;
    uint16_t v10;
    uint16_t vusb;
} rpc_get_voltages_resp;

typedef struct {
    rpc_header_t hdr;
    int esp_err; // to be casted to esp_err_t
    uint8_t mac[6];
} rpc_getmac_resp;

typedef struct {
    rpc_header_t hdr;
    int esp_err; // to be casted to esp_err_t
    uint8_t status;
} rpc_get_connection_resp;

typedef struct {
    rpc_header_t hdr;
    int baudrate;
    uint8_t flowctrl;
    uint8_t inversions;
} rpc_setbaud_req;

typedef struct {
    rpc_header_t hdr;
    uint8_t auth_mode; // to be casted to wifi_auth_mode_t
    char ssid[32];
    char password[64];
    // The AP picked from the scan list. Added later: a module takes these only
    // from a request this long, and an older module ignores them. All zero (or
    // a shorter request): any AP with the SSID.
    uint8_t bssid[6];
    uint8_t channel;
} rpc_wifi_connect_req;

/* 42 byte AP record */
typedef struct {
    uint8_t bssid[6];            // MAC address of AP
    uint8_t ssid[33];            // SSID of AP
    uint8_t channel;             // channel of AP
    int8_t  rssi;                // signal strength of AP
    uint8_t authmode;            // authmode of AP
} ultimate_ap_record_t;

typedef struct {
    uint8_t num_records;
    uint8_t reserved;
    ultimate_ap_record_t aps[DEFAULT_SCAN_LIST_SIZE];
} ultimate_ap_records_t;

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    ultimate_ap_records_t rec;
} rpc_scan_resp;

typedef struct {
    rpc_header_t hdr;
    u16_t len;
    char data;
} rpc_rx_pkt;

typedef struct {
    rpc_header_t hdr;
    char timezone[128];
} rpc_get_time_req;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} esp_datetime_t;

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    esp_datetime_t datetime;
} rpc_get_time_resp;

//----------------------------------
//----------------------------------
// TLS sessions, named by (slot, tag): the ESP32 runs the engine, the Ultimate moves the ciphertext. Fixed fields first,
// variable data last; rpc_dispatch.c and wifi_tls.cc assert these layouts at fixed offsets.
#define TLS_MAX_SESSIONS   2
#define TLS_SNI_MAX_LEN    253  // longest DNS name
#define TLS_STEP_IN_MAX    1536 // ct_in_len + pt_in_len per step
#define TLS_STEP_OUT_MAX   1024 // ct_out_len + pt_out_len per step
#define TLS_SLOT_GLOBAL    0xFF // CMD_TLS_STATS without a session (tag 0)
#define TLS_PIN_LEN        32   // SHA-256 of the server's SubjectPublicKeyInfo, digest order
#define TLS_CA_MAX_LEN     1472 // one DER certificate for trust mode CUSTOM
#define TLS_ALERT_NONE     0xFF

typedef enum {
    TLS_TRUST_BUNDLE = 0, // the ESP-IDF certificate bundle
    TLS_TRUST_CUSTOM = 1, // the one CA loaded with CMD_TLS_SET_CA, matched by ca_tag
    TLS_TRUST_PIN    = 2, // the leaf key's SHA-256 only
} tls_trust_t;

#define TLS_OPT_REQUIRE_TLS13 0x01 // rpc_tls_open_req.opts: offer TLS 1.3 only
#define TLS_STEP_NO_READ      0x01 // rpc_tls_step_req.flags: no plaintext out of this step

// rpc_tls_open_resp.reason, telling the ESP_ERR_INVALID_STATE refusals apart
#define TLS_OPEN_REASON_NONE         0
#define TLS_OPEN_REASON_NO_ENTROPY   1 // the radio has not started: no RNG seed
#define TLS_OPEN_REASON_NO_TIME      2 // BUNDLE or CUSTOM, but no trustworthy clock
#define TLS_OPEN_REASON_NO_CUSTOM_CA 3 // CUSTOM, but no CA held under the request's ca_tag

typedef enum {
    TLS_TIME_NONE = 0, // clock not later than the build date
    TLS_TIME_NIOS = 1, // set from unix_time in a CMD_TLS_OPEN (the Ultimate's SNTP time)
    TLS_TIME_SNTP = 2, // already valid without the Ultimate: SNTP over the ESP32's own Wi-Fi (STATS only)
} tls_time_source_t;

typedef enum {
    TLS_STATE_HANDSHAKING = 0,
    TLS_STATE_ESTABLISHED = 1,
    TLS_STATE_CLOSED      = 2,
    TLS_STATE_FAILED      = 3, // esp_err holds the mbedTLS error code, fail_reason says more
} tls_state_t;

// rpc_tls_step_resp.fail_reason, set with FAILED
typedef enum {
    TLS_FAIL_NONE             = 0, // generic: esp_err has the mbedTLS code (alert_in the alert, if any)
    TLS_FAIL_VERSION_REFUSED  = 1, // the ServerHello chose a version not offered; it is in verify_flags' low 16 bits
    TLS_FAIL_DOWNGRADE        = 2, // TLS 1.2 chosen by a ServerHello carrying the RFC 8446 downgrade sentinel
    TLS_FAIL_NO_EMS           = 3, // a TLS 1.2 server without the extended master secret
    TLS_FAIL_CERT_VERIFY      = 4, // verify_flags = mbedtls_ssl_get_verify_result(), 0xFFFFFFFF if it never completed
    TLS_FAIL_PIN_MISMATCH     = 5, // PIN: the leaf key is not the pinned one
    TLS_FAIL_PEER_CLOSED      = 6, // close_notify during the handshake
} tls_fail_reason_t;

// rpc_tls_set_ca_resp.reject, with ESP_ERR_INVALID_ARG
#define TLS_CA_REJECT_NONE          0
#define TLS_CA_REJECT_RSA_KEY_SHORT 1 // RSA key below 2048 bits
#define TLS_CA_REJECT_RESERVED      2 // never sent
#define TLS_CA_REJECT_CURVE         3 // EC key on a curve the X.509 profile does not accept
#define TLS_CA_REJECT_NOT_CA        4 // v3 without basicConstraints cA, or a keyUsage without keyCertSign

typedef struct {
    rpc_header_t hdr;
    uint8_t trust;        // tls_trust_t
    uint8_t sni_len;      // 0..TLS_SNI_MAX_LEN, no terminator; 0 = no name (PIN only)
    uint8_t slot;         // 0..TLS_MAX_SESSIONS-1, chosen by the Ultimate
    uint8_t time_source;  // TLS_TIME_NONE, or TLS_TIME_NIOS when unix_time is the Ultimate's SNTP time
    uint32_t unix_time;   // UTC seconds
    uint16_t tag;         // names the session with slot; never 0
    uint8_t opts;         // TLS_OPT_*
    uint8_t reserved;
    uint16_t ca_tag;      // CUSTOM: the Ultimate's current ca_tag; 0 otherwise
    uint16_t reserved2;
    uint8_t pin[TLS_PIN_LEN]; // read only when trust is PIN
    char sni[];
} rpc_tls_open_req;

typedef struct {
    rpc_header_t hdr;
    int esp_err;     // ESP_OK, an esp_err_t, or a negative mbedTLS code
    uint8_t slot;    // TLS_SLOT_GLOBAL when esp_err != ESP_OK
    uint8_t reason;  // TLS_OPEN_REASON_*
    uint16_t reserved; // 0
} rpc_tls_open_resp;

typedef struct {
    rpc_header_t hdr;
    uint8_t slot;
    uint8_t flags;   // TLS_STEP_NO_READ
    uint16_t tag;
    uint16_t ct_in_len;
    uint16_t pt_in_len;
    uint8_t data[];  // ct_in, then pt_in
} rpc_tls_step_req;

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    uint8_t state;         // tls_state_t
    uint8_t alert_in;      // fatal alert received, TLS_ALERT_NONE if none
    uint16_t ct_out_len;
    uint16_t pt_out_len;
    uint8_t alert_out;     // alert this engine sent unencrypted, TLS_ALERT_NONE if none (or encrypted)
    uint8_t fail_reason;   // tls_fail_reason_t, with FAILED
    uint32_t verify_flags; // see tls_fail_reason_t
    uint8_t data[];        // ct_out, then pt_out
} rpc_tls_step_resp;

typedef struct {
    rpc_header_t hdr;
    uint8_t slot;
    uint8_t reserved;
    uint16_t tag;
} rpc_tls_session_req; // CMD_TLS_CLOSE and CMD_TLS_STATS

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    uint16_t ct_out_len;
    uint16_t reserved;
    uint8_t data[];  // the close_notify record
} rpc_tls_close_resp;

typedef struct {
    rpc_header_t hdr;
    int esp_err;
    uint32_t heap_free;     // internal 8-bit capable heap
    uint32_t heap_min;      // low-water mark since the OPEN that found no session open
    uint32_t largest_block;
    uint32_t ct_in_bytes;   // session counters, 0 for TLS_SLOT_GLOBAL
    uint32_t ct_out_bytes;
    uint32_t pt_in_bytes;
    uint32_t pt_out_bytes;
    uint32_t handshake_us;  // ESP32 time spent in handshake steps
    uint16_t tls_version;   // 0x0304 for TLS 1.3
    uint16_t ciphersuite;   // IANA id
    uint16_t group;         // IANA NamedGroup, never 0 once ESTABLISHED
    uint8_t state;          // tls_state_t
    uint8_t sessions_open;
    uint32_t stack_free;    // TLS task stack high-water mark, bytes
    uint32_t esp_time;      // ESP32 clock at the session's OPEN (now, for TLS_SLOT_GLOBAL), 0 = no time
    uint8_t time_source;    // tls_time_source_t of that clock
    uint8_t entropy_source; // entropy_source_t, now
    uint8_t ems;            // 1: TLS 1.2 with the extended master secret
    uint8_t reserved;
    uint16_t block_max_ms;  // longest dispatcher iteration since boot
    uint16_t reserved2;
    uint8_t spki_sha256[32]; // SHA-256 of the leaf's SubjectPublicKeyInfo, digest order
} rpc_tls_stats_resp;

typedef struct {
    rpc_header_t hdr;
    uint16_t der_len; // 1..TLS_CA_MAX_LEN
    uint16_t ca_tag;  // never 0
    uint8_t der[];
} rpc_tls_set_ca_req;

typedef struct {
    rpc_header_t hdr;
    int esp_err;     // ESP_OK, a negative mbedTLS parse code, or ESP_ERR_INVALID_ARG with a reject reason
    uint8_t reject;  // TLS_CA_REJECT_*
    uint8_t reserved[3];
} rpc_tls_set_ca_resp;

//----------------------------------
// send raw packet
typedef struct {
    rpc_header_t hdr;
    uint32_t length;
    char data;
} rpc_send_eth_req;

typedef struct { // this response might be deprecated soon
    rpc_header_t hdr;
} rpc_send_eth_resp;

// Events
typedef struct {
    rpc_header_t hdr;
    uint32_t ip;       // to be casted to [esp_]ip4_addr_t
    uint32_t netmask;  // to be casted to [esp_]ip4_addr_t
    uint32_t gw;       // to be casted to [esp_]ip4_addr_t
    uint8_t changed;
} event_pkt_got_ip;

typedef struct {
    rpc_header_t hdr;
    uint8_t ssid[32];
} event_pkt_connected;

// EVENT_DISCONNECTED may carry the reason (wifi_event_sta_disconnected_t.reason)
// after the header. 0 and a bare header both mean that it is not known.
typedef struct {
    rpc_header_t hdr;
    uint8_t reason;
} event_pkt_disconnected;

#define CMD_ECHO              0x01
#define CMD_IDENTIFY          0x02
#define CMD_SET_BAUD          0x03
#define CMD_WIFI_SCAN         0x04
#define CMD_WIFI_CONNECT      0x05
#define CMD_WIFI_DISCONNECT   0x06
#define CMD_WIFI_GETMAC       0x07
#define CMD_SEND_PACKET       0x08
#define CMD_MODEM_ON          0x09
#define CMD_MODEM_OFF         0x0A
#define CMD_WIFI_IS_CONNECTED 0x0B
#define CMD_GET_VOLTAGES      0x0C
#define CMD_WIFI_ENABLE       0x0D
#define CMD_WIFI_DISABLE      0x0E
#define CMD_MACHINE_OFF       0x0F
#define CMD_GET_TIME          0x10
#define CMD_CLEAR_APS         0x11
#define CMD_WIFI_AUTOCONNECT  0x12
// 0x13-0x1F are u64ctrl's; 0x20 is CMD_GET_RANDOM, in rpc_shared.h
#define CMD_TLS_OPEN          0x30
#define CMD_TLS_STEP          0x31
#define CMD_TLS_CLOSE         0x32
#define CMD_TLS_STATS         0x33
#define CMD_TLS_CLOSE_ALL     0x34
#define CMD_TLS_SET_CA        0x35

/*
#define CMD_SOCKET          0x11
#define CMD_CONNECT         0x12
#define CMD_ACCEPT          0x13
#define CMD_BIND            0x14
#define CMD_SHUTDOWN        0x15
#define CMD_CLOSE           0x16
#define CMD_LISTEN          0x17
#define CMD_READ            0x18
#define CMD_RECV            0x19
#define CMD_RECVFROM        0x1A
#define CMD_WRITE           0x1B
#define CMD_SEND            0x1C
#define CMD_SENDTO          0x1D
#define CMD_GETHOSTBYNAME   0x21
#define CMD_GETPEERNAME     0x22
#define CMD_GETSOCKNAME     0x23
#define CMD_GETSOCKOPT      0x24
#define CMD_SETSOCKOPT      0x25
#define CMD_IOCTL           0x28
#define CMD_FCNTL           0x29
*/
#define EVENT_CONNECTED     0x40
#define EVENT_GOTIP         0x41
#define EVENT_DISCONNECTED  0x42
#define EVENT_RECV_PACKET   0x43
#define EVENT_RESCAN        0x44
#define EVENT_KEEPALIVE     0x45
#define EVENT_DISABLED      0x46
#define EVENT_RESTARTED     0x47 // the module restarted on its own (radio_boot.h)

#define EVENT_BUTTON        0x80
#define EVENT_FREEZE        0x81
#define EVENT_MENU          0x82
#define EVENT_RESET         0x83

#define EVENT_JOYSTICK      0x90


#endif /* SOFTWARE_WIFI_SCAN_MAIN_RPC_CALLS_H_ */
