#ifndef USB_88179_H
#define USB_88179_H

#include "usb_base.h"
#include "usb_device.h"
#include "network_interface.h"

// Largest bulk-in transfer the adapter is configured to aggregate: 1024 bytes
// times its size setting of 0x18, plus 2 KB of slack.
#define AX179_RX_BUFFER_LEN (1024 * (0x18 + 2))
#define AX179_TX_BUFFER_LEN (8 + 1536)

class UsbAx88179Driver : public UsbDriver
{
    int  irq_transaction;
    int  bulk_transaction;

    uint8_t irq_data[16];
    uint8_t setup[8];
    uint8_t temp_buffer[16];

    struct t_pipe ipipe;
    struct t_pipe bpipe;
    struct t_pipe bulk_out_pipe;

    uint8_t *rxBlock;
    uint8_t *rxBuffer;
    uint8_t *txBlock;
    uint8_t *txBuffer;

    UsbBase   *host;
    UsbDevice *device;
    UsbInterface *interface;
    NetworkInterface *netstack;

    bool link_up;
    int  bulkMaxPacket;

    int  read_reg(uint8_t cmd, uint16_t value, uint16_t index, void *data, uint16_t size);
    int  write_reg(uint8_t cmd, uint16_t value, uint16_t index, const void *data, uint16_t size);
    uint16_t read_phy_register(uint8_t reg);
    void write_phy_register(uint8_t reg, uint16_t value);
    bool read_mac_address();
    bool configure_link();
public:
    static UsbDriver *test_driver(UsbInterface *intf);

    UsbAx88179Driver(UsbInterface *intf);
    ~UsbAx88179Driver();

    void install(UsbInterface *intf);
    void deinstall(UsbInterface *intf);
    void disable(void);
    void pipe_error(int pipe);

    err_t output_packet(uint8_t *buffer, int pkt_len);

    void interrupt_handler();
    void bulk_handler();
};

#endif
