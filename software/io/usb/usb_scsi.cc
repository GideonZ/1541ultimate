#include <string.h>
#include <stdlib.h>
extern "C" {
	#include "itu.h"
    #include "small_printf.h"
}
#include "integer.h"
#include "usb.h"
#include "usb_scsi.h"
#include "event.h"

__inline DWORD cpu_to_32le(DWORD a)
{
    DWORD m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

BYTE c_scsi_reset[]     = { 0x21, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary device objects
/*********************************************************************/
#ifndef BOOTLOADER
// tester instance
UsbScsiDriver usb_scsi_driver_tester(usb_drivers);

UsbScsiDriver :: UsbScsiDriver(IndexedList<UsbDriver*> &list)
{
	list.append(this); // add tester
}

UsbScsiDriver :: UsbScsiDriver()
{
}

UsbScsiDriver :: ~UsbScsiDriver()
{

}

UsbScsiDriver *UsbScsiDriver :: create_instance(void)
{
	return new UsbScsiDriver();
}

bool UsbScsiDriver :: test_driver(UsbDevice *dev)
{
	printf("** Test UsbScsiDriver **\n");
	printf("Dev class: %d\n", dev->device_descr.device_class);
	if((dev->device_descr.device_class != 0x08)&&(dev->device_descr.device_class != 0x00)) {
		printf("Device is not mass storage..\n");
		return false;
	}
	if(dev->interface_descr.interface_class != 0x08) {
		printf("Interface class is not mass storage. [%b]\n", dev->interface_descr.interface_class);
		return false;
	}
	if(dev->interface_descr.protocol != 0x50) {
		printf("Protocol is not bulk only. [%b]\n", dev->interface_descr.protocol);
		return false;
	}
//	if(dev->interface_descr.sub_class != 0x06) {
//		printf("SubClass is not transparent SCSI. [%b]\n", dev->interface_descr.sub_class);
//		return false;
//	}

	// TODO: More tests needed here?
	return true;
}

void UsbScsiDriver :: install(UsbDevice *dev)
{
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);

	dev->set_configuration(dev->device_config.config_value);
	max_lun = dev->get_max_lun();
	printf("max lun = %d\n", max_lun);

    // create a device for each LUN
	for(int i=0;i<=max_lun;i++) {
		scsi_blk_dev[i] = new UsbScsi(dev, i);
		scsi_blk_dev[i]->reset();
		path_dev[i] = new FileDevice(&root, scsi_blk_dev[i], scsi_blk_dev[i]->get_name());
		state_copy[i] = scsi_blk_dev[i]->get_state(); // returns unknown, most likely! :)
		printf("*** LUN = %d *** Initial state = %d ***\n", i, state_copy[i]);
		poll_interval[i] = i; // ;-) not all at the same time!
		media_seen[i] = false;
		root.children.append(path_dev[i]);
	}
	push_event(e_refresh_browser, &root);
	current_lun = 0;
}

void UsbScsiDriver :: deinstall(UsbDevice *dev)
{
	for(int i=0;i<=max_lun;i++) {
		root.children.remove(path_dev[i]);
		push_event(e_invalidate, path_dev[i]);
		push_event(e_cleanup_path_object, path_dev[i]);
		push_event(e_cleanup_block_device, scsi_blk_dev[i]);
	}
	push_event(e_refresh_browser, &root);
}

void UsbScsiDriver :: poll(void)
{
	const int c_intervals[] = { 0,     // unknown
								30,    // no media
								2,     // not ready
								30,    // ready
								100 }; // error
	UsbScsi *blk;
	t_device_state old_state, new_state;

    DWORD capacity, block_size;

	// poll intervals are meant to lower the unnecessary
	// traffic on the USB bus.
	if(poll_interval[current_lun] > 0) {
		poll_interval[current_lun] --;
		goto next_lun;
	}
	blk = scsi_blk_dev[current_lun];
	blk->test_unit_ready();
	old_state = state_copy[current_lun];
	new_state = blk->get_state();
	state_copy[current_lun] = new_state;

	if(media_seen[current_lun] && (new_state==e_device_no_media)) { // removal!
        //printf("Media seen[%d]=%d and new_state=%d. old_state=%d.\n", current_lun, media_seen[current_lun], new_state, old_state);
		media_seen[current_lun] = false;
		push_event(e_invalidate, path_dev[current_lun]);
		push_event(e_detach_disk, path_dev[current_lun]);
	}

	if(new_state == e_device_ready) {
        if(!media_seen[current_lun]) {
            if(blk->read_capacity(&capacity, &block_size) == RES_OK) {
                path_dev[current_lun]->attach_disk(int(block_size));
            } else {
                blk->set_state(e_device_error);
            }
        }
		media_seen[current_lun] = true;
	}

	if(old_state != new_state) {
		push_event(e_refresh_browser, &root);
	}

	// lookup new poll interval
	poll_interval[current_lun] = c_intervals[int(new_state)]; // dirty but fast

next_lun:
	// cycle through the luns
	if(current_lun >= max_lun)
		current_lun = 0;
	else
		++current_lun;
}
	
#endif

/*********************************************************************/
/* The actual block device object, working on a usb device
/*********************************************************************/
UsbScsi :: UsbScsi(UsbDevice *d, int unit)
{
    id = 1;
    initialized = false;
    host = NULL;
    device = d;
    lun = unit;
    removable = 0;
    block_size = 512;
    
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = 0x55534243;
    cbw.lun = (BYTE)lun;

    if(d) {
        host = d->host;
        bulk_in  = d->find_endpoint(0x82);
        bulk_out = d->find_endpoint(0x02);
    }
    if((bulk_in > 0) && (bulk_out > 0) && (host) && (d))
        initialized = true;
}

UsbScsi :: ~UsbScsi()
{
}

void UsbScsi :: reset(void)
{
    BYTE *buf;
    
    if(!initialized)
        return;

    if(lun == 0) {
		printf("Executing reset...\n");
		int i = host->control_exchange(device->current_address,
									   c_scsi_reset, 8,
									   NULL, 8, &buf);

		printf("Device reset. (returned %d bytes)\n", i);
	    wait_ms(100);
    }

	set_state(e_device_unknown);
/*
    printf("Unstalling pipes..\n");
    host->unstall_pipe(bulk_in);
	host->unstall_pipe(bulk_out);
*/
    printf("Going to do inquiry..\n");
    inquiry();

/*
    host->unstall_pipe(bulk_in);
	host->unstall_pipe(bulk_out);
*/

}

int UsbScsi :: status_transport(bool do_bulk_in=true)
{
	DWORD *signature = (DWORD *)stat_resp;
	*signature = 0;
	int len;

	if(do_bulk_in)	
		len = host->bulk_in(stat_resp, 16, bulk_in);
	else
		len = 13; // has already been received in error
		
	int i;
	if((len != 13)||(*signature != 0x55534253)) {
		printf("Invalid status (len = %d, signature = %8x)... performing reset..\n", len, *signature);
		goto do_reset;
	} else if(stat_resp[12] == 2) {
		printf("Phase error.. performing reset.\n");
		goto do_reset;
	}
	return (int)stat_resp[12]; // OK, or other error

do_reset:
	i =  host->control_exchange(device->current_address,
								c_scsi_reset, 8,
								NULL, 8, NULL);

	if(i != 0)
		return -2; // reset failed

	host->unstall_pipe(bulk_in);
	host->unstall_pipe(bulk_out);

//	state = e_st_ready;

	return 0; // OK
}

int UsbScsi :: request_sense(bool debug)
{
    if(!initialized)
        return -5;

    cbw.tag = ++id;
    cbw.data_length = cpu_to_32le(18);
    cbw.flags = CBW_IN;
    cbw.lun = (BYTE)lun;
    cbw.cmd_length = 6; // windows uses 12!
    cbw.cmd[0] = 0x03; // REQUEST SENSE
    cbw.cmd[1] = BYTE(lun << 5);
    cbw.cmd[2] = 0; // reserved
    cbw.cmd[3] = 0; // reserved
    cbw.cmd[4] = 18; // allocated return length
    cbw.cmd[5] = 0; // control (?)

    for(int i=6;i<16;i++)
    	cbw.cmd[i] = 0;
    
    host->bulk_out(&cbw, 31, bulk_out);
    int len = host->bulk_in(sense_data, 18, bulk_in);
    if(debug) {
        printf("%d bytes sense data returned: ", len);
        for(int i=0;i<len;i++) {
            printf("%b ", sense_data[i]);
        }
        printf(")\n");
    }
	if(debug)
		print_sense_error();
	return status_transport();
}

void UsbScsi :: handle_sense_error(void)
{
	if(!sense_data[2]) {
		set_state(e_device_ready);
	} else {
		switch(sense_data[12]) {
		case 0x28:  // becoming ready
			set_state(e_device_not_ready);
			break;
		case 0x04:  // not ready
			set_state(e_device_not_ready);
			break;
		case 0x3A:  // no media			
			set_state(e_device_no_media);
			break;
		default:
			printf("Unhandled sense code %b/%b.\n", sense_data[2], sense_data[12]);
		}
	}
}

int UsbScsi :: exec_command(int cmdlen, bool out, BYTE *cmd, int datalen, BYTE *data, bool debug)
{
    if(!initialized)
        return -1;

    cbw.tag = ++id;
    cbw.data_length = cpu_to_32le(datalen);
    cbw.flags = (out)?CBW_OUT:CBW_IN;
    cbw.lun = (BYTE)lun;
    cbw.cmd_length = cmdlen;
    memcpy(&cbw.cmd[0], cmd, cmdlen);
    
    /* CBW PHASE */
    BYTE *b = (BYTE *)&cbw;
    if(debug) {
        printf("Command: ");
        for(int i=0;i<31;i++) {
            printf("%b ", b[i]);
        } printf("\n");
    }
    int len = 0;
    int st = host->bulk_out(&cbw, 31, bulk_out);
    if(st < 0) { // out failed.. let's see if we can get back in sync..
    	printf("Out failed.. let's see if we can get back in sync..\n");
    	if(status_transport() == 0) {
            st = host->bulk_out(&cbw, 31, bulk_out);
            if(st < 0)
                return -2;
        } else {
        	return -6;
        }
    }

	bool read_status = true;
    /* DATA PHASE */
    if((data)&&(datalen)) {
    	if(out) {
			len = host->bulk_out(data, datalen, bulk_out);
			if(debug) {
				printf("%d data bytes sent.\n", len);
			}
    	} else { // in
			len = host->bulk_in(data, datalen, bulk_in);
			if(debug) {
				printf("%d data bytes received: ", len);
				for(int i=0;i<len;i++)
					printf("%b ", data[i]);
				printf(" (");
				for(int i=0;i<len;i++)
					printf("%c", data[i]<32?'.':data[i]);
				printf(")\n");
			}
			if(len != datalen) {
				printf("expected %d bytes, got %d.. unstalling pipe..", datalen, len);
				if(len == 13) { // could be a status!
					memcpy(stat_resp, data, 16); // 16 is likely to be faster than 13
					read_status = false;
				}
			    host->unstall_pipe(bulk_in);
			}
		}
	}

    /* STATUS PHASE */
    st = status_transport(read_status);
   	if(st == 1) {
//		printf("Command error. now issueing REQUEST SENSE.\n");
		request_sense(debug);
		handle_sense_error();
		return -7;
	}
	if(st < 0) {
		return st;
	}

	return len; // response ok, return number of bytes read or written
}


void UsbScsi :: inquiry(void)
{
    BYTE response[64];
    int len, i;

    if(!initialized)
        return;

    BYTE inquiry_command[6] = { 0x12, 0, 0, 0, 36, 0 };
    inquiry_command[1] = BYTE(lun << 5);

    if((len = exec_command(6, false, inquiry_command, 36, response, false)) < 0) {
    	printf("Inquiry failed. %d\n", len);
    	initialized = false; // don't try again
    	return;
    }

    removable = 0;
    if(response[1] & 0x80) {
    	printf("Removable device!\n");
    	removable = 1;
    }
    response[36] = 0;
    printf("Device: %s\n", (char *)&response[8]);

    // copy  name
    char *n = name;
    int j=0;
    for(int i=8;i<16;i++)
    	n[j++] = (char)response[i];
    n[j++] = ' ';
    for(int i=16;i<32;i++)
    	n[j++] = (char)response[i];
    n[j++] = 0;
}

bool UsbScsi :: test_unit_ready(void)
{
    BYTE response[32];
    int len, i;

    if(!initialized)
        return false;

    BYTE test_ready_command[6] = { 0x00, BYTE(lun << 5), 0, 0, 0, 0 };
    int res = exec_command(6, false, test_ready_command, 0, NULL, false);
	if(res == -7) {
		return true; // handled by sense
	}
	if(res >= 0) {
		set_state(e_device_ready);
		return true;
	}
	printf("ERROR Testing Unit.. %d\n", res);
	return false;
}

DRESULT UsbScsi :: read_capacity(DWORD *num_blocks, DWORD *blk_size)
{
    BYTE buf[8];
    
    if(!initialized)
        return RES_NOTRDY;

	if(get_state() != e_device_ready)
        return RES_NOTRDY;

    BYTE read_cap_command[] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    printf("Read capacity.\n");
    int stat = exec_command(10, false, read_cap_command, 8, buf, false);
    
    if(stat >= 0) {
        memcpy(num_blocks, &buf[0], 4);
        ++(*num_blocks);
        memcpy(blk_size, &buf[4], 4);
//        printf("Block Size: %d. Num Blocks: %d\n", *blk_size, *num_blocks);
        this->block_size = int(*blk_size);
        this->capacity   = *num_blocks;
        return RES_OK;
    } else { // command failed
        printf("Read capacity failed.\n");
        return RES_ERROR;
    }
}

#ifndef BOOTLOADER
const char *sense_strings[] = {
 "No sense",
 "Recovered error",
 "Not ready",
 "Medium error",
 "Hardware error",
 "Illegal request",
 "Unit attention",
 "Data protect",
 "Blank check",
 "Vendor-specific",
 "Copy aborted",
 "Aborted command",
 "Equal",
 "Volume overflow",
 "Miscompare" };

struct t_sense_code
{
    BYTE major, minor;
    char *string;
};

struct t_sense_code sense_codes[] = {
    { 0x00, 0x00, "No additional sense information" },
    { 0x00, 0x01, "Filemark detected" },
    { 0x00, 0x02, "End-of-partitions/medium detected" },
    { 0x00, 0x03, "Setmark detected" },
    { 0x00, 0x04, "Beginning of partition/medium detected" },
    { 0x00, 0x05, "End-of-data detected" },
    { 0x00, 0x06, "I/O process terminated" },
    { 0x00, 0x11, "Audio play operation in progress" },
    { 0x00, 0x12, "Audio play operation paused" },
    { 0x00, 0x13, "Audio play operation successfully completed" },
    { 0x00, 0x14, "Audio play operation stopped due to error" },
    { 0x00, 0x15, "No current audio status to return" },
    { 0x01, 0x00, "No index/sector signal" },
    { 0x02, 0x00, "No seek complete" },
    { 0x03, 0x00, "Peripheral device write fault" },
    { 0x03, 0x01, "No write current" },
    { 0x03, 0x02, "Excessive write errors" },
    { 0x04, 0x00, "Logical unit not ready, cause not reportable" },
    { 0x04, 0x01, "Logical unit in process of becoming ready" },
    { 0x04, 0x02, "Logical unit not ready, initializing command required" },
    { 0x04, 0x03, "Logical unit not ready, manual intervention required" },
    { 0x04, 0x04, "Logical unit not ready, format in progress" },
    { 0x05, 0x00, "Logical unit does not respond to selection" },
    { 0x06, 0x00, "Reference position found" },
    { 0x07, 0x00, "Multiple peripheral devices selected" },
    { 0x08, 0x00, "Logical unit communication failure" },
    { 0x08, 0x01, "Logical unit communication time-out" },
    { 0x08, 0x02, "Logical unit communication parity error" },
    { 0x09, 0x00, "Track following error" },
    { 0x09, 0x01, "Tracking servo failure" },
    { 0x09, 0x02, "Focus servo failure" },
    { 0x09, 0x03, "Spindle servo failure" },
    { 0x0a, 0x00, "Error log overflow" },
    { 0x0c, 0x00, "Write error" },
    { 0x0c, 0x01, "Write error recovered with auto reallocation" },
    { 0x0c, 0x02, "Write error auto reallocation failed" },
    { 0x10, 0x00, "ID crc or ecc error" },
    { 0x11, 0x00, "Unrecovered read error" },
    { 0x11, 0x01, "Read retries exhausted" },
    { 0x11, 0x02, "Error too long to correct" },
    { 0x11, 0x03, "Multiple read errors" },
    { 0x11, 0x04, "Unrecovered read error�auto reallocate failed" },
    { 0x11, 0x05, "l-ec uncorrectable error" },
    { 0x11, 0x06, "circ unrecovered error" },
    { 0x11, 0x07, "Data resynchronization error" },
    { 0x11, 0x08, "Incomplete block read" },
    { 0x11, 0x09, "No gap found" },
    { 0x11, 0x0a, "Miscorrected error" },
    { 0x11, 0x0b, "Unrecovered read error�recommend reassignment" },
    { 0x11, 0x0c, "Unrecovered read error�recommend rewrite the data" },
    { 0x12, 0x00, "Address mark not found for ID field" },
    { 0x13, 0x00, "Address mark not found for data field" },
    { 0x14, 0x00, "Recorded entity not found" },
    { 0x14, 0x01, "Record not found" },
    { 0x14, 0x02, "Filemark or setmark not found" },
    { 0x14, 0x03, "End-of-data not found" },
    { 0x14, 0x04, "Block sequence error" },
    { 0x15, 0x00, "Random positioning error" },
    { 0x15, 0x01, "Mechanical positioning error" },
    { 0x15, 0x02, "Positioning error detected by read of medium" },
    { 0x16, 0x00, "Data synchronization mark error" },
    { 0x17, 0x00, "Recovered data with no error correction applied" },
    { 0x17, 0x01, "Recovered data with retries" },
    { 0x17, 0x02, "Recovered data with positive head offset" },
    { 0x17, 0x03, "Recovered data with negative head offset" },
    { 0x17, 0x04, "Recovered data with retries and/or circ applied" },
    { 0x17, 0x05, "Recovered data using previous sector ID" },
    { 0x17, 0x06, "Recovered data without ecc�data auto-reallocated" },
    { 0x17, 0x07, "Recovered data without ecc�recommend reassignment" },
    { 0x17, 0x08, "Recovered data without ecc�recommend rewrite" },
    { 0x18, 0x00, "Recovered data with error correction applied" },
    { 0x18, 0x01, "Recovered data with error correction and retries applied" },
    { 0x18, 0x02, "Recovered data�data auto-reallocated" },
    { 0x18, 0x03, "Recovered data with circ" },
    { 0x18, 0x04, "Recovered data with lec" },
    { 0x18, 0x05, "Recovered data�recommend reassignment" },
    { 0x18, 0x06, "Recovered data�recommend rewrite" },
    { 0x19, 0x00, "Defect list error" },
    { 0x19, 0x01, "Defect list not available" },
    { 0x19, 0x02, "Defect list error in primary list" },
    { 0x19, 0x03, "Defect list error in grown list" },
    { 0x1a, 0x00, "Parameter list length error" },
    { 0x1b, 0x00, "Synchronous data transfer error" },
    { 0x1c, 0x00, "Defect list not found" },
    { 0x1c, 0x01, "Primary defect list not found" },
    { 0x1c, 0x02, "Grown defect list not found" },
    { 0x1d, 0x00, "Miscompare during verify operation" },
    { 0x1e, 0x00, "Recovered ID with ecc" },
    { 0x20, 0x00, "Invalid command operation code" },
    { 0x21, 0x00, "Logical block address out of range" },
    { 0x21, 0x01, "Invalid element address" },
    { 0x22, 0x00, "Illegal function" },
    { 0x24, 0x00, "Invalid field in cdb" },
    { 0x25, 0x00, "Logical unit not supported" },
    { 0x26, 0x00, "Invalid field in parameter list" },
    { 0x26, 0x01, "Parameter not supported" },
    { 0x26, 0x02, "Parameter value invalid" },
    { 0x26, 0x03, "Threshold parameters not supported" },
    { 0x27, 0x00, "Write protected" },
    { 0x28, 0x00, "Not ready to ready transition (medium may have changed)" },
    { 0x28, 0x01, "Import or export element assessed" },
    { 0x29, 0x00, "Power on, reset, or bus device reset occurred" },
    { 0x2a, 0x00, "Parameters changed" },
    { 0x2a, 0x01, "Mode parameters changed" },
    { 0x2a, 0x02, "Log parameters changed" },
    { 0x2b, 0x00, "Copy cannot execute since host cannot disconnect" },
    { 0x2c, 0x00, "Command sequence error" },
    { 0x2c, 0x01, "Too many windows specified" },
    { 0x2f, 0x00, "Commands cleared by another initiator" },
    { 0x30, 0x00, "Incompatible medium installed" },
    { 0x30, 0x01, "Cannot read medium�unknown format" },
    { 0x30, 0x02, "Cannot read medium�incompatible format" },
    { 0x30, 0x03, "Cleaning cartridge installed" },
    { 0x31, 0x00, "Medium format corrupted" },
    { 0x32, 0x00, "No defect spare location available" },
    { 0x32, 0x01, "Defect list update failure" },
    { 0x33, 0x00, "Tape length error" },
    { 0x36, 0x00, "Ribbon, ink, or tower failure" },
    { 0x37, 0x00, "Rounded parameter" },
    { 0x39, 0x00, "Saving parameters not supported" },
    { 0x3a, 0x00, "Medium not present" },
    { 0x3b, 0x00, "Sequential positioning error" },
    { 0x3b, 0x01, "Tape position error at beginning-of-medium" },
    { 0x3b, 0x02, "Tape position error at end-of-medium" },
    { 0x3b, 0x03, "Tape or electronic vertical forms unit not ready" },
    { 0x3b, 0x04, "Slew failure" },
    { 0x3b, 0x05, "Paper jam" },
    { 0x3b, 0x06, "Failed to sense top-of-form" },
    { 0x3b, 0x07, "Failed to sense bottom-of-form" },
    { 0x3b, 0x08, "Reposition error" },
    { 0x3b, 0x09, "Read past end of medium" },
    { 0x3b, 0x0a, "Read past beginning of medium" },
    { 0x3b, 0x0b, "Position past end of medium" },
    { 0x3b, 0x0c, "Position past beginning of medium" },
    { 0x3b, 0x0d, "Medium destination element full" },
    { 0x3b, 0x0e, "Medium source element empty" },
    { 0x3d, 0x00, "Invalid bits in identify message" },
    { 0x3e, 0x00, "Logical unit has not self-configured yet" },
    { 0x3f, 0x00, "Target operation conditions have changed" },
    { 0x3f, 0x01, "Microcode has been changed" },
    { 0x3f, 0x02, "Changed operating definition" },
    { 0x3f, 0x03, "Inquiry data has changed" },
    { 0x40, 0x00, "RAM failure" },
    { 0x41, 0x00, "Data path failure" },
    { 0x42, 0x00, "Power-on or self-test failure" },
    { 0x43, 0x00, "Message error" },
    { 0x44, 0x00, "Internal target failure" },
    { 0x45, 0x00, "Select or reselect failure" },
    { 0x46, 0x00, "Unsuccessful soft reset" },
    { 0x47, 0x00, "SCSI parity error" },
    { 0x48, 0x00, "Initiator detected error message received" },
    { 0x49, 0x00, "Invalid message error" },
    { 0x4a, 0x00, "Command phase error" },
    { 0x4b, 0x00, "Data phase error" },
    { 0x4c, 0x00, "Logical unit failed self-configuration" },
    { 0x4e, 0x00, "Overlapped commands attempted" },
    { 0x50, 0x00, "Write append error" },
    { 0x50, 0x01, "Write append position error" },
    { 0x50, 0x02, "Position error related to timing" },
    { 0x51, 0x00, "Erase failure" },
    { 0x52, 0x00, "Cartridge fault" },
    { 0x53, 0x00, "Media load or eject failed" },
    { 0x53, 0x01, "Unload tape failure" },
    { 0x53, 0x02, "Medium removal prevented" },
    { 0x54, 0x00, "SCSI to host system interface failure" },
    { 0x55, 0x00, "System resource failure" },
    { 0x57, 0x00, "Unable to recover table of contents" },
    { 0x58, 0x00, "Generation does not exist" },
    { 0x59, 0x00, "Updated block read" },
    { 0x5A, 0x00, "Operator request or state change input (unspecified)" },
    { 0x5A, 0x01, "Operator medium removal request" },
    { 0x5A, 0x02, "Operator selected write protect" },
    { 0x5A, 0x02, "Operator selected write permit" },
    { 0x5B, 0x00, "Log exception" },
    { 0x5B, 0x01, "Threshold condition met" },
    { 0x5B, 0x02, "Log counter at maximum" },
    { 0x5B, 0x03, "Log list codes exhausted" },
    { 0x5C, 0x00, "RPL status change" },
    { 0x5C, 0x01, "Spindles synchronized" },
    { 0x5C, 0x02, "Spindles not synchronized" },
    { 0x60, 0x00, "Lamp failure" },
    { 0x61, 0x00, "Video acquisition error" },
    { 0x61, 0x01, "Unable to acquire video" },
    { 0x61, 0x02, "Out of focus" },
    { 0x62, 0x00, "Scan head positioning error" },
    { 0x63, 0x00, "End of user area encountered on this track" },
    { 0x64, 0x00, "Illegal mode for this track" },
	{ 0xFF, 0xFF, "End of list" } };

 
void UsbScsi :: print_sense_error(void)
{
	printf("Sense code: %b (%s)\n", sense_data[2], sense_strings[sense_data[2]] );
	for(int i=0; sense_codes[i].major != 0xFF; i++) {
		if((sense_codes[i].major == sense_data[12]) &&
		   (sense_codes[i].minor == sense_data[13])) {
		   printf("%s\n", sense_codes[i].string);
		   break;
		}
	}
}
#else
void UsbScsi :: print_sense_error(void)
{
	printf("Sense code: %b, (%b.%b)", sense_data[2], sense_data[12], sense_data[13]);
}
#endif

//=================================================
// Public Functions
//=================================================

DSTATUS UsbScsi :: init(void)
{
    if(!initialized)
        return STA_NODISK;
    
    //reset();
    
    return 0;
}

DSTATUS UsbScsi :: status(void)
{
    if(!initialized)
        return STA_NODISK;
}

DRESULT UsbScsi :: read(BYTE *buf, DWORD sector, BYTE num_sectors)
{
    if(!initialized)
        return RES_NOTRDY;

	if(get_state() != e_device_ready)
        return RES_NOTRDY;

        // printf("Read USB sector: %d (%d).\n", sector, num_sectors);

    BYTE read_10_command[] = { 0x28, BYTE(lun << 5), 0,0,0,0, 0x00, 0, num_sectors, 0 };
    
    int len, stat_len;

	USB_COMMAND = USB_CMD_SET_BUSY;
//    for(int s=0;s<num_sectors;s++) {
        //ST_DWORD(&cbw.cmd[2], sector);
        memcpy(&read_10_command[2], &sector, 4);
        
        for(int retry=0;retry<10;retry++) {
            USB_COMMAND = USB_CMD_SET_DEBUG;
            if(exec_command(10, false, read_10_command, block_size*num_sectors, buf, false) != block_size*num_sectors) {
        		USB_COMMAND = USB_CMD_CLEAR_BUSY;
                return RES_ERROR;
            } else
                break;
        }
        
//        buf += block_size;
//        sector ++;
//    }

	USB_COMMAND = USB_CMD_CLEAR_BUSY;
    return RES_OK;
}

DRESULT UsbScsi :: write(const BYTE *buf, DWORD sector, BYTE num_sectors)
{
    if(!initialized)
        return RES_NOTRDY;

	if(get_state() != e_device_ready)
        return RES_NOTRDY;

    BYTE write_10_command[] = { 0x2A, BYTE(lun << 5), 0,0,0,0, 0x00, 0, num_sectors, 0 };
    
    int len, stat_len;

	USB_COMMAND = USB_CMD_SET_BUSY;

    printf("USB: Writing %d sectors from %d.\n", num_sectors, sector);
//    for(int s=0;s<num_sectors;s++) {
        //ST_DWORD(&cbw.cmd[2], sector);
        memcpy(&write_10_command[2], &sector, 4);

        for(int retry=0;retry<10;retry++) {
        	len = exec_command(10, true, write_10_command, block_size*num_sectors, (BYTE *)buf, false);
        	if(len != block_size*num_sectors) {
        		printf("Error %d.\n", len);
        		USB_COMMAND = USB_CMD_CLEAR_BUSY;
                return RES_ERROR;
        	} else
                break;
        }
        
//        buf += block_size;
//        sector ++;
//    }
	USB_COMMAND = USB_CMD_CLEAR_BUSY;
    return RES_OK;
}

DRESULT UsbScsi :: ioctl(BYTE command, void *pdata)
{
    DWORD *data = (DWORD *)pdata;
    DWORD dummy;

    switch(command) {
        case GET_SECTOR_COUNT:
//            printf("Get Sector Count...\n");
            return read_capacity(data, &dummy);
        case GET_SECTOR_SIZE:
            return read_capacity(&dummy, data);
        default:
            printf("IOCTL %d.\n", command);
            return RES_PARERR;
    }
    return RES_OK;

}
