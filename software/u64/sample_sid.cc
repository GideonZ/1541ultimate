#include "altera_msgdma.h"
#include <stdio.h>
#include <stdint.h>

void sample_sid(void)
{
    alt_msgdma_dev *dma_in = alt_msgdma_open("/dev/msgdma_0_csr");
    if (!dma_in) {
        printf("Cannot find DMA unit to record audio.\n");
        return;
    }

    alt_msgdma_standard_descriptor desc_in;
    desc_in.write_address = (uint32_t *)0x02000000;
    desc_in.transfer_length = 8*1024*1024;
    desc_in.control = 0x80000000; // just once
    alt_msgdma_standard_descriptor_sync_transfer(dma_in, &desc_in);
}
