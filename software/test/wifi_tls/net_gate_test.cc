// The NET_TLS gate of network_target.h for one target. Compiled (not run) by `make gate` in
// target/pc/linux/wifitls with the target's flags; EXPECT_NET_TLS is 1 where the target has the WiFi
// module and its TLS (the Nios II U64, the U64-II), 0 for the RISC-V U64, which builds no wifi_tls.cc.
#include "network_target.h"

#ifndef EXPECT_NET_TLS
#error "EXPECT_NET_TLS is not set"
#endif
static_assert(NET_TLS == EXPECT_NET_TLS, "NET_TLS is not what this target needs");
