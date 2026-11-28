#ifndef SCSI_STUB_ITU_H
#define SCSI_STUB_ITU_H
#include <stdint.h>
// The register writes of the driver (the USB busy LED) go nowhere on the host.
#include "iomap.h"
#undef ioWrite8
#define ITU_USB_BUSY 0
#define ioWrite8(a, b) do { (void)(a); (void)(b); } while (0)
#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
extern "C" uint16_t getMsTimer(void);
#endif
