#ifndef USB_88772_H
#define USB_88772_H

#include "usb_base.h"
#include "usb_device.h"
#include "network_interface.h"

class UsbAx88772Driver : public UsbDriver
{
	int  irq_transaction;
    int  bulk_transaction;

    BYTE irq_data[8];
    BYTE temp_buffer[16];
    
    UsbBase   *host;
    UsbDevice *device;
    NetworkInterface *netstack;

    int  bulk_in;
    int  bulk_out;
    struct t_pipe bulk_out_pipe;

    bool link_up;

    bool read_mac_address();
    void write_mac_address();
    
    void write_phy_register(BYTE reg, WORD value);
    WORD read_phy_register(BYTE reg);

public:
	UsbAx88772Driver(IndexedList<UsbDriver *> &list);
	UsbAx88772Driver();
	~UsbAx88772Driver();

	UsbAx88772Driver *create_instance(void);
	bool test_driver(UsbDevice *dev);
	void install(UsbDevice *dev);
	void deinstall(UsbDevice *dev);
	void poll(void);
	void pipe_error(int pipe);

    //BYTE output_callback(struct netif *, struct pbuf *);
    BYTE output_packet(BYTE *buffer, int pkt_len);

    void interrupt_handler(BYTE *, int);
    void bulk_handler(BYTE *, int);
    void free_buffer(BYTE *b);
};

#endif
