// One interface with fixed addresses, for network_target_test.cpp. It is force-included
// and has the real header's guard, so the real one (next to network_target.cc, which a
// quoted include finds first) is skipped.
#ifndef NETWORK_INTERFACE_H
#define NETWORK_INTERFACE_H
#include <stdint.h>
#include <string.h>
class NetworkInterface
{
public:
    static int getNumberOfInterfaces(void) { return 1; }
    static NetworkInterface *getInterface(int i) { static NetworkInterface one; return i ? 0 : &one; }
    void getIpAddr(uint8_t *a) { static const uint8_t ip[12] = { 10,0,0,2, 255,255,255,0, 10,0,0,1 }; memcpy(a, ip, 12); }
    void getMacAddr(uint8_t *a) { static const uint8_t mac[6] = { 0,1,2,3,4,5 }; memcpy(a, mac, 6); }
    void setIpAddr(uint8_t *) { }
};
#endif
