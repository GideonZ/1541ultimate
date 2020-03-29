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

    int LoadREU(char *status);
    int SaveREU(char *status);
};

#endif // REU_PRELOADER_H
