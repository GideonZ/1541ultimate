#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern "C" {
	#include "itu.h"
	#include "dump_hex.h"
}
#include "integer.h"
#include "usb_ms_cbi.h"
#include "filemanager.h"
#include "FreeRTOS.h"
#include "task.h"
#include "endianness.h"

static uint8_t c_cbi_reset[]                  = { 0x1D, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t c_request_sense[]              = { 0x03, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00 };
static uint8_t c_class_specific_request[]     = { 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00 };

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary device objects
/*********************************************************************/

// tester instance
FactoryRegistrator<UsbInterface *, UsbDriver *> cbi_tester(UsbInterface :: getUsbDriverFactory(), UsbCbiDriver :: test_driver);

UsbCbiDriver :: UsbCbiDriver(UsbInterface *intf) : UsbScsiDriver(intf)
{
	poll_enable = false;
	current_lun = 0;
	max_lun = 0;
	file_manager = FileManager :: getFileManager();
	mutex = xSemaphoreCreateMutex();
	interface = intf;
	device = intf->getParentDevice();
	host = device->host;
}

UsbCbiDriver :: ~UsbCbiDriver()
{

}

UsbDriver *UsbCbiDriver :: test_driver(UsbInterface *intf)
{
	UsbDevice *dev = intf->getParentDevice();
	if((dev->device_descr.device_class != 0x08)&&(dev->device_descr.device_class != 0x00)) {
		return 0;
	}

	if(intf->getInterfaceDescriptor()->interface_class != 0x08) {
		printf("Interface class is not mass storage. [%b]\n", intf->getInterfaceDescriptor()->interface_class);
		return 0;
	}

	if(intf->getInterfaceDescriptor()->protocol != 0x00) {
		printf("Protocol is not CBI. [%b]\n", intf->getInterfaceDescriptor()->protocol);
		return 0;
	}
	printf("** Mass storage CBI device found **\n");
	return new UsbCbiDriver(intf);
}

// Entry point for call-backs.
void UsbCbiDriver_interrupt_callback(void *object) {
    ((UsbCbiDriver *)object)->interrupt_handler();
}


void UsbCbiDriver :: install(UsbInterface *intf)
{
	UsbDevice *dev = interface->getParentDevice();
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);

	dev->set_configuration(dev->get_device_config()->config_value);

	dev->set_interface(interface->getInterfaceDescriptor()->interface_number,
			interface->getInterfaceDescriptor()->alternate_setting);

    struct t_endpoint_descriptor *bin = interface->find_endpoint(0x82);
    struct t_endpoint_descriptor *bout = interface->find_endpoint(0x02);
    struct t_endpoint_descriptor *iin = interface->find_endpoint(0x83);

    int bi = bin->endpoint_address & 0x0F;
    int bo = bout->endpoint_address & 0x0F;
    int ii = iin->endpoint_address & 0x0F;

    if(dev) {
    	host = dev->host;
    	device = dev;
        host->initialize_pipe(&bulk_in, dev, bin);
        host->initialize_pipe(&bulk_out, dev, bout);
        host->initialize_pipe(&irq_in, dev, iin);
        irq_in.Length = 8;
        irq_in.Interval = 400;
        irq_in.buffer = this->irq_data;
        irq_in.MaxTrans = 8;

//        irq_transaction = host->allocate_input_pipe(&irq_in, UsbCbiDriver_interrupt_callback, this);
//        host->resume_input_pipe(irq_transaction);
    }

	// create a block device for each LUN

    // FIXME: The interface descriptor should tell us whether to create UsbScsi devices on top of this transport layer, or maybe Floppy, or others.
    // This is a very raw assumption

    for(int i=0;i<=max_lun;i++) {
		scsi_blk_dev[i] = new UsbScsi(this, i, max_lun);
		scsi_blk_dev[i]->reset();
		path_dev[i] = new FileDevice(scsi_blk_dev[i], scsi_blk_dev[i]->get_name(), scsi_blk_dev[i]->get_disp_name());
		path_dev[i]->setFloppy();

		// path_dev[i]->attach();
		state_copy[i] = scsi_blk_dev[i]->get_state(); // returns unknown, most likely! :)
		//printf("*** LUN = %d *** Initial state = %d ***\n", i, state_copy[i]);
		uint16_t now = getMsTimer();
		now += 500 + (i * 100);
		poll_interval[i] = now;
		media_seen[i] = false;
		file_manager->add_root_entry(path_dev[i]);
	}
	current_lun = 0;
	// printf("Now enabling poll.\n");
	poll_enable = true;
}

void UsbCbiDriver :: interrupt_handler()
{
    int data_len = host->getReceivedLength(irq_transaction);

    if (data_len) {
        printf("CBI (ADDR=%d) IRQ data: ", device->current_address);
        for(int i=0;i<data_len;i++) {
            printf("%b ", irq_data[i]);
        } printf("\n");
    }
    host->resume_input_pipe(this->irq_transaction);
}

void UsbCbiDriver :: deinstall(UsbInterface *intf)
{
	for(int i=0;i<=max_lun;i++) {
        printf("DeInstalling CBI LUN %d\n", i);
        file_manager->remove_root_entry(path_dev[i]);
		delete path_dev[i];
		delete scsi_blk_dev[i];
	}
}

