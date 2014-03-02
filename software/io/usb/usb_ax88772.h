#ifndef USB_88772_H
#define USB_88772_H

#include "usb.h"
#include "network_interface.h"

class UsbAx88772Driver : public UsbDriver, NetworkInterface 
{
    int  irq_transaction;
    int  bulk_transaction;
    BYTE irq_data[8];
    
    Usb       *host;
    UsbDevice *device;

    int  bulk_in;
    int  bulk_out;

    BYTE rx_buffer[2000];

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
    err_t output_callback(struct netif *, struct pbuf *);
    
    void interrupt_handler(BYTE *, int);
    void bulk_handler(BYTE *, int);

    //bool  transmit_frame(BYTE *buffer, int length);
    //void  test_packet_out(int size, int filler);
};

#endif
