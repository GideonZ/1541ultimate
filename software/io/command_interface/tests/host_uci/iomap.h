// The command interface's registers for the host tests: each access goes to a model of
// command_protocol.vhd in uci_deferral_test.cpp.
#ifndef HOST_UCI_IOMAP_H
#define HOST_UCI_IOMAP_H

#include <stdint.h>

uint8_t uci_model_read(int reg);
void uci_model_write(int reg, uint8_t value);
uint8_t *uci_model_memory(void);

struct UciRegister {
    int reg;
    operator uint8_t() const { return uci_model_read(reg); }
    void operator=(uint8_t value) const { uci_model_write(reg, value); }
};

#define CMD_IF_BASE ((intptr_t)uci_model_memory())
#define CMD_IF_SLOT_BASE       (UciRegister{ 0x0 })
#define CMD_IF_SLOT_ENABLE     (UciRegister{ 0x1 })
#define CMD_IF_HANDSHAKE_OUT   (UciRegister{ 0x2 })
#define CMD_IF_STATUSBYTE      (UciRegister{ 0x3 })
#define CMD_IF_COMMAND_START   (UciRegister{ 0x4 })
#define CMD_IF_COMMAND_END     (UciRegister{ 0x5 })
#define CMD_IF_RESPONSE_START  (UciRegister{ 0x6 })
#define CMD_IF_RESPONSE_END    (UciRegister{ 0x7 })
#define CMD_IF_STATUS_START    (UciRegister{ 0x8 })
#define CMD_IF_STATUS_END      (UciRegister{ 0x9 })
#define CMD_IF_STATUS_LENGTH   (UciRegister{ 0xA })
#define CMD_IF_IRQMASK         (UciRegister{ 0xB })
#define CMD_IF_RESPONSE_LEN_L  (UciRegister{ 0xC })
#define CMD_IF_RESPONSE_LEN_H  (UciRegister{ 0xD })
#define CMD_IF_COMMAND_LEN_L   (UciRegister{ 0xE })
#define CMD_IF_COMMAND_LEN_H   (UciRegister{ 0xF })
#define CMD_IF_IRQMASK_SET     (UciRegister{ 0x4 })
#define CMD_IF_IRQMASK_CLEAR   (UciRegister{ 0x5 })

#define CAPAB_COMMAND_INTF 1
#define CAPAB_CARTRIDGE 2
#define ITU_IRQ_DISABLE 0
#define ITU_IRQ_ENABLE 1
#define ITU_IRQ_CLEAR 2
#define ITU_INTERRUPT_CMDIF 1
#define ITU_INTERRUPT_RESET 2
static inline uint32_t getFpgaCapabilities(void) { return CAPAB_COMMAND_INTF | CAPAB_CARTRIDGE; }
static inline void ioWrite8(int, uint8_t) { }

#endif
