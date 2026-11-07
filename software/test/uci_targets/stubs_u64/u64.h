// U64 register surface that control_target.cc uses when built with U64 (the SAVEMEM test).
// The C64 memory window is a plain array. The critical-section and scheduler-suspend calls are
// counted, and C64_DMA_MEMONLY records whether it was set to 1 inside a suspended stretch (the
// copy runs between that write and the one back to 0).
#ifndef UCI_STUB_U64_H
#define UCI_STUB_U64_H
#include <stdint.h>
struct UciFakeU64 {
    int critical;            // portENTER_CRITICAL entries
    int suspended;           // vTaskSuspendAll depth now
    int suspends, resumes;
    int memonly_on_inside;   // C64_DMA_MEMONLY = 1 while suspended
    int memonly_on_outside;  // ... while not
    int copy_flag, begins, ends, begins_suspended, ends_suspended;   // c64_window.h stand-in
    int resumed_with_flag;   // xTaskResumeAll with the copy flag still up
};
extern UciFakeU64 uci_fake_u64;
extern uint8_t uci_fake_u64_window[65536];
struct UciFakeMemonly {
    uint8_t v;
    operator uint8_t() const { return v; }
    UciFakeMemonly &operator=(uint8_t x)
    {
        v = x;
        if (x) { if (uci_fake_u64.suspended) uci_fake_u64.memonly_on_inside++; else uci_fake_u64.memonly_on_outside++; }
        return *this;
    }
};
extern UciFakeMemonly uci_fake_u64_memonly;
#define C64_MEMORY_BASE ((uintptr_t)uci_fake_u64_window)
#define C64_DMA_MEMONLY uci_fake_u64_memonly
#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL() (uci_fake_u64.critical++)
#define portEXIT_CRITICAL()  ((void)0)
#endif
#define vTaskSuspendAll() (uci_fake_u64.suspends++, uci_fake_u64.suspended++)
#define xTaskResumeAll()  (uci_fake_u64.resumes++, uci_fake_u64.copy_flag ? uci_fake_u64.resumed_with_flag++ : 0, uci_fake_u64.suspended--)
// the SID register mirrors the U64 branch of CTRL_CMD_GET_SIDADDR reads
#define C64_SID1_EN_BAK         0
#define C64_SID2_EN_BAK         0
#define C64_SID1_BASE_BAK       0
#define C64_SID2_BASE_BAK       0
#define C64_SID1_MASK_BAK       0
#define C64_SID2_MASK_BAK       0
#define C64_EMUSID1_BASE_BAK    0
#define C64_EMUSID2_BASE_BAK    0
#define C64_EMUSID1_MASK_BAK    0
#define C64_EMUSID2_MASK_BAK    0
#define C64_STEREO_ADDRSEL_BAK  0
#endif
