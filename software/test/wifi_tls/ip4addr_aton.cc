// lwIP's ip4addr_aton (software/lwip/src/core/ipv4/ip4_addr.c at 26a22151): network_target.cc decides what an IPv4
// literal is with it, so the host tests need the same predicate (127.1, 0x7f.0.0.1 and "1.2.3.4 x" included).
// Copyright (c) 2001-2004 Swedish Institute of Computer Science. All rights reserved. (BSD; see software/lwip/COPYING.)
#include <ctype.h>
#include <arpa/inet.h>
#include "lwip/dns.h"

int ip4addr_aton(const char *cp, ip4_addr_t *addr)
{
    uint32_t val;
    uint8_t base;
    char c;
    uint32_t parts[4];
    uint32_t *pp = parts;

    c = *cp;
    for (;;) {
        if (!isdigit((unsigned char)c)) {
            return 0;
        }
        val = 0;
        base = 10;
        if (c == '0') {
            c = *++cp;
            if (c == 'x' || c == 'X') {
                base = 16;
                c = *++cp;
            } else {
                base = 8;
            }
        }
        for (;;) {
            if (isdigit((unsigned char)c)) {
                if ((base == 8) && ((uint32_t)(c - '0') >= 8))
                    break;
                val = (val * base) + (uint32_t)(c - '0');
                c = *++cp;
            } else if (base == 16 && isxdigit((unsigned char)c)) {
                val = (val << 4) | (uint32_t)(c + 10 - (islower((unsigned char)c) ? 'a' : 'A'));
                c = *++cp;
            } else {
                break;
            }
        }
        if (c == '.') {
            if (pp >= parts + 3) {
                return 0;
            }
            *pp++ = val;
            c = *++cp;
        } else {
            break;
        }
    }
    if (c != '\0' && !isspace((unsigned char)c)) {
        return 0;
    }
    switch (pp - parts + 1) {
        case 0:
            return 0;
        case 1:
            break;
        case 2:
            if (val > 0xffffffUL) {
                return 0;
            }
            if (parts[0] > 0xff) {
                return 0;
            }
            val |= parts[0] << 24;
            break;
        case 3:
            if (val > 0xffff) {
                return 0;
            }
            if ((parts[0] > 0xff) || (parts[1] > 0xff)) {
                return 0;
            }
            val |= (parts[0] << 24) | (parts[1] << 16);
            break;
        case 4:
            if (val > 0xff) {
                return 0;
            }
            if ((parts[0] > 0xff) || (parts[1] > 0xff) || (parts[2] > 0xff)) {
                return 0;
            }
            val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
            break;
        default:
            break;
    }
    if (addr) {
        addr->addr = htonl(val);
    }
    return 1;
}
