#ifndef REU_PRELOADER_H
#define REU_PRELOADER_H

#include "observer.h"
#include "filemanager.h"
#include "c64.h"

#include "FreeRTOS.h"
#include "task.h"

class REUPreloader {
    ObserverQueue *observerQueue;
    FileManager *fm;
    ConfigStore *cfg;
    Path *path;
    
    static void poll_reu_preload(void *a);
    void poll(void);
    void preload();

  public:
    REUPreloader();
    virtual ~REUPreloader();
};

#endif // REU_PRELOADER_H
