#ifndef USB_88772_H
#define USB_88772_H

#include "usb_base.h"
#include "usb_device.h"
#include "network_interface.h"
#include "fifo.h"

#define NUM_AX_BUFFERS 64

class UsbAx88772Driver : public UsbDriver
{
	int  irq_transaction;
    int  bulk_transaction;

    uint8_t  irq_data[16];
    uint8_t *lastPacketBuffer;

    uint8_t temp_buffer[16];
    
    struct t_pipe ipipe;
    struct t_pipe bpipe;

    Fifo<uint8_t *> freeBuffers;
    uint8_t *dataBuffersBlock;

    UsbBase   *host;
    UsbDevice *device;
    UsbInterface *interface;
    NetworkInterface *netstack;

    struct t_pipe bulk_out_pipe;

    bool link_up;
    uint16_t prodID;

    void read_srom();
    void write_srom();
    bool read_mac_address();
    bool write_mac_address(uint8_t *);
    
    void write_phy_register(uint8_t reg, uint16_t value);
    uint16_t read_phy_register(uint8_t reg);

    uint8_t *getBuffer();
public:
	static UsbDriver *test_driver(UsbInterface *intf);

	UsbAx88772Driver(UsbInterface *intf,uint16_t prodID);
	~UsbAx88772Driver();

	void install(UsbInterface *intf);
	void deinstall(UsbInterface *intf);
	void disable(void);
	void poll(void);
	void pipe_error(int pipe);

    //BYTE output_callback(struct netif *, struct pbuf *);
    err_t output_packet(uint8_t *buffer, int pkt_len);

    void interrupt_handler();
    void bulk_handler();
    void free_buffer(uint8_t *b);
};

#endif
