#include "home_directory.h"

static char home_directory[1024];

HomeDirectory::HomeDirectory(UserInterface *ui, TreeBrowser *browser)
{
#ifndef NO_FILE_ACCESS
    this->ui = ui;
    this->browser = browser;

    fm = FileManager :: getFileManager();
    path = fm->get_new_path("HomeDirectory");
    path->cd(ui->cfg->get_string(CFG_USERIF_HOME_DIR));
    setHomeDirectory(path->get_path());
    
    observerQueue = new ObserverQueue("HomeDir");
    fm->registerObserver(observerQueue);

//    xTaskCreate( HomeDirectory :: poll_home_directory, "Home Directory", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL);
#endif
}

HomeDirectory::~HomeDirectory() 
{
    printf("-> HomeDirectory destruct.\n");
    if (observerQueue) {
        fm->deregisterObserver(observerQueue);
        delete observerQueue;
    }
    if(path) {
        fm->release_path(path);
    }
    vTaskDelete(NULL);
}

const char* HomeDirectory::getHomeDirectory(void) {
    return home_directory;
}

void HomeDirectory::setHomeDirectory(const char* path)
{
    strncpy((char*)home_directory, path, 1023);
}

void HomeDirectory::poll_home_directory(void *a)
{
    HomeDirectory *l = (HomeDirectory*)a;
    l->poll();
}

void HomeDirectory::poll(void)
{
    FileManagerEvent *event;
    bool available = false;
    bool done = false;
    int delay = 250 / portTICK_PERIOD_MS;
    int timeout = 10000 / portTICK_PERIOD_MS;

    /* Wait for the filesystem on which the home directory resides to
       appear. If it doesn't appear within 10 seconds, abort. This way
       the hone directory is only entered when the media is already present
       at power on, and not if media is inserted some time later.
    */
    if (FileManager::getFileManager()->is_path_valid(path)) {
        available = true;
    }

    while (!available && (timeout > 0)) {
        if ((event = (FileManagerEvent *)observerQueue->waitForEvent(0))) {
            // printf("HOME DIRECTORY: event->eventType = %d. PathName = %s\n", event->eventType, event->pathName.c_str());
            if (((event->eventType == eNodeUpdated) || (event->eventType == eNodeAdded)) && event->pathName == "/") {
                if (strcasecmp(path->getElement(0), event->newName.c_str()) == 0) {
                    // printf("HOME DIRECTORY: filesystem %s mounted\n", event->newName.c_str());
                    available = true;
                    break;
                }
            }
            delete event;
        }
        vTaskDelay(delay);
        timeout -= delay;
    }

    if(available) {
        vTaskDelay(25);
        printf("HOME DIRECTORY: Enter %s\n", path->get_path());
        browser->cd(path->get_path());
    }

    delete(this);
}

