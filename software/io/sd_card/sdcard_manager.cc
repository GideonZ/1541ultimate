/*
 * sdcard_manager.cc
 *
 *  Created on: Apr 6, 2010
 *      Author: Gideon
 */
#include "itu.h"
#include "dump_hex.h"
#include "sdcard_manager.h"

SdCardManager sd_card_manager;

static void poll_sdcard(void *param)
{
	SdCardManager *sdm = (SdCardManager *)param;
	while(1) {
		sdm->poll();
		vTaskDelay(20);
	}
}

SdCardManager :: SdCardManager(void)
{
	fm = FileManager :: getFileManager();
	init();
}

SdCardManager :: ~SdCardManager()
{
	vTaskDelete(task_handle);
	fm->remove_root_entry(sd_dev);
	delete sd_dev;
	delete sd_card;
}

void SdCardManager :: init()
{
	sd_card = new SdCard; // block device
	sd_dev = new FileDevice(sd_card, "SD", "SD Card");
	fm->add_root_entry(sd_dev);
	xTaskCreate(poll_sdcard, "SD Card Manager", configMINIMAL_STACK_SIZE, this, PRIO_BACKGROUND, &task_handle );
}

void SdCardManager :: poll()
{
	int	sense = sdio_sense(); // doesn't cost much time to do it every time
	switch(sd_card->get_state()) {
		case e_device_unknown: // boot state
		    if(sense & SD_CARD_DETECT) {
				if(sd_card->init()) { // initialize card
					sd_card->set_state(e_device_error);
				} else { // initial insertion
					sd_card->set_state(e_device_ready);
                    sd_dev->attach_disk(512); // block size
				}
				fm->sendEventToObservers(eNodeUpdated, "/", sd_dev->get_name());
		    } else {
		    	sd_card->set_state(e_device_no_media);
				fm->sendEventToObservers(eNodeUpdated, "/", sd_dev->get_name());
		    }
		    break;

		case e_device_no_media:
		    if(sense & SD_CARD_DETECT) { // insertion
		    	sd_card->set_state(e_device_not_ready);
				fm->sendEventToObservers(eNodeUpdated, "/", sd_dev->get_name());
		    }
		    break;

		case e_device_not_ready:
			vTaskDelay(50);
			if(sd_card->init()) { // initialize card
				sd_card->set_state(e_device_error);
				fm->sendEventToObservers(eNodeUpdated, "/", sd_dev->get_name());
			} else {
				sd_card->set_state(e_device_ready);
                sd_dev->attach_disk(512); // block size
				fm->sendEventToObservers(eNodeUpdated, "/", sd_dev->get_name());
			}
			break;
		
		case e_device_ready:
			if(!(sense & SD_CARD_DETECT)) { // removal
				fm->sendEventToObservers(eNodeMediaRemoved, "/", sd_dev->get_name());
				sd_dev->detach_disk();
				sd_card->set_state(e_device_no_media);
				fm->invalidate(sd_dev);
			}
			break;

		case e_device_error:
			if(!(sense & SD_CARD_DETECT)) { // removal
				fm->sendEventToObservers(eNodeMediaRemoved, "/", sd_dev->get_name());
				sd_dev->detach_disk();
				sd_card->set_state(e_device_no_media);
				fm->invalidate(sd_dev);
			}
			break;
			
		default:
			sd_card->set_state(e_device_unknown);
	}
}
