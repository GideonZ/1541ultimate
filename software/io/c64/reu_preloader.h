#ifndef REU_PRELOADER_H
#define REU_PRELOADER_H

#include "observer.h"
#include "filemanager.h"
#include "c64.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

class REUPreloader {
    ObserverQueue *observerQueue;
    FileManager *fm;
    ConfigStore *cfg;
    Path *path;
    SemaphoreHandle_t sem;
    char localStatus[512];
    
    static void poll_reu_preload(void *a);
    void poll(void);
    void cleanup(void);
  public:
    REUPreloader();
    virtual ~REUPreloader();

    // stop (optional) is asked between slices of the transfer; see sliced_transfer.h
    int LoadREU(char *status, bool (*stop)(void) = NULL);
    int SaveREU(char *status, bool (*stop)(void) = NULL);
};

#endif // REU_PRELOADER_H
