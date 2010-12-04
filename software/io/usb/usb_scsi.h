#ifndef USBSCSI_H
#define USBSCSI_H

#include "usb.h"
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
public:
	UsbScsiDriver(IndexedList<UsbDriver *> &list);
	UsbScsiDriver();
	~UsbScsiDriver();

	UsbScsiDriver *create_instance(void);
	bool test_driver(UsbDevice *dev);
	void install(UsbDevice *dev);
	void deinstall(UsbDevice *dev);
	void poll(void);
};
#endif

class UsbScsi : public BlockDevice
{
    DWORD      id;
    bool       initialized;
    Usb       *host;
    UsbDevice *device;
    int        lun;
    int		   removable;
    DWORD      capacity;
    int        block_size;
    
    int        bulk_in;
    int        bulk_out;

    BYTE       stat_resp[32];
    BYTE       sense_data[32];
    char	   name[32];

    struct t_cbw    cbw;
public:
    UsbScsi(UsbDevice *d, int unit);
    ~UsbScsi();
    
    int status_transport(bool);
    void reset(void);
    char *get_name(void) { return name; }
    int  request_sense(bool debug = false);
    void handle_sense_error(void);
    int  exec_command(int cmdlen, bool out, BYTE *cmd, int resplen, BYTE *response, bool debug = false);
    void inquiry(void);
	bool test_unit_ready(void);
	void print_sense_error(void);
    DRESULT read_capacity(DWORD *num_blocks, DWORD *block_size);

    DSTATUS init(void);
    DSTATUS status(void);
    DRESULT read(BYTE *, DWORD, BYTE);
    DRESULT write(const BYTE *, DWORD, BYTE);
    DRESULT ioctl(BYTE, void *);

};

#endif
