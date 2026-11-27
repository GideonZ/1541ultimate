#ifndef USB_SCSI_MAX_LUN_H
#define USB_SCSI_MAX_LUN_H

#include <stdint.h>

// Number of per-LUN slots in UsbScsiDriver (and the largest LUN count that the
// Bulk-Only Transport specification allows: LUN values 0..15).
#define USB_SCSI_LUN_SLOTS 16

// Decides the highest LUN to install from the outcome of the Get Max LUN
// control request. 'result' is what control_exchange returned: the number of
// bytes received, or a negative error code (a device may STALL the request
// when it has a single LUN). 'first_byte' is the first received byte and is
// only meaningful when at least one byte was received.
static inline int usb_scsi_max_lun(int result, uint8_t first_byte)
{
    if (result < 1)
        return 0;
    if (first_byte > USB_SCSI_LUN_SLOTS - 1)
        return USB_SCSI_LUN_SLOTS - 1;
    return (int)first_byte;
}

#endif
