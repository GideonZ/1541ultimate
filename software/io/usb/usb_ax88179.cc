#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "itu.h"
#include "integer.h"
#include "usb_ax88179.h"
#include "ax88179_frame.h"
#include "FreeRTOS.h"
#include "task.h"
#include "endianness.h"

#if DATACACHE
#define BIT31 0x80000000
#else
#define BIT31 0
#endif

// Vendor requests and registers, as in the Linux driver ax88179_178a.c.
#define AX_ACCESS_MAC           0x01
#define AX_ACCESS_PHY           0x02
#define AX_PAUSE_WATERLVL_HIGH  0x54
#define AX_PAUSE_WATERLVL_LOW   0x55

#define PHYSICAL_LINK_STATUS    0x02
#define AX_USB_HS               0x02
#define AX_RX_CTL               0x0B
#define AX_RX_CTL_DROPCRCERR    0x0100
#define AX_RX_CTL_IPE           0x0200
#define AX_RX_CTL_START         0x0080
#define AX_RX_CTL_AP            0x0020
#define AX_RX_CTL_AB            0x0008
#define AX_RX_CTL_AMALL         0x0002
#define AX_NODE_ID              0x10
#define AX_MEDIUM_STATUS_MODE   0x22
#define AX_MEDIUM_GIGAMODE      0x0001
#define AX_MEDIUM_FULL_DUPLEX   0x0002
#define AX_MEDIUM_EN_125MHZ     0x0008
#define AX_MEDIUM_RXFLOW_CTRLEN 0x0010
#define AX_MEDIUM_TXFLOW_CTRLEN 0x0020
#define AX_MEDIUM_RECEIVE_EN    0x0100
#define AX_MEDIUM_PS            0x0200
#define AX_PHYPWR_RSTCTL        0x26
#define AX_PHYPWR_RSTCTL_IPRL   0x0020
#define AX_RX_BULKIN_QCTRL      0x2E
#define AX_CLK_SELECT           0x33
#define AX_CLK_SELECT_BCS       0x01
#define AX_CLK_SELECT_ACS       0x02
#define AX_RXCOE_CTL            0x34
#define AX_TXCOE_CTL            0x35

#define AX88179_PHY_ID          0x03
#define MII_BMCR                0x00
#define BMCR_ANENABLE           0x1000
#define BMCR_ANRESTART          0x0200
#define GMII_PHY_PHYSR          0x11
#define GMII_PHY_PHYSR_SMASK    0xC000
#define GMII_PHY_PHYSR_GIGA     0x8000
#define GMII_PHY_PHYSR_100      0x4000
#define GMII_PHY_PHYSR_FULL     0x2000
#define GMII_PHY_PHYSR_LINK     0x0400

// Bulk-in aggregation settings: control, timer low, timer high, size in KB, gap.
static const uint8_t bulkin_gigabit_hs[5] = { 7, 0x20, 3, 0x16, 0xFF };
static const uint8_t bulkin_100_hs[5]     = { 7, 0xAE, 7, 0x18, 0xFF };
static const uint8_t bulkin_default[5]    = { 7, 0xCC, 0x4C, 0x18, 8 };

// Entry points for call-backs.
static void UsbAx88179Driver_interrupt_callback(void *object) {
    ((UsbAx88179Driver *)object)->interrupt_handler();
}

static void UsbAx88179Driver_bulk_callback(void *object) {
    ((UsbAx88179Driver *)object)->bulk_handler();
}

// Received frames are copied into the network stack, so there is nothing to give back.
static void UsbAx88179Driver_free_buffer(void *drv, void *b) {
}

static err_t UsbAx88179Driver_output(void *drv, void *b, int len) {
    return ((UsbAx88179Driver *)drv)->output_packet((uint8_t *)b, len);
}

FactoryRegistrator<UsbInterface *, UsbDriver *> ax88179_tester(UsbInterface :: getUsbDriverFactory(), UsbAx88179Driver :: test_driver);

