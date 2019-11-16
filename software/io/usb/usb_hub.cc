#include <string.h>
#include <stdlib.h>
extern "C" {
	#include "itu.h"
    #include "small_printf.h"
	#include "dump_hex.h"
}
#include "integer.h"
#include "usb_hub.h"
#include "FreeRTOS.h"

#if DISABLE_USB_DEBUG
    #define printf(...)
#endif

__inline uint32_t cpu_to_32le(uint32_t a)
{
    uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary system objects
/*********************************************************************/

uint8_t c_get_hub_descriptor[] = { 0xA0, 0x06, 0x00, 0x29, 0x00, 0x00, 0x40, 0x00 };
uint8_t c_get_hub_status[]     = { 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00 };
uint8_t c_get_port_status[]    = { 0xA3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00 };
uint8_t c_set_hub_feature[]    = { 0x20, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_set_port_feature[]   = { 0x23, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_clear_hub_feature[]  = { 0x20, 0x01, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_clear_port_feature[] = { 0x23, 0x01, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00 };

// tester instance
FactoryRegistrator<UsbInterface *, UsbDriver *> hub_tester(UsbInterface :: getUsbDriverFactory(), UsbHubDriver :: test_driver);

// TODO: It should be globally static, but we also need to know per hub port, so we better use a semaphore
bool UsbHubDriver :: reset_busy = false;

UsbHubDriver :: UsbHubDriver(UsbInterface *intf) : UsbDriver(intf)
{
	num_ports = 0;
	for(int i=0;i<7;i++) {
        children[i] = NULL;
    }
    device = NULL;
    host = NULL;
    memset((void *)irq_data, 0, 4);
    reset_timeout = 0;
    port_in_reset = 0;
    irq_stopped = 0;
}

UsbHubDriver :: ~UsbHubDriver()
{

}

UsbDriver *UsbHubDriver :: test_driver(UsbInterface *intf)
{
	UsbDevice *dev = intf->getParentDevice();

	//printf("** Test UsbHubDriver **\n");
	if(dev->device_descr.device_class != 0x09) {
		printf("Device is not a hub..\n");
		return 0;
	}
	if((dev->device_descr.protocol != 0x01)&&(dev->device_descr.protocol != 0x02)) {
		printf("Device protocol: no TT's. [%b]\n", dev->device_descr.protocol);
		return 0;
	}
//	if(dev->interface_descr.sub_class != 0x00) {
//		printf("SubClass is not zero... [%b]\n", dev->interface_descr.sub_class);
//		return 0;
//	}
	printf("** USB HUB Found! **\n");
	// TODO: More tests needed here?
	return new UsbHubDriver(intf);
}


#define PORT_CONNECTION       0x00
#define PORT_ENABLE           0x01
#define PORT_SUSPEND          0x02
#define PORT_OVER_CURRENT     0x03
#define PORT_RESET            0x04
#define PORT_POWER            0x08
#define PORT_LOW_SPEED        0x09

#define C_PORT_CONNECTION     0x10
#define C_PORT_ENABLE         0x11
#define C_PORT_SUSPEND        0x12
#define C_PORT_OVER_CURRENT   0x13
#define C_PORT_RESET          0x14

#define PORT_TEST             0x15
#define PORT_INDICATOR        0x16

#define BIT_PORT_CONNECTION   0x01
#define BIT_PORT_ENABLE       0x02
#define BIT_PORT_SUSPEND      0x04
#define BIT_PORT_OVER_CURRENT 0x08
#define BIT_PORT_RESET        0x10

#define BIT1_PORT_POWER		  0x01
#define BIT1_PORT_LOW_SPEED	  0x02
#define BIT1_PORT_HIGH_SPEED  0x04

// Entry point for call-backs.
void UsbHubDriver_interrupt_handler(void *object) {
	((UsbHubDriver *)object)->interrupt_handler();
}


void UsbHubDriver :: install(UsbInterface *intf)
{
	UsbDevice *dev = intf->getParentDevice();
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    device = dev;
    
	dev->set_configuration(dev->get_device_config()->config_value);

	int i = dev->host->control_exchange(&dev->control_pipe,
								   c_get_hub_descriptor, 8,
								   buf, 64);

    num_ports = int(buf[2]);
    individual_power_switch = ((buf[3] & 0x03) == 1);
    individual_overcurrent  = ((buf[3] & 0x18) == 8);
    compound                = ((buf[3] & 0x04) == 4);
    power_on_time           = 2 * int(buf[5]);
    hub_current             =     int(buf[6]);
    
    printf("Found a hub with %d ports.\n", num_ports);
/*
    if(individual_power_switch)
        printf("with individual power switch, ");
    if(individual_overcurrent)
        printf("with individual overcurrent protection, ");
    printf("which is %spart of a compound device.\n", compound?"":"not ");
*/
    printf("The power-good time is %d ms, and the hub draws %d mA.\n", power_on_time, hub_current);

	i = dev->host->control_exchange(&dev->control_pipe,
								   c_get_hub_status, 8,
								   buf, 64);

/*
	printf("Hub status: (returned %d bytes)\n", i);
    for(int j=0;j<i;j++) {
        printf("%b ", buf[j]);
    }
*/

    printf("Turning on power for each port..\n");
    // Turn on port power for each port
    c_set_port_feature[2] = PORT_POWER;
    for(int j=0;j<num_ports;j++) {
        c_set_port_feature[4] = uint8_t(j+1);
    	i = dev->host->control_exchange(&dev->control_pipe,
    								    c_set_port_feature, 8,
    								    buf, 64);
    }
    wait_ms(power_on_time);        

//    printf("Check all ports for connection..\n");

/*
    for(int j=0;j<num_ports;j++) {

        c_get_port_status[4] = BYTE(j+1);
    	i = dev->host->control_exchange(&dev->control_pipe,
    								    c_get_port_status, 8,
    								    buf, 64);
        if(i == 4) {
        	printf("Port %d of hub on addr %d INITIAL status:", j+1, dev->current_address);
            for(int b=0;b<4;b+=2) {
                printf("%b%b ", buf[b+1], buf[b]);
            } printf("\n");
        } else {
            printf("Failed to read port %d status. Got %d bytes.\n", j+1, i);
            continue;
        }
    }
*/

    printf("-- Install Complete.. HUB on address %d.. --\n", dev->current_address);

    struct t_endpoint_descriptor *iin = intf->find_endpoint(0x83);

    host->initialize_pipe(&ipipe, dev, iin);
    ipipe.Interval = 1160; // 50 Hz; // added 1000 for making it slower
    ipipe.Length = 2; // just read 2 bytes max (max 15 port hub)
    ipipe.buffer = (void *)irq_data;
    ipipe.MaxTrans = 64; // fix?

    irq_transaction = host->allocate_input_pipe(&ipipe, UsbHubDriver_interrupt_handler, this);
    printf("Poll installed on irq_transaction %d\n", irq_transaction);
    host->resume_input_pipe(irq_transaction);
}

void UsbHubDriver :: disable(void)
{
	printf("Disabling HUB %d\n", device->current_address);

	host->free_input_pipe(irq_transaction);
    for(int i=0;i<num_ports;i++) {
    	if (children[i]) {
    		children[i]->disable();
    	}
    }
}

// called from cleanup task
void UsbHubDriver :: deinstall(UsbInterface *intf)
{
    for(int i=0;i<num_ports;i++) {
    	if (children[i]) {
    		host->deinstall_device(children[i]);
    	}
    }
}

void UsbHubDriver :: poll()
{
	handle_irqdata();

	for(int i=0;i<num_ports;i++) {
    	if (children[i]) {
    		children[i]->poll();
    	}
    }
}

// called from USB message thread
void UsbHubDriver :: interrupt_handler()
{
    int data_length = host->getReceivedLength(irq_transaction);

    cache_load((uint8_t *)irq_data, data_length);

    //printf("This = %p. HUB (ADDR=%d) IRQ data (%d bytes), at %p: ", this, device->current_address, data_length, irq_data_in);
	configASSERT(data_length <= 4);

	host->resume_input_pipe(irq_transaction);
}

void UsbHubDriver :: handle_irqdata(void)
{
	uint8_t irqd = irq_data[0];
	if(!irqd) {
        return;
	}

	printf("HUB %d IRQ data: %b\n", device->current_address, irqd);

	int i;
    
    for(int j=0;j<num_ports;j++) {
        if(irqd & (1 << (j+1))) {
            c_get_port_status[4] = uint8_t(j+1);
            i = host->control_exchange(&device->control_pipe,
										c_get_port_status, 8,
										buf, 8);
            if(i == 4) {

            	printf("Port %d of hub on addr %d status:", j+1, device->current_address);
                for(int b=0;b<4;b+=2) {
                    printf("%b%b ", buf[b+1], buf[b]);
                } printf(" Port in reset: %d\n", port_in_reset);

                c_clear_port_feature[4] = uint8_t(j+1);

                if(buf[2] & BIT_PORT_CONNECTION) {
                    //printf("* CONNECTION CHANGE *\n");
                    if(buf[0] & BIT_PORT_CONNECTION) { // if connected, let's reset it!
                    	if (!reset_busy) {
							c_clear_port_feature[2] = C_PORT_CONNECTION;
							i = host->control_exchange(&device->control_pipe,
													   c_clear_port_feature, 8,
													   dummy, 8);

							// this reset is expected to cause a port enable event!
							c_set_port_feature[2] = PORT_RESET;
							c_set_port_feature[4] = uint8_t(j+1);
							wait_ms(power_on_time);
							printf("Issuing reset on port %d.\n", j+1);
							i = host->control_exchange(&device->control_pipe,
													   c_set_port_feature, 8,
													   dummy, 8);
							port_in_reset = j+1;
							reset_busy = true;
							reset_timeout = 4;
                    	}
                    } else { // disconnect
                    	printf("-> Disconnect\n");
                    	if (port_in_reset == j+1) {
                    		port_in_reset = 0;
                    		reset_busy = false;
                    	}
                    	c_clear_port_feature[2] = C_PORT_CONNECTION;
                    	if(children[j]) {
                        	// let the poll task clean it up.
                        	host->queueDeinstall(children[j]);
                        }
                        children[j] = NULL;
                        printf("-> Device stopped, now clearing feature.\n");
                        i = host->control_exchange(&device->control_pipe,
                    							   c_clear_port_feature, 8,
                    							   dummy, 8);
                        printf("-> Disconnect done (#%d:%d)\n", device->current_address, j+1);
                    }                        
                }
                if(buf[2] & BIT_PORT_ENABLE) {
                    //printf("* ENABLE CHANGE *\n");
                    c_clear_port_feature[2] = C_PORT_ENABLE;
                	i = host->control_exchange(&device->control_pipe,
                							   c_clear_port_feature, 8,
                							   dummy, 8);
                }
                if(buf[2] & BIT_PORT_SUSPEND) {
                    //printf("* SUSPEND CHANGE *\n");
                    c_clear_port_feature[2] = C_PORT_SUSPEND;
                	i = host->control_exchange(&device->control_pipe,
                							   c_clear_port_feature, 8,
                							   dummy, 8);
                }
                if(buf[2] & BIT_PORT_OVER_CURRENT) {
                    printf("* OVERCURRENT CHANGE *\n");
                    c_clear_port_feature[2] = C_PORT_OVER_CURRENT;
                	i = host->control_exchange(&device->control_pipe,
                							   c_clear_port_feature, 8,
                							   dummy, 8);
                }
                if(buf[2] & BIT_PORT_RESET) {
                    printf("* RESET CHANGE %d *\n", j+1);
                	if(buf[0] & BIT_PORT_ENABLE) {
                    	port_in_reset = 0;
                    	reset_busy = false;
						if(children[j]) {
							printf("*** ERROR *** Device already present! ***\n");
							host->queueDeinstall(children[j]);
							children[j] = NULL;
		                    c_clear_port_feature[2] = C_PORT_RESET;
		                	i = host->control_exchange(&device->control_pipe,
		                							   c_clear_port_feature, 8,
		                							   dummy, 8);
						} else {
	                    	int speed = (buf[1] & BIT1_PORT_HIGH_SPEED) ? 2 :
	                    		     	(buf[1] & BIT1_PORT_LOW_SPEED) ? 0 : 1;
	                    	printf("Port reset to %s speed\n", (speed==2)?"high":(speed==1)?"full":"low");
	                    	wait_ms(power_on_time);
							UsbDevice *d = new UsbDevice(host, speed);
							d->set_parent(device, j);
							if(!host->init_device(d)) {
								printf("Device init failed...\n");
								c_clear_port_feature[2] = C_PORT_ENABLE; // disable port
			                	i = host->control_exchange(&device->control_pipe,
			                							   c_clear_port_feature, 8,
			                							   dummy, 8);
			                    c_clear_port_feature[2] = C_PORT_RESET;
			                	i = host->control_exchange(&device->control_pipe,
			                							   c_clear_port_feature, 8,
			                							   dummy, 8);
								continue;
							} else {
			                    c_clear_port_feature[2] = C_PORT_RESET;
			                	i = host->control_exchange(&device->control_pipe,
			                							   c_clear_port_feature, 8,
			                							   dummy, 8);
								children[j] = d;
								// TODO: Put devices in an install queue
								d->init2();
								host->install_device(d, false); // assume powered hub for now
							}
						}
                    } else {
                    	if (reset_timeout <= 0) {
                    		port_in_reset = 0;
                    		reset_busy = false;

                    		printf("RESET TIMEOUT\n");
							c_clear_port_feature[2] = C_PORT_ENABLE; // disable port
		                	i = host->control_exchange(&device->control_pipe,
		                							   c_clear_port_feature, 8,
		                							   dummy, 8);
		                    c_clear_port_feature[2] = C_PORT_RESET;
		                	i = host->control_exchange(&device->control_pipe,
		                							   c_clear_port_feature, 8,
		                							   dummy, 8);
                    	} else {
                    		reset_timeout --;
                    	}
                    }
                }
            } else {
                printf("Failed to read port %d status. Got %d bytes.\n", j+1, i);
                continue;
            }
        }
    }
    host->resume_input_pipe(this->irq_transaction);
    irq_data[0] = 0;
}

void UsbHubDriver :: pipe_error(int pipe) // called from IRQ!
{
	printf("HUB IRQ pipe error, ignoring.\n");
	irq_data[0] = 0xFE;
	host->resume_input_pipe(pipe);
}

void UsbHubDriver :: reset_port(int port)
{
    c_set_port_feature[2] = PORT_RESET;
    c_set_port_feature[4] = uint8_t(port+1);
    printf("HUB: Requested to issue reset on port %d.\n", port+1);
	host->control_exchange(&device->control_pipe,
					       c_set_port_feature, 8,
						   dummy, 8);
}
