#ifndef USB_EM1010_H
#define USB_EM1010_H

#include "usb_base.h"
#include "usb_device.h"
#include "network_interface.h"

#define EMREG_EC0      0x00
#define EMREG_EC1      0x01
#define EMREG_EC2      0x02
#define EMREG_MA0      0x08
#define EMREG_MA1      0x09
#define EMREG_MA2      0x0A
#define EMREG_MA3      0x0B
#define EMREG_MA4      0x0C
#define EMREG_MA5      0x0D
#define EMREG_MA6      0x0E
#define EMREG_MA7      0x0F
#define EMREG_EID0     0x10
#define EMREG_EID1     0x11
#define EMREG_EID2     0x12
#define EMREG_EID3     0x13
#define EMREG_EID4     0x14
#define EMREG_EID5     0x15
#define EMREG_PT       0x18
#define EMREG_RPNBFC   0x1A
#define EMREG_ORFBFC   0x1B
#define EMREG_EP1C     0x1C
#define EMREG_BIST     0x1E
#define EMREG_EEPROMO  0x20
#define EMREG_EEPROMDL 0x21
#define EMREG_EEPROMD  0x22
#define EMREG_EEPROMAC 0x23
#define EMREG_PHYA     0x25
#define EMREG_PHYDL    0x26
#define EMREG_PHYD     0x27
#define EMREG_PHYAC    0x28
#define EMREG_USBBS    0x2A
#define EMREG_TS1      0x2B
#define EMREG_TS2      0x2C
#define EMREG_RS       0x2D
#define EMREG_RLPC     0x2E
#define EMREG_RLPCL    0x2F
#define EMREG_WUF0M(x) (0x30+x)
#define EMREG_WUF0O    0x40
#define EMREG_WUF0CRCL 0x41
#define EMREG_WUF0CRCH 0x42
#define EMREG_WUF1M(x) (0x48+x)
#define EMREG_WUF1O    0x58
#define EMREG_WUF1CRCL 0x59
#define EMREG_WUF1CRCH 0x5A
#define EMREG_WUF2M(x) (0x60+x)
#define EMREG_WUF2O    0x70
#define EMREG_WUF2CRCL 0x71
#define EMREG_WUF2CRCH 0x72
#define EMREG_WUC      0x78
#define EMREG_WUS      0x7A
#define EMREG_IPHYC    0x7B
#define EMREG_GPIO54C  0x7C
#define EMREG_GPIO10C  0x7E
#define EMREG_GPIO32C  0x7F
#define EMREG_Test     0x80
#define EMREG_TM       0x81
#define EMREG_RPN      0x82

class UsbEm1010Driver : public UsbDriver
{
    int  irq_transaction;
    int  bulk_transaction;
    
    UsbBase   *host;
    UsbDevice *device;
    UsbInterface *interface;
    NetworkInterface *netstack;

    int  bulk_in;
    int  bulk_out;
    int  irq_in;
    struct t_pipe bulk_out_pipe;
    bool link_up;

    bool read_register(uint8_t offset, uint8_t &data);
    bool read_registers(uint8_t offset, uint8_t *data, int len);
    bool dump_registers(void);
    bool write_register(uint8_t offset, uint8_t data);
    bool read_phy_register(uint8_t offset, uint16_t *data);
    bool write_phy_register(uint8_t offset, uint16_t data);
    bool read_mac_address();

public:
	static UsbDriver *test_driver(UsbInterface *dev);

	UsbEm1010Driver(UsbInterface *intf);
	~UsbEm1010Driver();

	void install(UsbInterface *dev);
	void deinstall(UsbInterface *dev);
	void disable(void);
	void poll(void);
	void pipe_error(int pipe);

    void bulk_handler(uint8_t *buffer, int length);
    void interrupt_handler(uint8_t *, int);
    uint8_t output_packet(uint8_t *buffer, int length);
    void free_buffer(uint8_t *b);
};

#endif
