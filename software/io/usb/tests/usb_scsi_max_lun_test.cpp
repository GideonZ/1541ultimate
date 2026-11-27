#include "host_test/host_test.h"
#include "../usb_scsi_max_lun.h"

// Return codes of UsbBase::control_exchange (usb_base.cc): bytes received on
// success, -4 on STALL, -1 when the setup stage failed, -9 when the USB mutex
// could not be taken, 0 for a timeout or an empty reply.
namespace {
const int kStall = -4;
const int kSetupFailed = -1;
const int kMutexTimeout = -9;
// Whatever happened to be on the stack when the request failed.
const uint8_t kGarbage = 0xA5;
}

TEST(UsbScsiMaxLunTest, StallMeansSingleLun)
{
    EXPECT_EQ(0, usb_scsi_max_lun(kStall, kGarbage));
}

TEST(UsbScsiMaxLunTest, OtherErrorsMeanSingleLun)
{
    EXPECT_EQ(0, usb_scsi_max_lun(kSetupFailed, kGarbage));
    EXPECT_EQ(0, usb_scsi_max_lun(kMutexTimeout, kGarbage));
}

TEST(UsbScsiMaxLunTest, TimeoutOrEmptyReplyMeansSingleLun)
{
    EXPECT_EQ(0, usb_scsi_max_lun(0, kGarbage));
}

TEST(UsbScsiMaxLunTest, ReplyZero)
{
    EXPECT_EQ(0, usb_scsi_max_lun(1, 0));
}

TEST(UsbScsiMaxLunTest, ReplyThree)
{
    EXPECT_EQ(3, usb_scsi_max_lun(1, 3));
}

TEST(UsbScsiMaxLunTest, ReplyFifteenIsLastValidSlot)
{
    EXPECT_EQ(USB_SCSI_LUN_SLOTS - 1, usb_scsi_max_lun(1, 15));
}

TEST(UsbScsiMaxLunTest, ReplySixteenIsClamped)
{
    EXPECT_EQ(USB_SCSI_LUN_SLOTS - 1, usb_scsi_max_lun(1, 16));
}

TEST(UsbScsiMaxLunTest, Reply255IsClamped)
{
    EXPECT_EQ(USB_SCSI_LUN_SLOTS - 1, usb_scsi_max_lun(1, 255));
}