void UsbCbiDriver :: poll(void)
{
	const int c_intervals[] = { 10,    // unknown
								250,   // no media
								20,    // not ready
								500,   // ready
								500 }; // error
	UsbScsi *blk;
	t_device_state old_state, new_state;

    uint32_t capacity, block_size;

    if (!poll_enable)
    	return;

    // poll intervals are meant to lower the unnecessary
	// traffic on the USB bus.
    uint16_t now = getMsTimer();
    uint16_t passed_time = now - poll_interval[current_lun];
    static uint8_t block_buffer[512];

    if(passed_time >= c_intervals[int(state_copy[current_lun])]) {

    	poll_interval[current_lun] = now;
		blk = scsi_blk_dev[current_lun];
		blk->test_unit_ready();
		old_state = state_copy[current_lun];
		new_state = blk->get_state();
		state_copy[current_lun] = new_state;

		if(media_seen[current_lun] && (new_state==e_device_no_media)) { // removal!
			//printf("Media seen[%d]=%d and new_state=%d. old_state=%d.\n", current_lun, media_seen[current_lun], new_state, old_state);
			media_seen[current_lun] = false;
			file_manager->invalidate(path_dev[current_lun]);
			file_manager->sendEventToObservers(eNodeMediaRemoved, "/", path_dev[current_lun]->get_name());
			path_dev[current_lun]->detach_disk();
		}

		if(new_state == e_device_ready) {
			if(!media_seen[current_lun]) {
				if(blk->read_capacity(&capacity, &block_size) == RES_OK) {
					printf("Path Dev %p %p. Current lun %d. BS = %d. CAP = %d\n", path_dev, path_dev[current_lun], current_lun, block_size, capacity);
					path_dev[current_lun]->attach_disk(int(block_size));
					// * debug *
					blk->read(block_buffer, 0, 1);

				} else {
					blk->set_state(e_device_error);
				}
			}
			media_seen[current_lun] = true;
		}

		if(old_state != new_state) {
			file_manager->sendEventToObservers(eNodeUpdated, "/", path_dev[current_lun]->get_name());
		}
    }

    // cycle through the luns
	if(current_lun >= max_lun)
		current_lun = 0;
	else
		++current_lun;
}

int UsbCbiDriver :: exec_command(int lun, int cmdlen, bool out, uint8_t *cmd, int datalen, uint8_t *data, bool debug)
{
    if (!xSemaphoreTake(mutex, 5000)) {
        printf("UsbCbiDriver unavailable (EX).\n");
        return -9;
    }

    debug = false;

    if (cmdlen > 12) {
        cmdlen = 12;
    }
    // get a padded variant of the command
    memset(cmd_copy, 0, 12);
    memcpy(cmd_copy, cmd, cmdlen);

    if (debug) {
        printf("Sending CBI command:\n");
        dump_hex(cmd_copy, 12);
    }

    // Issue command
    //c_class_specific_request[4] = interface->getInterfaceDescriptor()->interface_number;
    int st = host->control_write(&device->control_pipe, c_class_specific_request, 8, cmd_copy, 12);

    if(st < 0) { // control write failed
        printf("Sending command to CBI device failed..\n");
        xSemaphoreGive(mutex);
        return st;
    }

    int len = 0;

    // Handle OUT transfer
    if (out && data) {
        len = host->bulk_out(&bulk_out, data, datalen, 24000); // 3 seconds
        if(debug) {
            printf("%d data bytes sent.\n", len);
        }
    }

    // Handle IN transfer
    if (!out && data) {
        uint16_t pre, post;
        pre = bulk_in.Command;
        len = host->bulk_in(&bulk_in, data, datalen, 24000); // 3 seconds
        post = bulk_in.Command;
        if(debug) {
            printf("%d data bytes received:  (Requested Length: %d, Pre:%04X, Post:%04X)\n", len, datalen, pre, post);
            dump_hex(data, len);
        }
    }

    uint16_t pre, post;
    pre = irq_in.Command;
    int irqbytes = host->bulk_in(&irq_in, irq_data, 8, 1000); // 0.125 seconds
    post = irq_in.Command;
    if(debug) {
        printf("%d IRQ bytes received:   (Requested Length: %d, Pre:%04X, Post:%04X)\n", irqbytes, 8, pre, post);
        dump_hex(irq_data, irqbytes);
    }

    xSemaphoreGive(mutex);

    int retval = len;

    if(irq_data[0] != 0) {
        request_sense(lun, debug, true);
        retval = -7;
    }
    return retval; // if response ok, return number of bytes read or written
}


int UsbCbiDriver :: mass_storage_reset()
{
	uint8_t buf[8];

	printf("Executing CBI reset...\n");
	int i = exec_command(0, 6, false, c_cbi_reset, 0, NULL, true);
	printf("Device reset. (returned %d bytes)\n", i);

	return i;
}

int UsbCbiDriver :: request_sense(int lun, bool debug, bool handle)
{
    int len = exec_command(lun, 8, false, c_request_sense, 18, sense_data_cbi, true);

    if(debug) {
        printf("%d bytes sense data returned: ", len);
        for(int i=0;i<len;i++) {
            printf("%b ", sense_data_cbi[i]);
        }
        printf(")\n");
        print_sense_error(sense_data_cbi);
        if (handle) {
            scsi_blk_dev[lun]->handle_sense_error(sense_data_cbi);
        }
    }

	int retval = len;
	return retval;
}

