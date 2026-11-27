// The recorder registers as the tape code sees them. The generated copy of tape_recorder.h
// (see the Makefile) includes this in place of its four RECORD_* macros, which dereference a
// fixed bus address. Status and control share one address on the hardware (a read gives the
// status, a write sets the control, c2n_record.vhd:100-123), the data port is a separate
// address (bit 11 of the address selects it, c2n_record.vhd:102 and :118).
#ifndef TAPE_TESTS_REGS_H
#define TAPE_TESTS_REGS_H
#include <stdint.h>
namespace tapeenv {
uint8_t reg_status();
uint8_t reg_data8();
uint32_t reg_data32();
struct ControlReg { void operator=(uint8_t v); };
extern ControlReg control_reg;
}
#define RECORD_STATUS  (tapeenv::reg_status())
#define RECORD_CONTROL (tapeenv::control_reg)
#define RECORD_DATA    (tapeenv::reg_data8())
#define RECORD_DATA32  (tapeenv::reg_data32())
#endif
