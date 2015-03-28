#ifndef USBSCSI_H
#define USBSCSI_H

#include "usb_base.h"
#include "usb_device.h"
#include "blockdev.h"
#include "file_device.h"

struct t_cbw
{
    DWORD signature;
    DWORD tag;
    DWORD data_length;
    BYTE  flags;
    BYTE  lun;
    BYTE  cmd_length;
    BYTE  cmd[16];
};

#define CBW_IN  0x80
#define CBW_OUT 0x00

class UsbScsi;

#ifndef BOOTLOADER
class UsbScsiDriver : public UsbDriver
{
	FileDevice *path_dev[16];
	UsbScsi *scsi_blk_dev[16];
	t_device_state state_copy[16];
	int poll_interval[16];
	bool media_seen[16];
	
	int max_lun;
	int current_lun;
    int get_max_lun(UsbDevice *dev);

    DWORD      id;
    struct t_pipe bulk_in;
    struct t_pipe bulk_out;
    BYTE       stat_resp[32];
    BYTE       sense_data[32];
    struct t_cbw    cbw;

    UsbBase *host;
public:
    UsbDevice *device;
	UsbScsiDriver(IndexedList<UsbDriver *> &list);
	UsbScsiDriver();
	~UsbScsiDriver();

	UsbScsiDriver *create_instance(void);
	bool test_driver(UsbDevice *dev);
	void install(UsbDevice *dev);
	void deinstall(UsbDevice *dev);
	void poll(void);

    int status_transport(bool);
    int  request_sense(int lun, bool debug = false);
    int  exec_command(int lun, int cmdlen, bool out, BYTE *cmd, int resplen, BYTE *response, bool debug = false);
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
    DWORD      capacity;
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
    void handle_sense_error(BYTE *);
    DRESULT read_capacity(DWORD *num_blocks, DWORD *block_size);

    DSTATUS init(void);
    DSTATUS status(void);
    DRESULT read(BYTE *, DWORD, int);
    DRESULT write(const BYTE *, DWORD, int);
    DRESULT ioctl(BYTE, void *);

};

#endif
