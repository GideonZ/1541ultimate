#include <stdint.h>
#include "filemanager.h"

// 1. Global Header Structure (24 bytes)
struct pcap_global_header {
    uint32_t magic_number;   // 0xa1b2c3d4 for microsecond resolution
    uint16_t version_major;  // 2
    uint16_t version_minor;  // 4
    int32_t  thiszone;       // GMT to local correction (usually 0)
    uint32_t sigfigs;        // Accuracy of timestamps (usually 0)
    uint32_t snaplen;        // Max length of captured packets (e.g., 65535)
    uint32_t network;        // Data link type (1 for Ethernet)
};

// 2. Packet Header Structure (16 bytes)
struct pcap_packet_header {
    uint32_t ts_sec;         // Timestamp seconds
    uint32_t ts_usec;        // Timestamp microseconds
    uint32_t incl_len;       // Number of octets of packet saved in file
    uint32_t orig_len;       // Actual length of packet on the wire
};

#define PCAP_RAMADDR 0x3000000

class Pcap {
    uint8_t *pointer;
public:
    Pcap()
    {
        pointer = (uint8_t *)PCAP_RAMADDR;
    }
    void init()
    {
        pointer = (uint8_t *)PCAP_RAMADDR;
        struct pcap_global_header gh = {
            .magic_number = 0xa1b2c3d4,
            .version_major = 2,
            .version_minor = 4,
            .thiszone = 0,
            .sigfigs = 0,
            .snaplen = 65535,
            .network = 1 // Ethernet
        };
        memcpy(pointer, &gh, sizeof(gh));
        pointer += sizeof(gh);
    }

    void add_packet(uint8_t *packet, int length, uint32_t stamp)
    {
        uint32_t rounded = uint32_t((length + 3) & ~3);
        struct pcap_packet_header ph = {
            .ts_sec = 0,
            .ts_usec = stamp,
            .incl_len = rounded,
            .orig_len = uint32_t(length),
        };
        ENTER_SAFE_SECTION;
        memcpy(pointer, &ph, sizeof(ph));
        pointer += sizeof(ph);
        memcpy(pointer, packet, rounded);
        pointer += rounded;
        LEAVE_SAFE_SECTION;
    }

    void write_to_file(const char *fn)
    {
        FILE *fp = fopen(fn, "wb");
        if (fp) {
            size_t len = (size_t)pointer - PCAP_RAMADDR;
            void *data = (void *)PCAP_RAMADDR;
            fwrite(data, len, 1, fp);
            fclose(fp);
        }
    }
};
