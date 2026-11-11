// Register map for the host harness: the UCI command interface registers live in
// a test-controlled array, so the real command_intf.h macros (CMD_IF_STATUSBYTE
// and friends) read and write test variables.
#ifndef UCI_STUB_IOMAP_H
#define UCI_STUB_IOMAP_H
#include <stdint.h>
extern volatile uint8_t uci_fake_cmdif_regs[0x1000];
#define CMD_IF_BASE ((uintptr_t)uci_fake_cmdif_regs)
#define ITU_BASE 0
#define ioRead8(x) ((uint8_t)0)
#define SID_ENABLE_LEFT  0
#define SID_ENABLE_RIGHT 0
#define SID_BASE_LEFT    0
#define SID_BASE_RIGHT   0
#endif