UsbAx88179Driver :: UsbAx88179Driver(UsbInterface *intf) : UsbDriver(intf)
{
    device = NULL;
    host = NULL;
    netstack = NULL;
    link_up = false;
    irq_transaction = -1;
    bulk_transaction = -1;
    bulkMaxPacket = 512;

    // Reached through BIT31 so that they land in uncached space on any target
    // that turns the data cache on.
    rxBlock = new uint8_t[AX179_RX_BUFFER_LEN];
    rxBuffer = &rxBlock[BIT31];
    txBlock = new uint8_t[AX179_TX_BUFFER_LEN];
    txBuffer = &txBlock[BIT31];
}

UsbAx88179Driver :: ~UsbAx88179Driver()
{
    if(netstack)
        releaseNetworkStack(netstack);
    delete[] rxBlock;
    delete[] txBlock;
}

UsbDriver * UsbAx88179Driver :: test_driver(UsbInterface *intf)
{
    UsbDevice *dev = intf->getParentDevice();
    if(le16_to_cpu(dev->device_descr.vendor) != 0x0b95) {
        return 0;
    }
    // 0x1790 is the AX88179 (USB 3.0), 0x178A the AX88178A (USB 2.0). They share
    // one command set.
    uint16_t prodID = le16_to_cpu(dev->device_descr.product);
    if ((prodID != 0x1790) && (prodID != 0x178A)) {
        return 0;
    }

    printf("** Asix AX88%s found **\n", (prodID == 0x1790) ? "179" : "178A");
    return new UsbAx88179Driver(intf);
}

int UsbAx88179Driver :: read_reg(uint8_t cmd, uint16_t value, uint16_t index, void *data, uint16_t size)
{
    uint8_t req[8] = { 0xC0, cmd, (uint8_t)value, (uint8_t)(value >> 8),
                       (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)size, (uint8_t)(size >> 8) };
    return host->control_exchange(&device->control_pipe, req, 8, data, size);
}

int UsbAx88179Driver :: write_reg(uint8_t cmd, uint16_t value, uint16_t index, const void *data, uint16_t size)
{
    // control_write() hands the setup packet to the USB engine as it is, so it
    // lives in the driver object, as does the data.
    setup[0] = 0x40;
    setup[1] = cmd;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)size;
    setup[7] = (uint8_t)(size >> 8);
    memcpy(temp_buffer, data, size);
    return host->control_write(&device->control_pipe, setup, 8, temp_buffer, size);
}

uint16_t UsbAx88179Driver :: read_phy_register(uint8_t reg)
{
    uint8_t v[2] = { 0, 0 };
    read_reg(AX_ACCESS_PHY, AX88179_PHY_ID, reg, v, 2);
    return uint16_t(v[0]) | (uint16_t(v[1]) << 8);
}

void UsbAx88179Driver :: write_phy_register(uint8_t reg, uint16_t value)
{
    uint8_t v[2] = { (uint8_t)value, (uint8_t)(value >> 8) };
    write_reg(AX_ACCESS_PHY, AX88179_PHY_ID, reg, v, 2);
}

bool UsbAx88179Driver :: read_mac_address()
{
    uint8_t mac[8];
    if (read_reg(AX_ACCESS_MAC, AX_NODE_ID, 6, mac, 6) != 6) {
        return false;
    }
    printf("MAC address:  %b:%b:%b:%b:%b:%b\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if ((mac[0] & 1) || !(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])) {
        printf("Invalid MAC address.\n");
        return false;
    }
    if (netstack)
        netstack->set_mac_address(mac);
    return true;
}

