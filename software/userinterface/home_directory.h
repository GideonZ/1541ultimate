#ifndef HOME_DIRECTORY_H
#define HOME_DIRECTORY_H

#include "config.h"
#include "observer.h"
#include "filemanager.h"
#include "userinterface.h"
#include "tree_browser.h"

#include "FreeRTOS.h"
#include "task.h"

class HomeDirectory
{
    UserInterface *ui;
    TreeBrowser *browser;
    ObserverQueue *observerQueue;
    FileManager *fm;
    Path *path;
    
    static void poll_home_directory(void *a);
    void poll(void);

  public:
    HomeDirectory(UserInterface *ui, TreeBrowser *browser);
    virtual ~HomeDirectory();

    static const char* getHomeDirectory(void);
    static void setHomeDirectory(const char* path);
};

#endif // HOME_DIRECTORY_H
