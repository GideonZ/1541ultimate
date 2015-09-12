#ifndef USBSCSI_H
#define USBSCSI_H

#include "usb_base.h"
#include "usb_device.h"
#include "blockdev.h"
#include "file_device.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

struct t_cbw
{
    uint32_t signature;
    uint32_t tag;
    uint32_t data_length;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cmd_length;
    uint8_t  cmd[16];
};

#define CBW_IN  0x80
#define CBW_OUT 0x00

class UsbScsi;

#ifndef BOOTLOADER
class UsbScsiDriver : public UsbDriver
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
    int get_max_lun(UsbDevice *dev);

    uint32_t      id;
    struct t_pipe bulk_in;
    struct t_pipe bulk_out;
    uint8_t       stat_resp[32];
    uint8_t       sense_data[32];
    struct t_cbw    cbw;

    UsbBase *host;
public:
    UsbDevice *device;

    static UsbDriver *test_driver(UsbDevice *dev);
	UsbScsiDriver();
	~UsbScsiDriver();

	void install(UsbDevice *dev);
	void deinstall(UsbDevice *dev);
	void poll(void);

    int status_transport(bool);
    int  request_sense(int lun, bool debug = false);
    int  exec_command(int lun, int cmdlen, bool out, uint8_t *cmd, int resplen, uint8_t *response, bool debug = false);
	void print_sense_error(void);
};
#endif

class UsbScsi : public BlockDevice
{
    bool       initialized;
    //UsbDevice *device;
    UsbScsiDriver *driver;
    int        lun;
    int		   max_lun;
    int		   removable;
    uint32_t      capacity;
    int        block_size;

    char	   name[16];
    char       disp_name[32];
    
public:
    UsbScsi(UsbScsiDriver *drv, int unit, int max_lun);
    ~UsbScsi();
    
    void reset(void);
    char *get_name(void) { return name; }
    char *get_disp_name(void) { return disp_name; }

    void inquiry(void);
	bool test_unit_ready(void);
    void handle_sense_error(uint8_t *);
    DRESULT read_capacity(uint32_t *num_blocks, uint32_t *block_size);

    DSTATUS init(void);
    DSTATUS status(void);
    DRESULT read(uint8_t *, uint32_t, int);
    DRESULT write(const uint8_t *, uint32_t, int);
    DRESULT ioctl(uint8_t, void *);

};

#endif
