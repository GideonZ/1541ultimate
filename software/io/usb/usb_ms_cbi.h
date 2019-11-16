#ifndef USB_MS_CBI_H
#define USB_MS_CBI_H

#include "usb_base.h"
#include "usb_device.h"
#include "usb_scsi.h"
#include "blockdev.h"
#include "file_device.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

class UsbScsi;

class UsbCbiDriver : public UsbScsiDriver
{
	bool poll_enable;
	FileManager *file_manager;
	FileDevice *path_dev[16];
	UsbScsi *scsi_blk_dev[16];
	t_device_state state_copy[16];
	uint16_t poll_interval[16];
	bool media_seen[16];
	SemaphoreHandle_t mutex;
	
	int max_lun;
	int current_lun;
    int irq_transaction;
    int mass_storage_reset(void);

    struct t_pipe bulk_in;
    struct t_pipe bulk_out;
    struct t_pipe irq_in;

    uint8_t       cmd_copy[16];
    uint8_t       irq_data[16];
    uint8_t       sense_data_cbi[32];

    UsbDevice *device;
    UsbInterface *interface;
    UsbBase *host;
public:

    static UsbDriver *test_driver(UsbInterface *dev);
	UsbCbiDriver(UsbInterface *intf);
	~UsbCbiDriver();

	UsbDevice  *getDevice(void) { return device; }
	void install(UsbInterface *dev);
	void deinstall(UsbInterface *dev);
	void poll(void);

	void interrupt_handler(void);
    int  request_sense(int lun, bool debug = false, bool handle = false);
    int  exec_command(int lun, int cmdlen, bool out, uint8_t *cmd, int resplen, uint8_t *response, bool debug = false);
};
#endif
