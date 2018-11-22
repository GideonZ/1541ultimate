#include "reu_preloader.h"

REUPreloader::REUPreloader()
{
#ifndef NO_FILE_ACCESS
    cfg = c64->cfg;

    if(cfg->get_value(CFG_C64_REU_PRE)) {

        fm = FileManager :: getFileManager();
        path = fm->get_new_path("REUPreloader");
        path->cd(cfg->get_string(CFG_C64_REU_IMG));

        observerQueue = new ObserverQueue("REU Preloader");
        fm->registerObserver(observerQueue);

        xTaskCreate( REUPreloader :: poll_reu_preload, "REU Preloader", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL);
    }
#endif
}

REUPreloader::~REUPreloader()
{
    if (observerQueue) {
        fm->deregisterObserver(observerQueue);
        delete observerQueue;
    }
    if(path) {
        fm->release_path(path);
    }
}

void REUPreloader::poll_reu_preload(void *a)
{
    REUPreloader *l = (REUPreloader*)a;
    l->poll();
}

void REUPreloader::poll(void)
{
    FileManagerEvent *event;

    while(1) {
        if ((event = (FileManagerEvent *)observerQueue->waitForEvent(0))) {

            if(event->eventType == eNodeUpdated && event->pathName == "/") {

                if(strcasecmp(path->getElement(0), event->newName.c_str()) == 0) {

                    vTaskDelay(25);
                    preload();

                    vTaskDelete(NULL);
                    delete(this);
                    break;
                }
            }
        }
        vTaskDelay(25);
    }
}


void REUPreloader :: preload(void)
{
#ifndef NO_FILE_ACCESS
  uint32_t reu_sizes[] =
    { 0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000, 0x1000000 };

  if(cfg->get_value(CFG_C64_REU_EN) && cfg->get_value(CFG_C64_REU_PRE)) {

    FileManager *fm = FileManager :: getFileManager();
    const char *path = cfg->get_string(CFG_C64_REU_IMG);

    File *f = 0;
    if(fm->fopen(path, FA_READ, &f) != FR_OK) {
      printf("REU Preloader: Failed to open %s\n", path);
      return;
    }

    uint32_t offset = 0;
    if(cfg->get_value(CFG_C64_REU_OFFS)) {
      offset = reu_sizes[cfg->get_value(CFG_C64_REU_OFFS)];
    }

    uint32_t transferred;
    uint32_t address = REU_MEMORY_BASE + offset;
    uint32_t size = reu_sizes[cfg->get_value(CFG_C64_REU_SIZE)];
    
    if(offset < size) {
        f->read((uint8_t *)address, size - offset, &transferred);
        printf("REU preloader: loaded %d bytes to %d from %s\n", transferred, address, path);
    }
    else {
        printf("REU preloader: Preload Offset %d >= REU size %d, skipping\n", offset, size);
    }

    fm->fclose(f);
  }
#endif
}
