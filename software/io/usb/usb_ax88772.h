#ifndef USB_88772_H
#define USB_88772_H

#include "usb_base.h"
#include "usb_device.h"
#include "network_interface.h"

class UsbAx88772Driver : public UsbDriver
{
	int  irq_transaction;
    int  bulk_transaction;

    uint8_t temp_buffer[16];
    
    UsbBase   *host;
    UsbDevice *device;
    NetworkInterface *netstack;

    int  bulk_in;
    int  bulk_out;
    struct t_pipe bulk_out_pipe;

    bool link_up;

    void read_srom();
    void write_srom();
    bool read_mac_address();
    bool write_mac_address();
    
    void write_phy_register(uint8_t reg, uint16_t value);
    uint16_t read_phy_register(uint8_t reg);

public:
	static UsbDriver *test_driver(UsbDevice *dev);

	UsbAx88772Driver();
	~UsbAx88772Driver();

	void install(UsbDevice *dev);
	void deinstall(UsbDevice *dev);
	void poll(void);
	void pipe_error(int pipe);

    //BYTE output_callback(struct netif *, struct pbuf *);
    uint8_t output_packet(uint8_t *buffer, int pkt_len);

    void interrupt_handler(uint8_t *, int);
    void bulk_handler(uint8_t *, int);
    void free_buffer(uint8_t *b);
};

#endif