void UsbAx88179Driver :: install(UsbInterface *intf)
{
    UsbDevice *dev = intf->getParentDevice();
    printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    interface = intf;
    device = dev;

    dev->set_configuration(dev->get_device_config()->config_value);

    netstack = getNetworkStack(this, UsbAx88179Driver_output, UsbAx88179Driver_free_buffer);
    if(!netstack) {
        puts("** Warning! No network stack available!");
    }

    struct t_endpoint_descriptor *iin = interface->find_endpoint(0x83);
    struct t_endpoint_descriptor *bin = interface->find_endpoint(0x82);
    struct t_endpoint_descriptor *bout = interface->find_endpoint(0x02);
    if (!iin || !bin || !bout) {
        printf("** AX88179: endpoints missing.\n");
        return;
    }
    bulkMaxPacket = bout->max_packet_size;

    uint8_t v[5];
    uint16_t w;

    // Power up the PHY, then select its clocks.
    w = 0;
    write_reg(AX_ACCESS_MAC, AX_PHYPWR_RSTCTL, 2, &w, 2);
    w = AX_PHYPWR_RSTCTL_IPRL;
    write_reg(AX_ACCESS_MAC, AX_PHYPWR_RSTCTL, 2, &w, 2);
    vTaskDelay(pdMS_TO_TICKS(500));

    v[0] = AX_CLK_SELECT_ACS | AX_CLK_SELECT_BCS;
    write_reg(AX_ACCESS_MAC, AX_CLK_SELECT, 1, v, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    if (!read_mac_address()) {
        return;
    }

    memcpy(v, bulkin_default, 5);
    write_reg(AX_ACCESS_MAC, AX_RX_BULKIN_QCTRL, 5, v, 5);

    v[0] = 0x34;
    write_reg(AX_ACCESS_MAC, AX_PAUSE_WATERLVL_LOW, 1, v, 1);
    v[0] = 0x52;
    write_reg(AX_ACCESS_MAC, AX_PAUSE_WATERLVL_HIGH, 1, v, 1);

    // The network stack computes its own checksums.
    v[0] = 0;
    write_reg(AX_ACCESS_MAC, AX_RXCOE_CTL, 1, v, 1);
    write_reg(AX_ACCESS_MAC, AX_TXCOE_CTL, 1, v, 1);

    // Start receiving: own address and broadcast, IP header aligned by two bytes.
    w = AX_RX_CTL_DROPCRCERR | AX_RX_CTL_IPE | AX_RX_CTL_START |
        AX_RX_CTL_AP | AX_RX_CTL_AMALL | AX_RX_CTL_AB;
    write_reg(AX_ACCESS_MAC, AX_RX_CTL, 2, &w, 2);

    w = AX_MEDIUM_RECEIVE_EN | AX_MEDIUM_TXFLOW_CTRLEN | AX_MEDIUM_RXFLOW_CTRLEN |
        AX_MEDIUM_FULL_DUPLEX | AX_MEDIUM_GIGAMODE;
    write_reg(AX_ACCESS_MAC, AX_MEDIUM_STATUS_MODE, 2, &w, 2);

    // Restart auto negotiation.
    w = read_phy_register(MII_BMCR);
    if (w & BMCR_ANENABLE) {
        write_phy_register(MII_BMCR, w | BMCR_ANRESTART);
    }

    host->initialize_pipe(&bulk_out_pipe, device, bout);

    if (netstack) {
        host->initialize_pipe(&ipipe, device, iin);
        ipipe.Interval = 8000; // 1 Hz
        ipipe.Length = 8;
        ipipe.buffer = irq_data;
        irq_transaction = host->allocate_input_pipe(&ipipe, UsbAx88179Driver_interrupt_callback, this);
        host->resume_input_pipe(irq_transaction);

        host->initialize_pipe(&bpipe, device, bin);
        bpipe.Interval = 1;
        bpipe.Length = AX179_RX_BUFFER_LEN;
        bpipe.buffer = rxBuffer;
        bulk_transaction = host->allocate_input_pipe(&bpipe, UsbAx88179Driver_bulk_callback, this);

        netstack->start();
    }
}

void UsbAx88179Driver :: disable()
{
    host->free_input_pipe(irq_transaction);
    host->free_input_pipe(bulk_transaction);
    printf("AX88179 Disabled.\n");
    if (netstack) {
        netstack->stop();
    }
}

void UsbAx88179Driver :: deinstall(UsbInterface *intf)
{
    printf("AX88179 Deinstalled.\n");
}

// Sets the medium mode and the receive aggregation to what the PHY negotiated.
bool UsbAx88179Driver :: configure_link()
{
    uint8_t sts = 0;
    uint16_t mode = AX_MEDIUM_RECEIVE_EN | AX_MEDIUM_TXFLOW_CTRLEN | AX_MEDIUM_RXFLOW_CTRLEN;
    uint16_t rxctl = AX_RX_CTL_DROPCRCERR | AX_RX_CTL_IPE | AX_RX_CTL_START |
                     AX_RX_CTL_AP | AX_RX_CTL_AMALL | AX_RX_CTL_AB;
    uint16_t zero = 0;

    // Cycle the receiver, as the Linux driver does on every link change.
    write_reg(AX_ACCESS_MAC, AX_RX_CTL, 2, &zero, 2);
    write_reg(AX_ACCESS_MAC, AX_RX_CTL, 2, &rxctl, 2);

    read_reg(AX_ACCESS_MAC, PHYSICAL_LINK_STATUS, 1, &sts, 1);
    uint16_t physr = read_phy_register(GMII_PHY_PHYSR);
    if (!(physr & GMII_PHY_PHYSR_LINK)) {
        return false;
    }

    const uint8_t *bulkin = bulkin_default;
    if ((physr & GMII_PHY_PHYSR_SMASK) == GMII_PHY_PHYSR_GIGA) {
        mode |= AX_MEDIUM_GIGAMODE | AX_MEDIUM_EN_125MHZ;
        if (sts & AX_USB_HS) {
            bulkin = bulkin_gigabit_hs;
        }
    } else if ((physr & GMII_PHY_PHYSR_SMASK) == GMII_PHY_PHYSR_100) {
        mode |= AX_MEDIUM_PS;
        if (sts & AX_USB_HS) {
            bulkin = bulkin_100_hs;
        }
    }
    if (physr & GMII_PHY_PHYSR_FULL) {
        mode |= AX_MEDIUM_FULL_DUPLEX;
    }
    printf("AX88179 link: PHYSR %4x, USB status %b\n", physr, sts);

    uint8_t v[5];
    memcpy(v, bulkin, 5);
    write_reg(AX_ACCESS_MAC, AX_RX_BULKIN_QCTRL, 5, v, 5);
    write_reg(AX_ACCESS_MAC, AX_MEDIUM_STATUS_MODE, 2, &mode, 2);
    return true;
}

void UsbAx88179Driver :: pipe_error(int pipe) // called from IRQ!
{
    printf("UsbAx88179 ERROR ");
    if ((pipe == irq_transaction) || (pipe == bulk_transaction)) {
        printf("on %s pipe. ", (pipe == irq_transaction) ? "IRQ" : "bulk");
        // FIXME: push message in queue from interrupt
        if (netstack) {
            netstack->stop();
        }
        printf("Net stack stopped\n");
    } else {
        printf("Internal error.\n");
    }
}

void UsbAx88179Driver :: interrupt_handler()
{
    int data_len = host->getReceivedLength(irq_transaction);
    cache_load(irq_data, data_len);

    // Bit 16 of the first status word is the link state.
    if (data_len >= 3) {
        if (irq_data[2] & 0x01) {
            if (!link_up && configure_link()) {
                printf("Bringing link up.\n");
                host->resume_input_pipe(bulk_transaction);
                if (netstack)
                    netstack->link_up();
                link_up = true;
            }
        } else {
            if (link_up) {
                printf("Bringing link down.\n");
                host->pause_input_pipe(bulk_transaction);
                if (netstack)
                    netstack->link_down();
                link_up = false;
            }
        }
    }
    host->resume_input_pipe(this->irq_transaction);
}

void UsbAx88179Driver :: bulk_handler()
{
    int data_len = host->getReceivedLength(this->bulk_transaction);

    if (link_up && netstack) {
        cache_load(rxBuffer, data_len);
        struct Ax179RxFrame frames[16];
        int n = ax179_rx_frames(rxBuffer, data_len, frames, 16);
        for (int i = 0; i < n; i++) {
            netstack->input_copy(rxBuffer + frames[i].offset, frames[i].length);
        }
    }
    host->resume_input_pipe(bulk_transaction);
}

err_t UsbAx88179Driver :: output_packet(uint8_t *buffer, int pkt_len)
{
    if (!link_up)
        return ERR_CONN;

    // bulk_out() returns only when the transfer is done, so one buffer is enough.
    int to_send = ax179_tx_block(buffer, pkt_len, txBuffer, AX179_TX_BUFFER_LEN, bulkMaxPacket);
    if (to_send < 0) {
        return ERR_ARG;
    }
    host->bulk_out(&bulk_out_pipe, txBuffer, to_send);
    return ERR_OK;
}
