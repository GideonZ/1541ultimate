// Host stand-in for software/network/network_interface.h: what network_target.cc calls.
#ifndef NETWORK_INTERFACE_H_STUB
#define NETWORK_INTERFACE_H_STUB
#include <stdint.h>
class NetworkInterface
{
public:
    static int getNumberOfInterfaces(void) { return 0; }
    static NetworkInterface *getInterface(int i) { return 0; }
    void getIpAddr(uint8_t *a) { }
    void getMacAddr(uint8_t *a) { }
    void setIpAddr(uint8_t *a) { }
};
#endif
