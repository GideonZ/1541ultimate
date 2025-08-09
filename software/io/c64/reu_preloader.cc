#include "reu_preloader.h"
#include "init_function.h"

REUPreloader *reu_preloader = NULL; // globally static
static void init(void *_a, void *_b)
{
    reu_preloader = new REUPreloader();
}
InitFunction reu_preload_init("REU Preloader", init, NULL, NULL, 98);

static const uint32_t reu_sizes[] = { 0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000, 0x1000000 };

REUPreloader::REUPreloader()
{
    cfg = C64::getMachine()->cfg;
    sem = xSemaphoreCreateMutex();
    fm = FileManager::getFileManager();

    if (cfg->get_value(CFG_C64_REU_PRE)) {

        path = fm->get_new_path("REUPreloader");
        path->cd(cfg->get_string(CFG_C64_REU_IMG));

        observerQueue = new ObserverQueue("REU Preloader");
        fm->registerObserver(observerQueue);

        xTaskCreate(REUPreloader::poll_reu_preload, "REU Preloader", configMINIMAL_STACK_SIZE, this, PRIO_BACKGROUND, NULL);
    }
}

REUPreloader::~REUPreloader()
{
    cleanup();
    if (sem) {
        vSemaphoreDelete(sem);
    }
}

void REUPreloader :: cleanup(void)
{
    if (observerQueue) {
        fm->deregisterObserver(observerQueue);
        delete observerQueue;
    }
    if (path) {
        fm->release_path(path);
    }
}

void REUPreloader::poll_reu_preload(void *a)
{
    REUPreloader *l = (REUPreloader*) a;
    l->poll();
}

void REUPreloader::poll(void)
{
    FileManagerEvent *event;

    while (1) {
        if ((event = (FileManagerEvent *) observerQueue->waitForEvent(25))) {

            if (((event->eventType == eNodeUpdated) || (event->eventType == eNodeAdded)) && event->pathName == "/") {

                if (strcasecmp(path->getElement(0), event->newName.c_str()) == 0) {

                    vTaskDelay(25);
                    LoadREU(localStatus);
                    puts(localStatus);

                    cleanup();
                    vTaskDelete(NULL);
                    break;
                }
            }
            delete event;
        }
    }
}

int REUPreloader::LoadREU(char *status)
{
    uint32_t transferred = 0;
    if (cfg->get_value(CFG_C64_REU_EN)) {

        xSemaphoreTake(sem, portMAX_DELAY); // make sure we are the only one trying to access the REU memory for loading / saving

        FileManager *fm = FileManager::getFileManager();
        const char *fullpath = cfg->get_string(CFG_C64_REU_IMG);

        File *f = 0;
        FRESULT fres = fm->fopen(fullpath, FA_READ, &f);
        if (fres != FR_OK) {
            sprintf(status, "REU Load: Failed to open %s: %s", fullpath, FileSystem :: get_error_string(fres));
            xSemaphoreGive(sem);
            return -1;
        }

        uint32_t offset = 0;
        if (cfg->get_value(CFG_C64_REU_OFFS)) {
            offset = reu_sizes[cfg->get_value(CFG_C64_REU_OFFS) - 1]; // 1 = 128K, so we need to shift by one
        }

        uint32_t address = REU_MEMORY_BASE + offset;
        uint32_t size = reu_sizes[cfg->get_value(CFG_C64_REU_SIZE)];

        if (offset < size) {
            f->read((uint8_t *) address, size - offset, &transferred);
            sprintf(status, "REU Load: Loaded %d bytes to $%6x from %s", transferred, offset, fullpath);
        } else {
            sprintf(status, "REU Load: Offset %d >= REU size %d, skipping", offset, size);
        }

        fm->fclose(f);
        xSemaphoreGive(sem);
    } else { // REU not enabled
        return -2;
    }
    return (int)transferred;
}

int REUPreloader::SaveREU(char *status)
{
    uint32_t transferred = 0;

    if (cfg->get_value(CFG_C64_REU_EN)) {

        xSemaphoreTake(sem, portMAX_DELAY); // make sure we are the only one trying to access the REU memory for loading / saving

        uint32_t offset = 0;
        if (cfg->get_value(CFG_C64_REU_OFFS)) {
            offset = reu_sizes[cfg->get_value(CFG_C64_REU_OFFS) - 1]; // 1 = 128K, so we need to shift by one
        }

        uint32_t address = REU_MEMORY_BASE + offset;
        uint32_t size = reu_sizes[cfg->get_value(CFG_C64_REU_SIZE)];

        if (offset >= size) {
            sprintf(status, "REU Save: Offset %d >= REU size %d, skipping", offset, size);
            xSemaphoreGive(sem);
            return -3;
        }

        FileManager *fm = FileManager::getFileManager();
        const char *fullpath = cfg->get_string(CFG_C64_REU_IMG);
        int bu_size = strlen(fullpath) + 8;
        char *bu = new char[bu_size];
        strcpy(bu, fullpath);
        set_extension(bu, ".bu", bu_size);

        // If the backup exists it gets now deleted
        FRESULT r = fm->delete_file(bu);
        printf("Delete: %s\n", FileSystem :: get_error_string(r));

        // If the original file exists, rename it to .bu
        r = fm->rename(fullpath, bu);
        printf("Rename: %s\n", FileSystem :: get_error_string(r));

        File *f = 0;
        FRESULT fres = fm->fopen(fullpath, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
        if (fres != FR_OK) {
            sprintf(status, "REU Save: Failed to open %s: %s", fullpath, FileSystem :: get_error_string(fres));
            xSemaphoreGive(sem);
            delete[] bu;
            return -1;
        }

        f->write((uint8_t *) address, size - offset, &transferred);
        sprintf(status, "REU Save: Saved %d bytes from $%6x to %s", transferred, offset, fullpath);

        fm->fclose(f);
        xSemaphoreGive(sem);
        delete[] bu;
    } else { // REU not enabled
        return -2;
    }
    return (int)transferred;
}
